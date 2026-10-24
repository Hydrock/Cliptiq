"""Build paired Base/optional OCR products; run focused backend integration."""
import argparse,hashlib,importlib.machinery,json,os,pathlib,platform,shutil,subprocess,sys,zipfile
root=pathlib.Path(__file__).resolve().parents[1];os.chdir(root)
parser=argparse.ArgumentParser();parser.add_argument('--engine',choices=['native','tesseract'],required=True);parser.add_argument('--reuse-deps',action='store_true',help='Reuse a previously built hash-checked development prefix');args=parser.parse_args()
env=os.environ.copy();cmake='cmake'
if platform.system()=='Windows':
 helper=importlib.machinery.SourceFileLoader('footprint',str(root/'tools/build-footprint.py')).load_module();env,cmake=helper.visual_studio_env()
ctest=str(pathlib.Path(cmake).with_name('ctest.exe')) if platform.system()=='Windows' else 'ctest'
def run(*command):subprocess.run([str(c) for c in command],env=env,check=True)
if args.engine=='tesseract' and not args.reuse_deps:run(sys.executable,'packaging/ocr/build.py')
base=root/('out/ocr-'+platform.system()+'-Base');optional=root/('out/ocr-'+platform.system()+'-'+args.engine);host='cliptiq_menu_host' if platform.system()=='Linux' else 'cliptiq_app';suffix='.exe' if platform.system()=='Windows' else ''
for folder,engine,index in [(base,'none','OFF'),(optional,args.engine,'ON')]:
 run(cmake,'-S',root,'-B',folder,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel','-Docr_index='+index,'-DCLIPTIQ_OCR_ENGINE='+engine)
 targets=[host,'fts_variant_probe'] if engine=='none' else [host,'fts_variant_probe','ocr_test','fts_test','ocr_queue_test','native_ocr_test']
 run(cmake,'--build',folder,'--target',*targets,'--parallel','4')
run(ctest,'--test-dir',optional,'-R','^(ocr|fts|ocr_queue|native_ocr)$','--output-on-failure')
run(sys.executable,'tests/integration/fts_variants.py',base/('fts_variant_probe'+suffix),optional/('fts_variant_probe'+suffix))
def executable(folder):return folder/('Cliptiq.app/Contents/MacOS/Cliptiq' if platform.system()=='Darwin' else ('Cliptiq'+suffix if platform.system()=='Windows' else host))
def inventory(folder):
 path=folder/'Cliptiq.app' if platform.system()=='Darwin' else executable(folder)
 return {p.relative_to(folder).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (path.rglob('*') if path.is_dir() else [path]) if p.is_file()}
report={'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'platform':platform.platform(),'architecture':platform.machine(),'engine':args.engine,'base':inventory(base),'optional':inventory(optional),'models':{},'manual_acceptance':'OPEN; CI is not a clean consumer VM'}
pack=root/'out'/('ocr-pack-'+platform.system()+'-'+args.engine);pack.mkdir(exist_ok=True)
source=optional/'Cliptiq.app' if platform.system()=='Darwin' else executable(optional)
if source.is_dir():shutil.copytree(source,pack/source.name,dirs_exist_ok=True)
else:shutil.copy2(source,pack/source.name)
if args.engine=='tesseract':
 models=root/'.toolchains/ocr'/('install-'+platform.system())/'tessdata';shutil.copytree(models,pack/'tessdata',dirs_exist_ok=True)
 report['models']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in models.glob('*.traineddata')}
 shutil.copytree(root/'packaging/ocr/licenses',pack/'licenses',dirs_exist_ok=True)
 shutil.copy2(root/'packaging/ocr/pins.json',pack/'pins.json')
shutil.copy2(root/'THIRD_PARTY_NOTICES.md',pack/'THIRD_PARTY_NOTICES.md')
archive=root/'out'/('ocr-pack-'+platform.system()+'-'+args.engine+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(pack.rglob('*')):
  if p.is_file():z.write(p,p.relative_to(pack))
report['app_owned_bytes']=sum(p.stat().st_size for p in pack.rglob('*') if p.is_file());report['zip_bytes']=archive.stat().st_size
binary=executable(optional)
command=['ldd',str(binary)] if platform.system()=='Linux' else ['otool','-L',str(binary)] if platform.system()=='Darwin' else ['dumpbin','/DEPENDENTS',str(binary)]
command[0]=shutil.which(command[0],path=env.get('PATH')) or command[0]
report['dynamic_dependencies']=subprocess.check_output(command,env=env,text=True)
report['sources']={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for pattern in ['core/**/*.cpp','core/**/*.hpp','platform/**/*','packaging/ocr/**/*','CMakeLists.txt'] for p in root.glob(pattern) if p.is_file() and '__pycache__' not in p.parts}
(root/('out/t16-ocr-size-'+platform.system()+'-'+args.engine+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:report[k] for k in ['engine','app_owned_bytes','zip_bytes']},indent=2))
