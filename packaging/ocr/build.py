"""Fetch/build the pinned offline OCR closure. No app/runtime downloads."""
import argparse, hashlib, json, os, pathlib, platform, subprocess, tarfile, urllib.request
root=pathlib.Path(__file__).resolve().parents[2]
pins=json.loads((root/'packaging/ocr/pins.json').read_text())
parser=argparse.ArgumentParser();parser.add_argument('--fetch-only',action='store_true');args=parser.parse_args()
work=root/'.toolchains/ocr';work.mkdir(parents=True,exist_ok=True)
for name,pin in pins.items():
 archive=work/(name+('.tar.gz' if name in ('tesseract','leptonica') else '.traineddata'))
 if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:
  data=urllib.request.urlopen(pin['url'],timeout=120).read()
  if hashlib.sha256(data).hexdigest()!=pin['sha256']:raise RuntimeError('OCR dependency hash mismatch: '+name)
  archive.write_bytes(data)
 if name in ('tesseract','leptonica'):
  # Fresh extraction overwrites source files from the verified archive.
  with tarfile.open(archive) as source:source.extractall(work,filter='data')
if args.fetch_only:raise SystemExit(0)
prefix=work/('install-'+platform.system());env=os.environ.copy();cmake='cmake'
if platform.system()=='Windows':
 import sys
 sys.path.insert(0,str(root/'tools'));from importlib.machinery import SourceFileLoader
 helper=SourceFileLoader('footprint',str(root/'tools/build-footprint.py')).load_module();env,cmake=helper.visual_studio_env()
def run(*cmd):subprocess.run([str(c) for c in cmd],env=env,check=True)
common=['-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel','-DBUILD_SHARED_LIBS=OFF','-DSW_BUILD=OFF','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_INSTALL_LIBDIR=lib','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded']
lept=work/('build-lept-'+platform.system());tess=work/('build-tess-'+platform.system())
run(cmake,'-S',work/'leptonica-1.85.0','-B',lept,*common,'-DBUILD_PROG=OFF',*['-DENABLE_'+x+'=OFF' for x in ['ZLIB','PNG','GIF','JPEG','TIFF','WEBP','OPENJPEG']])
run(cmake,'--build',lept,'--parallel','4');run(cmake,'--install',lept)
run(cmake,'-S',work/'tesseract-5.5.1','-B',tess,*common,'-DCMAKE_PREFIX_PATH='+str(prefix),'-DBUILD_TRAINING_TOOLS=OFF','-DBUILD_TESTS=OFF','-DGRAPHICS_DISABLED=ON','-DDISABLED_LEGACY_ENGINE=ON','-DDISABLE_TIFF=ON','-DDISABLE_ARCHIVE=ON','-DDISABLE_CURL=ON','-DOPENMP_BUILD=OFF','-DENABLE_NATIVE=OFF','-DINSTALL_CONFIGS=OFF','-DWIN32_MT_BUILD=ON')
run(cmake,'--build',tess,'--parallel','4');run(cmake,'--install',tess)
models=prefix/'tessdata';models.mkdir(exist_ok=True)
for name in ('eng','rus'):(models/(name+'.traineddata')).write_bytes((work/(name+'.traineddata')).read_bytes())
manifest={'pins':pins,'files':{p.relative_to(prefix).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in prefix.rglob('*') if p.is_file()}}
(prefix/'closure.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('OCR development prefix:',prefix)
