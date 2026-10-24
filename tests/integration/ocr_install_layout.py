"""Verify installed Linux binary/model adjacency and real offline OCR."""
import hashlib,json,pathlib,subprocess,sys,tempfile
build=pathlib.Path(sys.argv[1]).resolve();root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cliptiq-ocr-install-') as directory:
 prefix=pathlib.Path(directory);subprocess.run(['cmake','--install',str(build),'--prefix',str(prefix)],check=True)
 binary=prefix/'bin/cliptiq';assert binary.is_file();models=binary.parent/'tessdata';pins=json.loads((root/'packaging/ocr/pins.json').read_text())
 for name in ['eng','rus']:
  model=models/(name+'.traineddata');assert model.is_file(),str(model)
  assert hashlib.sha256(model.read_bytes()).hexdigest()==pins[name]['sha256']
 subprocess.run([str(build/'native_ocr_test'),str(models),str(root/'tests/fixtures/ocr')],check=True)
print('installed binary finds pinned EN/RU models offline: pass')
