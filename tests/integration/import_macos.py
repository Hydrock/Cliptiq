import pathlib,subprocess,tempfile,sqlite3,sys,plistlib,json,hashlib,shutil,zlib,struct
cli,native=sys.argv[1:3];legacy=sys.argv[3] if len(sys.argv)>3 else None
root=pathlib.Path(tempfile.mkdtemp(prefix='cliptiq-migration-native-'))
def run(args,ok=True):
 r=subprocess.run(list(map(str,args)),capture_output=True,text=True);
 if r.stderr:print(r.stderr)
 assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
def archive(classname,fields):
 objects=['$null',{}, {'$classname':classname,'$classes':[classname,'NSObject']}]
 fields=dict(fields);fields['$class']=plistlib.UID(2)
 for k,v in list(fields.items()):
  if isinstance(v,str):fields[k]=plistlib.UID(len(objects));objects.append(v)
 objects[1]=fields
 return plistlib.dumps({'$archiver':'NSKeyedArchiver','$version':100000,'$objects':objects,'$top':{'root':plistlib.UID(1)}},fmt=plistlib.FMT_BINARY)
def snapshot(p):return {x.relative_to(p).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in p.rglob('*') if x.is_file()}
try:
 prefs=root/'prefs.plist';prefs.write_bytes(plistlib.dumps({'kCPYPrefMaxHistorySizeKey':50,'kCPYMigrateNewKeyCombo':True,'kCPYHotKeyMainKeyCombo':archive('Magnet.KeyCombo',{'key':'b','modifiers':4096|512,'doubledModifiers':False}),'kCPYPrefStoreTypesKey':{'HTML':False,'String':True},'excludedApplications':json.dumps([{'identifier':'org.example.Secret','name':'Secret'}]).encode(),'unknown.preserved':b'original opaque bytes','folderKeyCombos':json.dumps({'legacy-folder':{'key':'j','modifiers':4096|512,'doubledModifiers':False}}).encode()}))
 if legacy:
  source=root/'legacy';run([legacy,'--fixture',source]);before=snapshot(source);neutral=root/'legacy.neutral';run([legacy,'export',source/'default.realm',source,prefs,neutral,native]);assert snapshot(source)==before
  dest=root/'legacy.dest';run([cli,'import',neutral,dest,'--apply-settings']);d=sqlite3.connect(dest)
  assert d.execute('SELECT data FROM representations').fetchone()==(b'Legacy text',)
  assert d.execute('SELECT enabled FROM snippet_folder_states').fetchone()==(0,)
  assert d.execute("SELECT value FROM settings WHERE key='t09.hotkey:folder:legacy-folder'").fetchone()==('Ctrl+Shift+J',)
  assert d.execute("SELECT count(*) FROM migration_originals WHERE category='settings' AND id LIKE 'legacy.archive.%'").fetchone()==(1,)
  d.close()
  def png(width,height):
   def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
   return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+b'\xff\0\0\xff'*2+b'\0'+b'\0\xff\0\xff'*2))+chunk(b'IEND',b'')
  small=root/'small.png';small.write_bytes(png(2,2));image_source=root/'image-source';run([legacy,'--image-fixture',image_source,small,'real']);image_before=snapshot(image_source);run([legacy,'export',image_source/'default.realm',image_source,prefs,root/'image.neutral',native]);assert snapshot(image_source)==image_before
  huge=root/'huge.png';huge.write_bytes(png(16000,16000));huge_source=root/'huge-source';run([legacy,'--image-fixture',huge_source,huge,'encoded']);image_before=snapshot(huge_source);run([legacy,'export',huge_source/'default.realm',huge_source,prefs,root/'huge.neutral',native],False);assert snapshot(huge_source)==image_before and not (root/'huge.neutral').exists()
  (source/'clip.data').unlink();run([legacy,'export',source/'default.realm',source,prefs,root/'missing.neutral',native],False);assert not (root/'missing.neutral').exists()
 else:
  # Reuse the portable synthetic source generator, without running its suite.
  code=pathlib.Path(__file__).with_name('import_fixtures.py').read_text();scope={'sys':sys};prefix=code[:code.index('try:\n for v in range')];exec(prefix.replace('exe,test=sys.argv[1:]','exe,test=sys.argv[1:3]'),scope)
  huge_prefs=root/'huge-prefs.plist'
  with huge_prefs.open('wb') as file:file.truncate(192*1024*1024)
  bounded=subprocess.run([native,'unused-source.sqlite3',str(huge_prefs),str(root/'huge-prefs.neutral')],capture_output=True,text=True)
  assert bounded.returncode!=0 and 'before read' in bounded.stderr and not (root/'huge-prefs.neutral').exists(),bounded.stderr
  p=root/'source.sqlite3';scope['fixture'](p,4);before=p.read_bytes();neutral=root/'native.neutral';run([native,p,prefs,neutral]);assert p.read_bytes()==before
  dest=root/'native.dest';run([cli,'import',neutral,dest,'--apply-settings']);d=sqlite3.connect(dest)
  assert d.execute("SELECT value FROM settings WHERE key='t09.history_count'").fetchone()==('50',)
  actual=d.execute("SELECT value FROM settings WHERE key='t09.hotkey:main'").fetchone();assert actual==('Ctrl+Shift+B',),actual
  assert d.execute("SELECT value FROM settings WHERE key='t09.hotkey:history'").fetchone()==('',)
  assert d.execute("SELECT value FROM settings WHERE key='t09.excluded'").fetchone()==('org.example.Secret',)
  assert d.execute("SELECT data FROM migration_originals WHERE category='settings' AND id='source.preferences'").fetchone()==(prefs.read_bytes(),)
  d.close();shutil.rmtree(scope['root'])
 print('PASS native preferences/archive allowlist, nil-disabled shortcut, exclusions/types, source preservation'+(' and real Realm schema7 file conversion' if legacy else ''))
finally:shutil.rmtree(root)
