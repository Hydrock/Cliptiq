"""Measure the actual application + complete optional pinned Sparkle closure."""
from pathlib import Path
import subprocess,urllib.request,hashlib,tarfile,shutil,json,platform
root=Path(__file__).resolve().parents[2];out=root/'out';out.mkdir(exist_ok=True)
log=(out/'t15-sparkle-test.log').open('w')
def run(args):
 args=list(map(str,args));r=subprocess.run(args,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log.write('$ '+' '.join(args)+'\n'+r.stdout);log.flush()
 if r.returncode:raise RuntimeError(r.stdout[-5000:])
 return r.stdout
archive=out/'t15-Sparkle.tar.xz'
urllib.request.urlretrieve('https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz',archive)
digest='c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==digest
sdk=out/'t15-sparkle-sdk';sdk.mkdir(exist_ok=True)
# Pinned official SDK includes intentional framework symlinks; never unpack an
# unverified user archive using this policy.
with tarfile.open(archive) as t:t.extractall(sdk,filter='fully_trusted')
framework=next(sdk.rglob('Sparkle.framework'));build=out/'core-Darwin-sparkle'
run(['cmake','-S',root,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel','-DBUILD_TESTING=ON',f'-DCLIPTIQ_SPARKLE_FRAMEWORK={framework}'])
run(['cmake','--build',build,'--target','cliptiq_app','update_test','update_native_test','--parallel','3'])
run(['ctest','--test-dir',build,'-R','^update','--output-on-failure'])
app=build/'Cliptiq.app';target=app/'Contents/Frameworks/Sparkle.framework'
resources=app/'Contents/Resources';resources.mkdir(exist_ok=True);shutil.copy2(sdk/'LICENSE',resources/'Sparkle-LICENSE.txt')
# Retain all XPC/installer/autoupdate helpers and localizations. Only CPU slices
# differ between architecture-specific deliveries; no hidden helper downloads.
arch=platform.machine()
for p in target.rglob('*'):
 if p.is_file() and not p.is_symlink() and 'Mach-O' in run(['file',p]):
  if len(run(['lipo','-archs',p]).split())>1:
   temp=p.with_name(p.name+'.thin');run(['lipo',p,'-thin',arch,'-output',temp]);shutil.copymode(p,temp);temp.replace(p)
for p in sorted(target.rglob('*'),key=lambda p:len(p.parts),reverse=True):
 if p.is_file() and not p.is_symlink() and 'Mach-O' in run(['file',p]):run(['codesign','--force','--sign','-',p])
for p in sorted(target.rglob('*'),key=lambda p:len(p.parts),reverse=True):
 if not p.is_symlink() and p.suffix in ('.app','.xpc'):run(['codesign','--force','--sign','-',p])
run(['codesign','--force','--sign','-',target]);run(['codesign','--force','--sign','-',app]);run(['codesign','--verify','--deep','--strict',app])
# Prove relocation without the build-tree SDK; --about exits before any updater,
# clipboard, single-instance, hotkey, network or input registration.
hidden=framework.with_name('Sparkle.build-tree-unavailable');framework.rename(hidden)
try:run([app/'Contents/MacOS/Cliptiq','--about'])
finally:hidden.rename(framework)
packed=out/'t15-sparkle-app.zip';run(['ditto','-c','-k','--keepParent',app,packed])
files={p.relative_to(app).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(app.rglob('*')) if p.is_file() and not p.is_symlink()}
report={'source_commit':run(['git','rev-parse','HEAD']).strip(),'source_dirty':bool(run(['git','status','--porcelain']).strip()),'arch':arch,'sparkle_version':'2.10.0','sdk_sha256':digest,'app_owned_bytes':sum(v['bytes'] for v in files.values()),'zip_bytes':packed.stat().st_size,'files':files,'relocation':'passed, build-tree SDK unavailable','signing':'ad hoc only; no Developer ID/notarization/production feed identity','scope':'actual app bundle plus complete Sparkle helpers/resources/license; no installer/system frameworks; no production update was installed'}
(out/'t15-sparkle-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
