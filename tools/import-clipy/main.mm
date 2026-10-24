#include "cliptiq/hotkeys.hpp"
#include "cliptiq/migration.hpp"
#import <Foundation/Foundation.h>
#include <filesystem>
#include <fstream>
#include <iostream>
// Only inert fields from explicitly mapped legacy classes are decoded. No
// upstream NSCoding implementation or arbitrary class is instantiated.
@interface CliptiqArchiveValue : NSObject <NSSecureCoding>
@property(nonatomic, strong) NSDictionary *fields;
@end
@implementation CliptiqArchiveValue
+ (BOOL)supportsSecureCoding {
  return YES;
}
- (instancetype)initWithCoder:(NSCoder *)c {
  if ((self = [super init])) {
    NSMutableDictionary *d = [NSMutableDictionary dictionary];
    NSSet *classes =
        [NSSet setWithObjects:NSString.class, NSNumber.class, NSArray.class,
                              NSDictionary.class, NSData.class, NSNull.class,
                              CliptiqArchiveValue.class, nil];
    for (NSString *k in @[ @"key", @"identifier", @"name" ]) {
      if ([c containsValueForKey:k]) {
        id v = [c decodeObjectOfClasses:classes forKey:k];
        if (v)
          d[k] = v;
      }
    }
    for (NSString *k in @[ @"modifiers", @"QWERTYKeyCode", @"keyCode" ]) {
      if ([c containsValueForKey:k])
        d[k] = @([c decodeIntegerForKey:k]);
    }
    if ([c containsValueForKey:@"doubledModifiers"])
      d[@"doubledModifiers"] = @([c decodeBoolForKey:@"doubledModifiers"]);
    self.fields = d;
  }
  return self;
}
- (void)encodeWithCoder:(NSCoder *)c {
  for (NSString *k in self.fields)
    [c encodeObject:self.fields[k] forKey:k];
}
@end
static id decode(id value) {
  if (![value isKindOfClass:NSData.class])
    return value;
  NSData *b = value;
  NSError *error = nil;
  id json =
      [NSJSONSerialization JSONObjectWithData:b
                                      options:NSJSONReadingFragmentsAllowed
                                        error:&error];
  if (json)
    return json;
  @try {
    NSKeyedUnarchiver *u =
        [[NSKeyedUnarchiver alloc] initForReadingFromData:b error:&error];
    if (!u)
      return nil;
    u.requiresSecureCoding = YES;
    u.decodingFailurePolicy = NSDecodingFailurePolicySetErrorAndReturn;
    for (NSString *n in @[
           @"Magnet.KeyCombo", @"KeyCombo", @"Clipy.CPYAppInfo",
           @"Clipy.ApplicationInformation"
         ])
      [u setClass:CliptiqArchiveValue.class forClassName:n];
    NSSet *classes =
        [NSSet setWithObjects:NSString.class, NSNumber.class, NSArray.class,
                              NSDictionary.class, NSData.class, NSNull.class,
                              CliptiqArchiveValue.class, nil];
    id result = [u decodeObjectOfClasses:classes
                                  forKey:NSKeyedArchiveRootObjectKey];
    [u finishDecoding];
    if (u.error)
      std::cerr << "Legacy settings archive decode: "
                << u.error.localizedDescription.UTF8String << '\n';
    return u.error ? nil : result;
  } @catch (NSException *e) {
    std::cerr << "Legacy settings archive rejected: " << e.reason.UTF8String
              << '\n';
    return nil;
  }
}
static NSDictionary *dict(id x) {
  if ([x isKindOfClass:CliptiqArchiveValue.class])
    return ((CliptiqArchiveValue *)x).fields;
  return [x isKindOfClass:NSDictionary.class] ? x : nil;
}
static std::optional<std::string> chord(id value) {
  id x = decode(value);
  if (x == NSNull.null)
    return std::string();
  NSDictionary *d = dict(x);
  if (!d)
    return {};
  if ([d[@"doubledModifiers"] boolValue])
    return {};
  NSString *k = d[@"key"];
  if (![k isKindOfClass:NSString.class])
    return {};
  unsigned m = [d[@"modifiers"] unsignedIntValue];
  if (m & ~(256u | 512u | 2048u | 4096u))
    return {};
  std::string out;
  if (m & 4096)
    out += "Ctrl+";
  if (m & 512)
    out += "Shift+";
  if (m & 2048)
    out += "Alt+";
  if (m & 256)
    out += "Super+";
  out += k.uppercaseString.UTF8String;
  auto parsed = cliptiq::parse_chord(out);
  return parsed ? std::optional<std::string>(out) : std::nullopt;
}
static std::vector<cliptiq::MigrationSetting> preferences(NSString *path) {
  NSURL *u = [NSURL fileURLWithPath:path];
  NSNumber *symlink = nil;
  [u getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:nil];
  if (symlink.boolValue)
    throw std::runtime_error("Preferences symlink rejected");
  auto file = std::filesystem::u8path(path.UTF8String);
  if (!std::filesystem::is_regular_file(
          std::filesystem::symlink_status(file)) ||
      std::filesystem::file_size(file) > 16 * 1024 * 1024)
    throw std::runtime_error(
        "Preferences exceeds16MiB or is not a regular file (before read)");
  std::ifstream input(file, std::ios::binary);
  std::vector<char> bytes;
  char buffer[65536];
  while (input) {
    input.read(buffer, sizeof(buffer));
    auto n = input.gcount();
    if (bytes.size() + std::size_t(n) > 16 * 1024 * 1024)
      throw std::runtime_error("Preferences grew beyond16MiB");
    bytes.insert(bytes.end(), buffer, buffer + n);
  }
  if (!input.eof())
    throw std::runtime_error("Preferences read failed");
  NSData *b = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  NSDictionary *p =
      [NSPropertyListSerialization propertyListWithData:b
                                                options:NSPropertyListImmutable
                                                 format:nil
                                                  error:nil];
  if (![p isKindOfClass:NSDictionary.class])
    throw std::runtime_error("Expected preferences property list dictionary");
  std::vector<cliptiq::MigrationSetting> rows;
  auto add = [&](NSString *key, id raw, std::optional<std::string> apply) {
    NSData *d = [NSPropertyListSerialization
        dataWithPropertyList:@{@"value" : raw ?: NSNull.null}
                      format:NSPropertyListBinaryFormat_v1_0
                     options:0
                       error:nil];
    if (!d && [raw isKindOfClass:NSData.class])
      d = raw;
    cliptiq::Bytes bytes;
    if (d)
      bytes.assign((const uint8_t *)d.bytes,
                   (const uint8_t *)d.bytes + d.length);
    rows.push_back({key.UTF8String, std::move(bytes), apply});
  };
  rows.push_back({"source.preferences",
                  cliptiq::Bytes((const uint8_t *)b.bytes,
                                 (const uint8_t *)b.bytes + b.length),
                  {}});
  NSDictionary *map = @{
    @"kCPYPrefMaxHistorySizeKey" : @"history_count",
    @"kCPYPrefInputPasteCommandKey" : @"auto_paste",
    @"kCPYPrefCopySameHistroy" : @"duplicates",
    @"kCPYPrefOverwriteSameHistroy" : @"replace_duplicates",
    @"kCPYPrefShowAlertBeforeClearHistoryKey" : @"confirm_clear",
    @"clearsHistoryOnQuit" : @"clear_quit",
    @"clearsHistoryPeriodically" : @"periodic",
    @"historyClearInterval" : @"cleanup_seconds",
    @"kCPYPrefNumberOfItemsPlaceInlineKey" : @"inline_count",
    @"kCPYPrefNumberOfItemsPlaceInsideFolderKey" : @"folder_count",
    @"kCPYPrefMaxMenuItemTitleLengthKey" : @"title_characters",
    @"showsSnippetsAboveHistory" : @"snippets_above",
    @"menuItemsAreMarkedWithNumbers" : @"numbered",
    @"kCPYPrefMenuItemsTitleStartWithZeroKey" : @"zero_start",
    @"addNumericKeyEquivalents" : @"numeric_selection",
    @"kCPYPrefShowIconInTheMenuKey" : @"icons",
    @"showToolTipOnMenuItem" : @"tooltips",
    @"maxLengthOfToolTipKey" : @"tooltip_characters",
    @"showImageInTheMenu" : @"image_previews",
    @"thumbnailWidth" : @"preview_width",
    @"thumbnailHeight" : @"preview_height",
    @"kCPYPrefShowColorPreviewInTheMenu" : @"hex_swatches",
    @"kCPYPrefAddClearHistoryMenuItemKey" : @"show_clear",
    @"kCPYPrefIgnoreConcealedPasteboardType" : @"concealed",
    @"ignoresUniversalClipboard" : @"remote_macos"
  };
  for (NSString *k in map)
    if ([p[k] isKindOfClass:NSNumber.class])
      add(map[k], p[k], std::string([p[k] stringValue].UTF8String));
  if ([p[@"kCPYPrefReorderClipsAfterPasting"] isKindOfClass:NSNumber.class])
    add(@"sort", p[@"kCPYPrefReorderClipsAfterPasting"],
        [p[@"kCPYPrefReorderClipsAfterPasting"] boolValue] ? "1" : "0");
  NSDictionary *actions = @{
    @"mainKeyCombo" : @"main",
    @"historyKeyCombo" : @"history",
    @"snippetKeyCombo" : @"snippets",
    @"settingsKeyCombo" : @"settings",
    @"editSnippetsKeyCombo" : @"editor",
    @"clearHistoryKeyCombo" : @"clear"
  };
  NSDictionary *old = @{
    @"mainKeyCombo" : @"kCPYHotKeyMainKeyCombo",
    @"historyKeyCombo" : @"kCPYHotKeyHistoryKeyCombo",
    @"snippetKeyCombo" : @"kCPYHotKeySnippetKeyCombo",
    @"clearHistoryKeyCombo" : @"kCPYClearHistoryKeyCombo"
  };
  for (NSString *k in actions) {
    id raw = p[k] ?: p[old[k] ?: @""];
    auto value = chord(raw);
    if (!raw && [p[@"kCPYMigrateNewKeyCombo"] boolValue] &&
        ([k isEqual:@"mainKeyCombo"] || [k isEqual:@"historyKeyCombo"] ||
         [k isEqual:@"snippetKeyCombo"]))
      value = std::string();
    if (raw || value) {
      add([@"hotkey:" stringByAppendingString:actions[k]], raw, value);
      if (!value)
        std::cerr << "Shortcut preserved but requires manual reassignment: "
                  << k.UTF8String << '\n';
    }
  }
  id folders = decode(p[@"folderKeyCombos"] ?: p[@"kCPYFolderKeyCombos"]);
  for (NSString *k in dict(folders)) {
    auto v = chord(dict(folders)[k]);
    add([@"hotkey:folder:" stringByAppendingString:k], dict(folders)[k], v);
  }
  id excluded =
      decode(p[@"excludedApplications"] ?: p[@"kCPYExcludeApplications"]);
  if ([excluded isKindOfClass:NSArray.class]) {
    std::string ids;
    for (id item in excluded) {
      NSString *ident = dict(item)[@"identifier"];
      if ([ident isKindOfClass:NSString.class]) {
        if (!ids.empty())
          ids += '\n';
        ids += ident.UTF8String;
      }
    }
    add(@"excluded",
        p[@"excludedApplications"] ?: p[@"kCPYExcludeApplications"], ids);
  }
  NSDictionary *types = dict(decode(p[@"pasteboardTypeSettings"]));
  types = dict(types[@"values"]) ?: types;
  types = types ?: dict(p[@"kCPYPrefStoreTypesKey"]);
  NSDictionary *typeMap = @{
    @"String" : @"type_text",
    @"RTF" : @"type_rtf",
    @"RTFD" : @"type_rtfd",
    @"HTML" : @"type_html",
    @"PDF" : @"type_pdf",
    @"Filenames" : @"type_files",
    @"URL" : @"type_url",
    @"TIFF" : @"type_image"
  };
  for (NSString *k in typeMap)
    if ([types[k] isKindOfClass:NSNumber.class])
      add(typeMap[k], types[k], [types[k] boolValue] ? "1" : "0");
  return rows;
}
int main(int argc, char **argv) {
  @autoreleasepool {
    @try {
      try {
        if (argc != 4) {
          std::cerr << "cliptiq-import-macos SOURCE.sqlite3 PREFERENCES.plist "
                       "OUTPUT.neutral\n";
          return 2;
        }
        auto rows = preferences([NSString stringWithUTF8String:argv[2]]);
        auto r = cliptiq::export_clipy_sqlite(argv[1], argv[3], rows);
        if (!r)
          throw std::runtime_error(r.error().message);
        return 0;
      } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
      }
    } @catch (NSException *e) {
      std::cerr << e.reason.UTF8String << '\n';
      return 1;
    }
  }
}
