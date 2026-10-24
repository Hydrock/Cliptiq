import Foundation
import AppKit
import RealmSwift
import SQLite3
import CryptoKit
import ImageIO

// Offline migration never opts into Realm SDK analytics, including debugger runs.
setenv("REALM_DISABLE_ANALYTICS","1",1)

// Schema7 field names mirror Clipy (MIT); no upstream migration callbacks run.
final class CPYClip: Object {
 @objc dynamic var dataPath=""; @objc dynamic var title=""; @objc dynamic var dataHash=""
 @objc dynamic var primaryType=""; @objc dynamic var updateTime=0
 @objc dynamic var thumbnailPath=""; @objc dynamic var isColorCode=false
 override static func primaryKey()->String? { "dataHash" }
}
final class CPYFolder: Object {
 @objc dynamic var index=0; @objc dynamic var enable=true; @objc dynamic var title=""
 @objc dynamic var identifier=""; let snippets=List<CPYSnippet>()
 override static func primaryKey()->String? { "identifier" }
}
final class CPYSnippet: Object {
 @objc dynamic var index=0; @objc dynamic var enable=true; @objc dynamic var title=""
 @objc dynamic var content=""; @objc dynamic var identifier=""
 let folders=LinkingObjects(fromType:CPYFolder.self,property:"snippets")
 override static func primaryKey()->String? { "identifier" }
}
enum MigrationError: Error { case invalid(String) }
func check(_ condition: Bool,_ message: String) throws { if !condition { throw MigrationError.invalid(message) } }
func read(_ path: URL, limit: Int=32*1024*1024) throws -> Data {
 let a=try FileManager.default.attributesOfItem(atPath:path.path)
 try check(a[.type] as? FileAttributeType == .typeRegular,"Nonregular/symlink asset rejected")
 try check(((a[.size] as? NSNumber)?.intValue ?? Int.max) <= limit,"Asset exceeds limit")
 let handle=try FileHandle(forReadingFrom:path);defer{try?handle.close()};var b=Data();while let chunk=try handle.read(upToCount:65536), !chunk.isEmpty {try check(chunk.count<=limit-b.count,"Asset grew beyond limit");b.append(chunk)};return b
}
// Image archive objects are mapped to inert readers: NSImage is never
// instantiated while decoding an import. Only bounded PNG/TIFF encodings are
// inspected with ImageIO metadata (no pixel decode or TIFF expansion).
final class ImageArchive: NSObject, NSSecureCoding {
 static var supportsSecureCoding:Bool {true}
 var blobs:[Data]=[]
 required init?(coder:NSCoder){super.init();var visited=Set<ObjectIdentifier>()
  func collect(_ value:Any?,_ depth:Int){guard depth<32,let value=value else{return}
   if let b=value as? Data {blobs.append(b);return}
   if let image=value as? ImageArchive {blobs+=image.blobs;return}
   if let list=value as? NSArray {let id=ObjectIdentifier(list);if !visited.insert(id).inserted{return};for item in list.prefix(4096){collect(item,depth+1)}}
  }
  for key in ["NSReps","NS.data","NSImageData","NSTIFFRepresentation","NSBitmapImageRep","NSRepresentation","NSRep"] {
   if coder.containsValue(forKey:key){collect(coder.decodeObject(of:[NSArray.self,NSData.self,NSString.self,ImageArchive.self],forKey:key),0)}
  }
 }
 func encode(with coder:NSCoder){coder.encode(blobs,forKey:"NSReps")}
 override init(){super.init()}
 func encoded()throws->(String,Data){var candidates:[(String,Data)]=[]
  for b in blobs {try check(b.count<=32*1024*1024,"Archived image bytes exceed32MiB")
   guard let source=CGImageSourceCreateWithData(b as CFData,[kCGImageSourceShouldCache:false] as CFDictionary),let properties=CGImageSourceCopyPropertiesAtIndex(source,0,[kCGImageSourceShouldCache:false] as CFDictionary) as? [String:Any] else{continue}
   let width=(properties[kCGImagePropertyPixelWidth as String] as? NSNumber)?.uint64Value ?? 0,height=(properties[kCGImagePropertyPixelHeight as String] as? NSNumber)?.uint64Value ?? 0
   try check(width>0&&height>0&&width<=40000000/height,"Archived image exceeds40MP")
   let type=CGImageSourceGetType(source) as String? ?? ""
   try check(type=="public.png"||type=="public.tiff","Unsupported archived image encoding; original unchanged")
   candidates.append((type,b))
  }
  try check(candidates.count==1,"Missing or ambiguous encoded image archive");return candidates[0]
 }
}
final class ClipArchive: NSObject, NSSecureCoding {
 static var supportsSecureCoding: Bool { true }
 let types:[String], string:String, rtf:Data?, pdf:Data?, files:[String], urls:[String]
 var image:ImageArchive?;var fixtureImage:Any?
 init(types:[String],string:String){self.types=types;self.string=string;rtf=nil;pdf=nil;files=[];urls=[];super.init()}
 required init?(coder:NSCoder){
  types=coder.decodeObject(of:[NSArray.self,NSString.self],forKey:"types") as? [String] ?? []
  string=coder.decodeObject(of:NSString.self,forKey:"stringValue") as String? ?? ""
  rtf=coder.decodeObject(of:NSData.self,forKey:"RTFData") as Data?
  pdf=coder.decodeObject(of:NSData.self,forKey:"PDF") as Data?
  files=coder.decodeObject(of:[NSArray.self,NSString.self],forKey:"filenames") as? [String] ?? []
  urls=coder.decodeObject(of:[NSArray.self,NSString.self],forKey:"URL") as? [String] ?? []
  image=coder.decodeObject(of:ImageArchive.self,forKey:"image");super.init()
 }
 func encode(with coder:NSCoder){coder.encode(types,forKey:"types");coder.encode(string,forKey:"stringValue");coder.encode(rtf,forKey:"RTFData");coder.encode(pdf,forKey:"PDF");coder.encode(files,forKey:"filenames");coder.encode(urls,forKey:"URL");coder.encode(fixtureImage,forKey:"image")}
}
func archive(_ b:Data)throws->ClipArchive {
 let u=try NSKeyedUnarchiver(forReadingFrom:b);u.requiresSecureCoding=true;u.decodingFailurePolicy = .setErrorAndReturn
 for name in ["Clipy.CPYClipData","CPYClipData"] {u.setClass(ClipArchive.self,forClassName:name)}
 for name in ["NSImage","NSBitmapImageRep","NSCachedImageRep"] {u.setClass(ImageArchive.self,forClassName:name)}
 guard let value=u.decodeObject(of:ClipArchive.self,forKey:NSKeyedArchiveRootObjectKey) else {throw MigrationError.invalid("Unsupported legacy clip archive")}
 u.finishDecoding();if let error=u.error {throw error};return value
}
func representations(_ a:ClipArchive)throws->[(String,Data)] {
 try check(a.types.count<=4096,"Too many archived types");var parts:[(String,Data)]=[]
 for t in a.types {
  switch t {
  case "NSStringPboardType","public.utf8-plain-text": parts.append(("public.utf8-plain-text",Data(a.string.utf8)))
  case "NSRTFPboardType","public.rtf": guard let b=a.rtf else {throw MigrationError.invalid("Missing RTF")};parts.append(("public.rtf",b))
  case "NSRTFDPboardType","com.apple.flat-rtfd":guard let b=a.rtf else {throw MigrationError.invalid("Missing RTFD")};parts.append(("com.apple.flat-rtfd",b))
  case "NSPDFPboardType","com.adobe.pdf":guard let b=a.pdf else {throw MigrationError.invalid("Missing PDF")};parts.append(("com.adobe.pdf",b))
  case "NSFilenamesPboardType":parts.append((t,try PropertyListSerialization.data(fromPropertyList:a.files,format:.binary,options:0)))
  case "NSURLPboardType","public.url":for url in a.urls {guard let u=URL(string:url) else {throw MigrationError.invalid("Invalid URL")};parts.append(("public.url",u.dataRepresentation))}
  case "NSTIFFPboardType","public.tiff":guard let image=a.image else {throw MigrationError.invalid("Missing image")};parts.append(try image.encoded())
  default:throw MigrationError.invalid("Unsupported legacy representation: \(t); source unchanged")
  }
 }
 try check(!parts.isEmpty,"Missing archived content");return parts
}
final class SQL {
 var db:OpaquePointer?
 init(_ path:String)throws{try check(sqlite3_open(path,&db)==SQLITE_OK,"Cannot open private stage database")}
 deinit{sqlite3_close(db)}
 func exec(_ sql:String)throws{try check(sqlite3_exec(db,sql,nil,nil,nil)==SQLITE_OK,String(cString:sqlite3_errmsg(db)))}
 func insert(_ sql:String,_ values:[Any])throws{
  var s:OpaquePointer?;try check(sqlite3_prepare_v2(db,sql,-1,&s,nil)==SQLITE_OK,"Stage prepare failed");defer{sqlite3_finalize(s)}
  let transient=unsafeBitCast(-1,to:sqlite3_destructor_type.self)
  for (i,v) in values.enumerated(){var rc:Int32=SQLITE_ERROR
   if let b=v as? Data {rc=b.withUnsafeBytes{sqlite3_bind_blob(s,Int32(i+1),$0.baseAddress,Int32(b.count),transient)}}
   else if let n=v as? Int {rc=sqlite3_bind_int64(s,Int32(i+1),Int64(n))}
   else if let str=v as? String {rc=sqlite3_bind_text(s,Int32(i+1),str,-1,transient)}
   try check(rc==SQLITE_OK,"Stage bind failed")
  };try check(sqlite3_step(s)==SQLITE_DONE,"Stage insert failed")
 }
}
func configuration(_ url:URL)->Realm.Configuration {Realm.Configuration(fileURL:url,schemaVersion:7,objectTypes:[CPYClip.self,CPYFolder.self,CPYSnippet.self])}
func fixture(_ root:URL,image:URL?=nil,encoded:Bool=false)throws{
 try FileManager.default.createDirectory(at:root,withIntermediateDirectories:false,attributes:[.posixPermissions:0o700])
 NSKeyedArchiver.setClassName("Clipy.CPYClipData",for:ClipArchive.self)
 let clip=ClipArchive(types:image == nil ? ["NSStringPboardType"]:["NSTIFFPboardType"],string:"Legacy text")
 if let image=image {let b=try read(image);if encoded {let object=ImageArchive();object.blobs=[b];clip.fixtureImage=object}else{clip.fixtureImage=NSImage(data:b);try check(clip.fixtureImage != nil,"Fixture image invalid")}}
 let blob=try NSKeyedArchiver.archivedData(withRootObject:clip,requiringSecureCoding:false)
 try blob.write(to:root.appendingPathComponent("clip.data"),options:.withoutOverwriting)
 let realm=try Realm(configuration:configuration(root.appendingPathComponent("default.realm")))
 try realm.write{
  let clip=CPYClip();clip.dataHash="legacy-original-id";clip.title="Legacy title";clip.dataPath="/old/install/clip.data";clip.primaryType="NSStringPboardType";clip.updateTime=1700000000;realm.add(clip)
  let f=CPYFolder();f.identifier="legacy-folder";f.title="Disabled folder";f.index=7;f.enable=false
  let n=CPYSnippet();n.identifier="legacy-snippet";n.title="Title";n.content="Snippet";n.index=4;n.enable=false;f.snippets.append(n);realm.add(f)
 }
}
func stage(_ source:URL,_ assets:URL,_ output:URL)throws{
 let fm=FileManager.default;let work=fm.temporaryDirectory.appendingPathComponent("cliptiq-legacy-"+UUID().uuidString)
 try fm.createDirectory(at:work,withIntermediateDirectories:false,attributes:[.posixPermissions:0o700]);defer{try?fm.removeItem(at:work)}
 let b=try read(source,limit:1024*1024*1024);let copy=work.appendingPathComponent("source.realm");try b.write(to:copy,options:.withoutOverwriting)
 try check(SHA256.hash(data:try read(source,limit:1024*1024*1024))==SHA256.hash(data:b),"Realm source changed; quit Clipy and retry")
 // New Realm may upgrade its file format; only this private copy is opened.
 try check(try schemaVersionAtURL(copy)==7,"Only legacy Realm schema7 is supported")
 let realm=try Realm(configuration:configuration(copy));let sql=try SQL(output.path)
 try sql.exec("""
 BEGIN;
 CREATE TABLE grdb_migrations(identifier TEXT PRIMARY KEY);INSERT INTO grdb_migrations VALUES('Create initial tables');
 CREATE TABLE pasteboardHistories(id TEXT PRIMARY KEY,title TEXT,pasteboardTypes TEXT,deviceID TEXT,updateAt INTEGER);
 CREATE TABLE pasteboardHistoryAssets(id TEXT PRIMARY KEY,pasteboardHistoryID TEXT,"index" INTEGER,pasteboardType TEXT,data BLOB);
 CREATE TABLE pasteboardHistoryThumbnailAssets(pasteboardHistoryID TEXT PRIMARY KEY,kind TEXT,data BLOB);
 CREATE TABLE snippetFolders(id TEXT PRIMARY KEY,title TEXT,"index" INTEGER,isEnabled INTEGER);
 CREATE TABLE snippets(id TEXT PRIMARY KEY,folderID TEXT,title TEXT,content TEXT,"index" INTEGER,isEnabled INTEGER);
 CREATE TABLE migration_settings(key TEXT PRIMARY KEY,value BLOB,apply_value TEXT);
 """)
 try check(realm.objects(CPYClip.self).count<=100000&&realm.objects(CPYFolder.self).count<=1000&&realm.objects(CPYSnippet.self).count<=10000,"Realm count limit")
 var total=0
 for clip in realm.objects(CPYClip.self){
  let name=URL(fileURLWithPath:clip.dataPath).lastPathComponent;try check(!name.isEmpty&&name != "."&&name != "..","Invalid asset name")
  let raw=try read(assets.appendingPathComponent(name));let decoded=try archive(raw);let parts=try representations(decoded)
  total+=raw.count+parts.reduce(0){$0+$1.1.count};try check(total<=256*1024*1024,"Legacy payload exceeds256MiB")
  let types=try JSONSerialization.data(withJSONObject:parts.map{$0.0});try sql.insert("INSERT INTO pasteboardHistories VALUES(?,?,?,NULL,?)",[clip.dataHash,clip.title,String(decoding:types,as:UTF8.self),clip.updateTime])
  for(i,p)in parts.enumerated(){try sql.insert("INSERT INTO pasteboardHistoryAssets VALUES(?,?,?,?,?)",[clip.dataHash+"-"+String(i),clip.dataHash,i,p.0,p.1])}
  // Preserve the exact archive as inert provenance, not only converted fields.
  try sql.insert("INSERT INTO migration_settings VALUES(?,?,NULL)",["legacy.archive."+clip.dataHash,raw])
  if !clip.thumbnailPath.isEmpty {let name=URL(fileURLWithPath:clip.thumbnailPath).lastPathComponent;let bytes=try read(assets.appendingPathComponent(name));try sql.insert("INSERT INTO pasteboardHistoryThumbnailAssets VALUES(?,?,?)",[clip.dataHash,clip.isColorCode ? "colorCode":"image",bytes])}
 }
 var linked=Set<String>()
 for f in realm.objects(CPYFolder.self){try sql.insert("INSERT INTO snippetFolders VALUES(?,?,?,?)",[f.identifier,f.title,f.index,f.enable ? 1:0]);for n in f.snippets {try check(linked.insert(n.identifier).inserted,"Snippet linked to multiple folders");try sql.insert("INSERT INTO snippets VALUES(?,?,?,?,?,?)",[n.identifier,f.identifier,n.title,n.content,n.index,n.enable ? 1:0])}}
 try check(linked.count==realm.objects(CPYSnippet.self).count,"Orphan Realm snippets")
 try sql.exec("COMMIT")
}
do {
 let args=CommandLine.arguments
 if args.count==3&&args[1]=="--fixture" {try fixture(URL(fileURLWithPath:args[2]))}
 else if args.count==5&&args[1]=="--image-fixture" {try fixture(URL(fileURLWithPath:args[2]),image:URL(fileURLWithPath:args[3]),encoded:args[4]=="encoded")}
 else if args.count==7&&args[1]=="export" {
  let output=URL(fileURLWithPath:args[5]);try check(!FileManager.default.fileExists(atPath:output.path),"Output already exists")
  let work=FileManager.default.temporaryDirectory.appendingPathComponent("cliptiq-stage-"+UUID().uuidString);try FileManager.default.createDirectory(at:work,withIntermediateDirectories:false,attributes:[.posixPermissions:0o700]);defer{try?FileManager.default.removeItem(at:work)}
  let staged=work.appendingPathComponent("stage.sqlite3");try stage(URL(fileURLWithPath:args[2]),URL(fileURLWithPath:args[3]),staged)
  let p=Process();p.executableURL=URL(fileURLWithPath:args[6]);p.arguments=[staged.path,args[4],output.path];try p.run();p.waitUntilExit();try check(p.terminationStatus==0,"Native neutral export failed")
 }else{throw MigrationError.invalid("cliptiq-legacy export SOURCE.realm ASSET_DIRECTORY PREFERENCES.plist OUTPUT.neutral PATH_TO_CLIPTIQ_IMPORT_MACOS")}
}catch{fputs("\(error)\n",stderr);exit(1)}
