"""Build relocatable CI candidates, package them, inspect native closure and budgets.
No application installation, publisher signing or release publication is performed.
"""
import re,argparse,hashlib,importlib.util,io,json,os,pathlib,platform,shutil,struct,subprocess,sys,tarfile,tempfile,zlib
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from distribution import inspect_binary,system_import,validate_distribution
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=pathlib.Path);parser.add_argument('--variant',choices=['base','ocr'],default='base');args=parser.parse_args()
target={'Darwin':'macos','Windows':'windows','Linux':'linux'}[platform.system()];arch={'AMD64':'x86_64','arm64':'aarch64','ARM64':'aarch64'}.get(platform.machine(),platform.machine());build=(args.build or ROOT/'out'/(f"ocr-{platform.system()}-{'native' if target=='macos' else 'tesseract'}" if args.variant=='ocr' else f'core-{platform.system()}-release')).resolve();out=ROOT/'out'/f't17-{target}-{arch}-{args.variant}'
if out.parent.resolve()!=(ROOT/'out').resolve() or out.is_symlink():raise RuntimeError('unsafe output directory')
if out.exists():
 if not (out/'.cliptiq-packaging-owner').is_file():raise RuntimeError('refusing to replace foreign output')
 shutil.rmtree(out)
out.mkdir();(out/'.cliptiq-packaging-owner').write_text('T17 candidate\n');payload=out/'payload';payload.mkdir();prefix=payload/'usr/lib/cliptiq' if target=='linux' else payload
def run(*command,cwd=None):
 try:return subprocess.check_output([str(x) for x in command],cwd=cwd,text=True,stderr=subprocess.STDOUT)
 except subprocess.CalledProcessError as error:
  print(error.output,file=sys.stderr);raise
cmake=os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
if not cmake:raise RuntimeError('cmake required')
print(run(cmake,'--install',build,'--prefix',prefix))
if target=='linux':
 (payload/'usr/bin').mkdir(parents=True);(payload/'usr/bin/cliptiq').symlink_to('../lib/cliptiq/bin/cliptiq')
 desktop=payload/'usr/share/applications';desktop.mkdir(parents=True);shutil.copy2(ROOT/'resources/linux/io.github.hydrock.cliptiq.desktop',desktop)
 binary=prefix/'bin/cliptiq';licenses=payload/'usr/share/cliptiq/licenses'
