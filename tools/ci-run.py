"""Run an existing CI command unchanged; annotate a bounded failure tail."""
import collections,subprocess,sys

def run(command,stream):
 tail=collections.deque(maxlen=60)
 process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
 for line in process.stdout:
  stream.write(line);stream.flush();tail.append(line[-4096:])
 code=process.wait()
 if code:
  text=('Command exited '+str(code)+'\n'+''.join(tail))[-3500:]
  text=text.replace('%','%25').replace('\r','%0D').replace('\n','%0A')
  print('::error::'+text,file=stream)
 return code

if __name__=='__main__':
 if len(sys.argv)<2:raise SystemExit('Usage: ci-run.py COMMAND [ARG ...]')
 raise SystemExit(run(sys.argv[1:],sys.stdout))
