"""Fail-closed release gate, separate from passing unsigned candidate CI."""
import argparse,hashlib,json,os,pathlib,platform,re,subprocess,tempfile
from distribution import signature_errors
REQUIRED={f'T{n:02d}' for n in range(4,17)}|{'clean-install','signing','notarization','publisher-identity','project-license','desktop-support-matrix','offline-closure'}
def checklist_errors(data):
 if data.get('schema_version')!=1 or not isinstance(data.get('gates'),list):return ['invalid release checklist']
 gates={};errors=[]
 for row in data['gates']:
  if row.get('id') in gates:errors.append('duplicate release gate')
  gates[row.get('id')]=row.get('status')
 for gate in sorted(REQUIRED):
  if gates.get(gate)!='closed':errors.append(gate+': OPEN or missing')
 return errors
def native_signature(artifact,expected):
 artifact=pathlib.Path(artifact).resolve()
 if platform.system()=='Windows':
  script=pathlib.Path(__file__).resolve().parents[1]/'packaging/windows/verify-signature.ps1'
  result=subprocess.run(['pwsh','-NoProfile','-File',str(script),'-Path',str(artifact)],capture_output=True,text=True,check=True);return signature_errors(json.loads(result.stdout),expected)
 if platform.system()=='Darwin':
  subprocess.run(['codesign','--verify','--deep','--strict',str(artifact)],check=True,capture_output=True)
  subprocess.run(['spctl','--assess','--type','execute',str(artifact)],check=True,capture_output=True)
  subprocess.run(['xcrun','stapler','validate',str(artifact)],check=True,capture_output=True)
  with tempfile.TemporaryDirectory(prefix='cliptiq-cert-') as folder:
   prefix=pathlib.Path(folder)/'cert';subprocess.run(['codesign','--display','--extract-certificates',str(prefix),str(artifact)],check=True,capture_output=True)
   leaf=pathlib.Path(str(prefix)+'0');return signature_errors({'trusted':True,'signer_sha256':hashlib.sha256(leaf.read_bytes()).hexdigest()},expected)
 raise ValueError('Linux delivery needs its authenticated package repository/signature route; candidate artifacts are not publisher-authenticated')
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--checklist',type=pathlib.Path,required=True);parser.add_argument('--expect-blocked',action='store_true');parser.add_argument('--artifact',type=pathlib.Path);parser.add_argument('--signer-sha256',default=os.environ.get('CLIPTIQ_SIGNER_SHA256'));args=parser.parse_args();errors=checklist_errors(json.loads(args.checklist.read_text(encoding='utf-8')))
 if args.expect_blocked:
  if not errors:print('Expected open manual gates, but checklist claims closed');return 1
  print('BLOCKED AS EXPECTED: production release remains unavailable; '+', '.join(errors));return 0
 if errors:print('BLOCKED: '+', '.join(errors));return 1
 if not args.artifact or not args.signer_sha256:print('BLOCKED: actual signed artifact and trusted publisher pin required');return 1
 try:errors=native_signature(args.artifact,args.signer_sha256)
 except (OSError,ValueError,subprocess.SubprocessError) as error:print('BLOCKED: native signature verification failed: '+str(error));return 1
 for error in errors:print('BLOCKED: '+error)
 if not errors:print('PASS native signature and manually accepted gates; no publication performed')
 return int(bool(errors))
if __name__=='__main__':raise SystemExit(main())
