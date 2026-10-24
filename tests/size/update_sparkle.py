"""Measure the actual application + complete optional pinned Sparkle closure."""
from pathlib import Path
import subprocess,urllib.request,hashlib,tarfile,shutil,json,platform,base64
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
# Public RFC8032 fixture keys, never production identities, no Keychain access.
key=out/'t15-fixture-key';wrong=out/'t15-fixture-wrong-key';payload=out/'t15-fixture-payload.bin'
key.write_bytes(base64.b64encode(bytes.fromhex('9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a')))
wrong.write_bytes(base64.b64encode(bytes.fromhex('4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c')))
key.chmod(0o600);wrong.chmod(0o600);payload.write_bytes(b'Cliptiq synthetic update payload')
sign=sdk/'bin/sign_update'
signature=run([sign,'--ed-key-file',key,'-p',payload]).strip()
run([sign,'--ed-key-file',key,'--verify',payload,signature])
for identity,sig in [(wrong,signature),(key,'invalid'),(key,'')]:
 r=subprocess.run([str(sign),'--ed-key-file',str(identity),'--verify',str(payload),sig],capture_output=True,text=True)
 assert r.returncode!=0,'SDK accepted invalid/missing signature or wrong signing identity'
payload.write_bytes(b'tampered')
r=subprocess.run([str(sign),'--ed-key-file',str(key),'--verify',str(payload),signature],capture_output=True,text=True)
assert r.returncode!=0,'SDK accepted tampered bytes'
log.write('Sparkle real Ed25519 verifier: correct/wrong identity, missing/malformed signature, tampered payload passed\n');log.flush()
key.unlink();wrong.unlink();payload.unlink()
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
