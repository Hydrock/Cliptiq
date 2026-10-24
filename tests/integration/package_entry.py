"""Noninteractive packaged entry-point smoke; never registers capture/input."""
import pathlib,subprocess,sys
for name in sys.argv[1:]:
 binary=pathlib.Path(name).resolve();result=subprocess.run([str(binary),'--version'],capture_output=True,text=True,timeout=10)
 assert result.returncode==0,(binary,result.returncode,result.stderr)
 assert result.stdout.strip()=='Cliptiq 0.1.0',result.stdout
print('PASS noninteractive --version')
