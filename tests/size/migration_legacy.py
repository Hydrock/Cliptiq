"""Stage the actual optional helper closure; debug symbols reported separately."""
import pathlib,sys,json,hashlib,zipfile,io,subprocess,shutil,re,tempfile,os
root=pathlib.Path(sys.argv[1]).resolve();dest=pathlib.Path('out/t14-legacy-package').resolve();dest.mkdir(exist_ok=False)
inputs=[root/'cliptiq-legacy',pathlib.Path('out/core-Darwin-release/tools/import-clipy/cliptiq-import').resolve(),pathlib.Path('out/core-Darwin-release/tools/import-clipy/cliptiq-import-macos').resolve(),*[p for p in root.rglob('*.dylib') if not any(x.endswith('.dSYM') for x in p.parts)]]
files={p.resolve().name:p.resolve() for p in inputs if p.is_file()};debug=sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and any(x.endswith('.dSYM') for x in p.parts))
for name,p in files.items():shutil.copy2(p,dest/name)
for p in dest.iterdir():
 commands=subprocess.check_output(['otool','-l',str(p)],text=True)
 paths=re.findall(r'cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset',commands,re.S)
 # Preserve SDK/system runtime search paths and existing optimized code. Only
 # add a local loader path if SwiftPM did not already emit one.
 if '@loader_path' not in paths and '@executable_path' not in paths:
  subprocess.run(['install_name_tool','-add_rpath','@loader_path',str(p)],check=True)
  subprocess.run(['codesign','--force','--sign','-',str(p)],check=True)
# Verify the original helper first, then the relocated one with the build-tree
# dylib temporarily unavailable. No DYLD tracing changes runtime initialization.
def smoke(executable,fixture):
 r=subprocess.run([str(executable),'--fixture',str(fixture)],capture_output=True,text=True)
 if r.returncode:
  debug=subprocess.run(['lldb','--batch','-o','run','-o','bt','--',str(executable),'--fixture',str(fixture)+'-debug'],capture_output=True,text=True)
  raise AssertionError((str(executable),r.returncode,r.stdout,r.stderr,debug.stdout[-20000:],debug.stderr[-2000:]))
with tempfile.TemporaryDirectory(prefix='cliptiq-helper-relocation-') as t:
 smoke(root/'cliptiq-legacy',pathlib.Path(t)/'control')
 original=root/'libRealmSwift.dylib';hidden=original.with_name('libRealmSwift.dylib.relocation-test')
 original.rename(hidden)
 try:smoke(dest/'cliptiq-legacy',pathlib.Path(t)/'relocated')
 finally:hidden.rename(original)
shutil.copytree(pathlib.Path('tools/import-clipy/licenses'),dest/'licenses')
shutil.copy2('THIRD_PARTY_NOTICES.md',dest/'THIRD_PARTY_NOTICES.md')
rows=[];archive=io.BytesIO()
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(x for x in dest.rglob('*') if x.is_file()):
  b=p.read_bytes();item=zipfile.ZipInfo(p.relative_to(dest).as_posix(),date_time=(2026,1,1,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED;z.writestr(item,b);rows.append({'path':p.relative_to(dest).as_posix(),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'linked':subprocess.check_output(['otool','-L',str(p)],text=True).splitlines()[1:] if p.name in files else []})
report={'scope':'complete optional macOS migration lane: C++ converter, native preferences tool, Realm10.54.6 helper+local dylib, release binaries with debug symbols excluded; real execution verified local dylib load; system AppKit/Foundation/Swift excluded; not Developer-ID/notarized installer','files':rows,'total_bytes':sum(r['bytes'] for r in rows),'zip_bytes':len(archive.getvalue()),'build_debug_symbol_bytes_excluded':debug,'relocation_fixture_passed':True}
pathlib.Path('out/t14-legacy-size.json').write_text(json.dumps(report,indent=2)+'\n');print(report['total_bytes'],report['zip_bytes'])
