"""Read CPU/memory counters of an explicitly selected running Windows process.

Does not access clipboard, profile database, windows or network configuration.
CPU percent is relative to one logical CPU. Do not run builds/tests concurrently.
"""
import argparse, ctypes as C, json, os, platform, runpy, statistics, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True)
p.add_argument('--scenario',required=True)
p.add_argument('--seconds',type=float,default=45)
p.add_argument('--settle',type=float,default=5)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if a.pid<=0 or a.seconds<=0 or a.settle<0:p.error("pid/seconds must be positive and settle nonnegative")
shared=runpy.run_path(str(Path(__file__).resolve().parents[1]/'raven-settings/measure.py'))
kernel,snapshot=shared['kernel'],shared['snapshot']
handle=kernel.OpenProcess(0x410,False,a.pid)
if not handle:raise C.WinError()
try:
 time.sleep(a.settle)
 first=last=snapshot(handle);started=time.perf_counter();samples=[]
 while time.perf_counter()-started<a.seconds:
  time.sleep(min(1,max(0,a.seconds-(time.perf_counter()-started))))
  last=snapshot(handle)
  samples.append(dict(elapsed_seconds=time.perf_counter()-started,**last))
 duration=time.perf_counter()-started
 result=dict(pid=a.pid,scenario=a.scenario,duration_seconds=duration,
             environment=dict(os=platform.platform(),logical_cpus=os.cpu_count()),
             cpu_percent_one_core=100*(last['cpu_seconds']-first['cpu_seconds'])/duration,
             working_set_mib_mean=statistics.mean(s['working_set_bytes'] for s in samples)/1048576,
             private_mib_mean=statistics.mean(s['private_bytes'] for s in samples)/1048576,
             samples=samples)
 a.output.parent.mkdir(parents=True,exist_ok=True)
 a.output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
finally:kernel.CloseHandle(handle)
