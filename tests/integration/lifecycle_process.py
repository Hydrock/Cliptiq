"""Real subprocess forwarding/crash recovery; private test mailbox, no GUI/input."""
import pathlib,subprocess,sys,tempfile,time,os,sqlite3
exe=str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='cliptiq-process-') as directory:
 root=pathlib.Path(directory)
 if os.name!='nt':root.chmod(0o700)
 def start():
  ready=root/'ready';ready.unlink(missing_ok=True)
  owner=subprocess.Popen([exe,'--serve',str(root/'private'),str(ready)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  deadline=time.monotonic()+5
  while not ready.exists():
   if owner.poll() is not None:raise RuntimeError(owner.communicate())
   if time.monotonic()>deadline:owner.kill();raise RuntimeError('primary startup timeout')
   time.sleep(.02)
  return owner
 def forward(action):
  deadline=time.monotonic()+5
  while True:
   result=subprocess.run([exe,'--forward',str(root/'private'),action],capture_output=True,text=True,timeout=5)
   if result.returncode==75 and time.monotonic()<deadline:time.sleep(.02);continue
   assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
   break
 owner=start()
 try:
  for action in ['main','history','snippets','settings']:forward(action)
  stdout,stderr=owner.communicate(timeout=8);assert owner.returncode==0,(stdout,stderr)
  assert (root/'private/events').read_text().splitlines()==['main','history','snippets','settings']
 finally:
  if owner.poll() is None:owner.kill();owner.wait()
 owner=start();owner.kill();owner.wait()
 owner=start()
 try:
  blocker=sqlite3.connect(root/'private/process.sqlite3');blocker.execute('BEGIN IMMEDIATE');time.sleep(.4)
  try:assert owner.poll() is None,('primary must retry transient mailbox Busy',owner.communicate())
  finally:blocker.rollback();blocker.close()
  for _ in range(4):forward('settings')
  stdout,stderr=owner.communicate(timeout=8);assert owner.returncode==0,(stdout,stderr)
 finally:
  if owner.poll() is None:owner.kill();owner.wait()
print('PASS separate processes, four fixed routes, settings recovery, crash/restart lock release')