elif target=='windows':binary=payload/'Cliptiq.exe';licenses=payload/'licenses'
else:binary=payload/'Cliptiq.app/Contents/MacOS/Cliptiq';licenses=payload/'licenses'
licenses.mkdir(parents=True)
for source,name in [('THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),('third_party/libgrapheme/LICENSE','libgrapheme-ISC.txt'),('third_party/libgrapheme/data/LICENSE','Unicode.txt'),('third_party/tinyxml2/LICENSE.txt','TinyXML2-zlib.txt')]:shutil.copy2(ROOT/source,licenses/name)
installed_notice=prefix/'share/cliptiq/THIRD_PARTY_NOTICES.md'
if installed_notice.is_file():installed_notice.unlink()
if args.variant=='ocr' and target!='macos':shutil.copytree(ROOT/'packaging/ocr/licenses',licenses/'ocr');shutil.copy2(ROOT/'packaging/ocr/pins.json',licenses/'ocr/pins.json')
if target=='macos':print(run('codesign','--force','--deep','--sign','-',payload/'Cliptiq.app'));print(run('codesign','--verify','--deep','--strict',payload/'Cliptiq.app'))
print(run(binary,'--version'))
cache=(build/'CMakeCache.txt').read_text(encoding='utf-8');compiler=[line for line in cache.splitlines() if line.startswith(('CMAKE_CXX_COMPILER:','CMAKE_CXX_FLAGS_MINSIZEREL:','CMAKE_MSVC_RUNTIME_LIBRARY:','CMAKE_OSX_DEPLOYMENT_TARGET:','CLIPTIQ_OCR_ENGINE:','ocr_index:'))]
manifest={'schema_version':1,'target':target,'arch':arch,'variant':args.variant,'version':'0.1.0','source_commit':run('git','rev-parse','HEAD',cwd=ROOT).strip(),'source_dirty':bool(run('git','status','--porcelain',cwd=ROOT).strip()),'manual_acceptance':'open','release_ready':False,'signature':'ad-hoc only' if target=='macos' else 'unsigned candidate','project_license':'OPEN: no new project license selected by packaging','clean_machine_dependency_delta_bytes':None,'toolchain':{'cmake':run(cmake,'--version').splitlines()[0],'platform':platform.platform(),'configuration':compiler},'files':[],'symlinks':[],'binaries':[],'system_dependencies':[]}
for p in sorted(payload.rglob('*')):
 if p.is_symlink():manifest['symlinks'].append(dict(path=p.relative_to(payload).as_posix(),target=os.readlink(p)))
 if p.is_file() and not p.is_symlink():
  manifest['files'].append(dict(path=p.relative_to(payload).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
  native=inspect_binary(p)
  if native:
   manifest['binaries'].append(dict(path=p.relative_to(payload).as_posix(),**native))
   for name in native['imports']:
    if system_import(target,name) and name not in manifest['system_dependencies']:manifest['system_dependencies'].append(name)
manifest_path=payload/('usr/share/cliptiq/cliptiq-package.json' if target=='linux' else 'cliptiq-package.json')
manifest_path.parent.mkdir(parents=True,exist_ok=True)
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
errors=validate_distribution(payload,manifest)
if errors:raise RuntimeError('; '.join(errors))
size_tool=ROOT/'tools/size-report.py';zip_path=out/f'cliptiq-0.1.0-{target}-{arch}-{args.variant}.zip'
print(run(sys.executable,size_tool,'--dist',payload,'--archive',zip_path,'--baseline',ROOT/'docs/support/size-baseline.json','--output',out/'size.json'))
packages=[]
if target=='linux':
 tar=out/f'cliptiq-{arch}-{args.variant}.tar.xz'
 with tarfile.open(tar,'w:xz') as archive:
  for p in sorted(payload.iterdir()):archive.add(p,arcname=p.name)
 packages.append(tar)
 # Debian dependencies are generated from actual ELF symbols, not guessed.
 metadata=out/'metadata';(metadata/'debian').mkdir(parents=True);(metadata/'debian/control').write_text('Source: cliptiq\nSection: utils\nPriority: optional\nMaintainer: Cliptiq contributors\n\nPackage: cliptiq\nArchitecture: any\nDescription: Clipboard history candidate\n')
 depends=run('dpkg-shlibdeps','-O','-e'+str(binary),cwd=metadata).strip().split('shlibs:Depends=',1)[1].splitlines()[0]
 with tempfile.TemporaryDirectory(prefix='cliptiq-deb-') as folder:
  staging=pathlib.Path(folder);shutil.copytree(payload,staging,dirs_exist_ok=True,symlinks=True);(staging/'DEBIAN').mkdir();(staging/'DEBIAN/control').write_text(f'Package: cliptiq\nVersion: 0.1.0~t17.{manifest["source_commit"][:7]}\nArchitecture: {"amd64" if arch=="x86_64" else "arm64"}\nMaintainer: Cliptiq contributors\nSection: utils\nPriority: optional\nDepends: {depends}\nDescription: Native clipboard history ({args.variant}), unsigned CI candidate\n')
  staging.chmod(0o755);(staging/'DEBIAN').chmod(0o755);(staging/'DEBIAN/control').chmod(0o644)
  deb=out/f'cliptiq-{arch}-{args.variant}.deb';print(run('dpkg-deb','--root-owner-group','--build',staging,deb));packages.append(deb)
 rpmroot=out/'rpm';rpmroot.mkdir();spec=rpmroot/'cliptiq.spec';spec.write_text(f'''Name: cliptiq
Version: 0.1.0
Release: 0.t17.{manifest['source_commit'][:7]}
Summary: Native clipboard history candidate
License: LicenseRef-Cliptiq-pending
BuildArch: {arch if arch=='x86_64' else 'aarch64'}
AutoReqProv: yes
%global debug_package %{{nil}}
%global _build_id_links none
%global __strip /bin/true
%description
Unsigned CI candidate. Production and manual acceptance are open.
%install
mkdir -p "%{{buildroot}}"
cp -a "{payload}/." "%{{buildroot}}/"
%files
/usr
''')
 print(run('rpmbuild','-bb','--define',f'_topdir {rpmroot}',spec));rpm=next((rpmroot/'RPMS').rglob('*.rpm'));dest=out/rpm.name;shutil.copy2(rpm,dest);packages.append(dest)
 manifest['debian_dependencies']=depends;manifest['rpm_dependencies']=run('rpm','-qp','--requires',dest).splitlines()
 # Extract a real DEB and run its relocated entry point; no host install.
 with tempfile.TemporaryDirectory(prefix='extract-',dir=out) as folder:
  print(run('dpkg-deb','--extract',deb,folder));print(run(pathlib.Path(folder)/'usr/lib/cliptiq/bin/cliptiq','--version'))
else:
 if target=='windows':
  # Full MSIX, semantic validation enabled; deliberately unsigned.
  content=out/'msix-content';shutil.copytree(payload,content);assets=content/'Assets';assets.mkdir()
  def png(size):
   raw=b''.join(b'\0'+b''.join((bytes([40,105,180,255]) if (size//5<=x<size*4//5 and size//5<=y<size*4//5) else bytes([255,255,255,255])) for x in range(size)) for y in range(size))
   chunk=lambda name,data:struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)
   return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')
  for name,size in [('Logo150',150),('Logo44',44),('StoreLogo',50)]:(assets/(name+'.png')).write_bytes(png(size))
  template=(ROOT/'packaging/windows/AppxManifest.xml.in').read_text();(content/'AppxManifest.xml').write_text(template.replace('@ARCH@','x64' if arch=='x86_64' else 'arm64'),encoding='utf-8')
  sdk=pathlib.Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/bin';tools=sorted(sdk.glob('*/x64/makeappx.exe'))
  if not tools:raise RuntimeError('Windows SDK MakeAppx required')
  msix=out/f'cliptiq-{arch}-{args.variant}.msix';print(run(tools[-1],'pack','/o','/d',content,'/p',msix));packages.append(msix)
packages.append(zip_path)
report={'source_commit':manifest['source_commit'],'target':target,'arch':arch,'variant':args.variant,'release_ready':False,'manual_acceptance':'open','package_checks':'native inventory/architecture/dependency validation and relocated --version; Linux DEB extract; Windows MSIX semantic validation','system_dependencies':manifest['system_dependencies'],'debian_dependencies':manifest.get('debian_dependencies'),'rpm_dependencies':manifest.get('rpm_dependencies'),'packages':[{'path':p.name,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in packages]}
(out/'package-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
