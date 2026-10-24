"""Stage the actual optional helper closure; debug symbols reported separately."""
import pathlib,sys,json,hashlib,zipfile,io,subprocess,shutil,re,tempfile,os
root=pathlib.Path(sys.argv[1]).resolve();dest=pathlib.Path('out/t14-legacy-package').resolve();dest.mkdir(exist_ok=False)
inputs=[root/'cliptiq-legacy',*[p for p in root.rglob('*.dylib') if not any(x.endswith('.dSYM') for x in p.parts)]]
files={p.resolve().name:p.resolve() for p in inputs if p.is_file()};debug=sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and any(x.endswith('.dSYM') for x in p.parts))
for name,p in files.items():shutil.copy2(p,dest/name)
for p in dest.iterdir():
 subprocess.run(['strip','-S',str(p)],check=True)
 commands=subprocess.check_output(['otool','-l',str(p)],text=True)
 for path in re.findall(r'cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset',commands,re.S):subprocess.run(['install_name_tool','-delete_rpath',path,str(p)],check=True)
 subprocess.run(['install_name_tool','-add_rpath','@loader_path',str(p)],check=True)
 for line in subprocess.check_output(['otool','-L',str(p)],text=True).splitlines()[1:]:
  dependency=line.strip().split(' (')[0];name=pathlib.Path(dependency).name
  if name in files and dependency!='@rpath/'+name:subprocess.run(['install_name_tool','-change',dependency,'@rpath/'+name,str(p)],check=True)
 subprocess.run(['codesign','--force','--sign','-',str(p)],check=True)
# A relocated executable must load the local Realm dylib, not a build-tree copy.
with tempfile.TemporaryDirectory(prefix='cliptiq-helper-relocation-') as t:
 environment=os.environ.copy();environment['DYLD_PRINT_LIBRARIES']='1';environment.pop('DYLD_LIBRARY_PATH',None)
 r=subprocess.run([str(dest/'cliptiq-legacy'),'--fixture',str(pathlib.Path(t)/'fixture')],env=environment,capture_output=True,text=True);assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
 assert str(dest/'libRealmSwift.dylib') in r.stderr,r.stderr
 assert str(root/'libRealmSwift.dylib') not in r.stderr,r.stderr
rows=[];archive=io.BytesIO()
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(dest.iterdir()):
  b=p.read_bytes();item=zipfile.ZipInfo(p.name,date_time=(2026,1,1,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED;z.writestr(item,b);rows.append({'path':p.name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'linked':subprocess.check_output(['otool','-L',str(p)],text=True).splitlines()[1:]})
report={'scope':'optional relocated macOS Realm10.54.6 helper+local dylib, stripped and ad-hoc signed; real execution verified local dylib load; system AppKit/Foundation/Swift excluded; not Developer-ID/notarized installer','files':rows,'total_bytes':sum(r['bytes'] for r in rows),'zip_bytes':len(archive.getvalue()),'build_debug_symbol_bytes_excluded':debug,'relocation_fixture_passed':True}
pathlib.Path('out/t14-legacy-size.json').write_text(json.dumps(report,indent=2)+'\n');print(report['total_bytes'],report['zip_bytes'])
