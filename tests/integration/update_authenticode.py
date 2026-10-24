"""Real Windows trust-provider tests; never execute the inspected file."""
import pathlib,subprocess,sys,tempfile,os
exe=sys.argv[1]
def run(path,pin):return subprocess.run([exe,'--verify',str(path),pin],capture_output=True,text=True)
with tempfile.TemporaryDirectory(prefix='cliptiq-update-') as directory:
 p=pathlib.Path(directory)/'unsigned.exe';p.write_bytes(b'not an executable')
 assert run(p,'0'*64).returncode==1
 assert run(p,'wrong').returncode==2
 # Embedded Microsoft CRT, NOT a Cliptiq release identity.
 roots=[pathlib.Path(os.environ['SystemRoot'])/'System32',pathlib.Path(os.environ.get('VCToolsRedistDir','missing'))]
 candidate=None
 for root in roots:
  for f in root.glob('**/vcruntime140.dll'):
   if f.stat().st_size<5*1024*1024:
    command="$s=Get-AuthenticodeSignature -LiteralPath '"+str(f).replace("'","''")+"'; if($s.Status -eq 'Valid' -and $s.SignerCertificate){[BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($s.SignerCertificate.RawData)).Replace('-','')}"
    # Do not pass a PowerShell 7 module path into Windows PowerShell 5.
    env={k:v for k,v in os.environ.items() if k.lower()!='psmodulepath'}
    r=subprocess.run(['powershell','-NoProfile','-Command',command],capture_output=True,text=True,env=env)
    pin=r.stdout.strip()
    if len(pin)==64:candidate=f;break
  if candidate:break
 assert candidate,'No embedded-signed CRT fixture found'
 assert run(candidate,pin).returncode==0
 assert run(candidate,'0'*64).returncode==1
 p.write_bytes(candidate.read_bytes());raw=bytearray(p.read_bytes());raw[1024]^=1;p.write_bytes(raw)
 assert run(p,pin).returncode==1
 print('Authenticode: unsigned, trusted correct/wrong publisher, tampered bytes and invalid pin passed; no installer executed')
