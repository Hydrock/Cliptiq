import pathlib,sys,json,hashlib,zipfile,io,subprocess
root=pathlib.Path(sys.argv[1]).resolve();files={}
for p in [root/'cliptiq-legacy',*root.rglob('*.dylib')]:
 if p.is_file():files[str(p.resolve())]=p
rows=[];archive=io.BytesIO()
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in files.values():
  b=p.read_bytes();name=p.relative_to(root).as_posix();z.writestr(name,b);rows.append({'path':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'linked':subprocess.check_output(['otool','-L',str(p)],text=True).splitlines()[1:]})
report={'scope':'optional macOS Realm10.54.6 helper and built local dylibs; build-tree measured closure, not app runtime, not signed installer; system AppKit/Foundation/Swift and OS libraries not bundled','files':rows,'total_bytes':sum(r['bytes'] for r in rows),'zip_bytes':len(archive.getvalue())}
pathlib.Path('out/t14-legacy-size.json').write_text(json.dumps(report,indent=2)+'\n');print(report['total_bytes'],report['zip_bytes'])
