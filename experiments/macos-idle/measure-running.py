"""Measure one existing macOS process through proc_pid_rusage (no clipboard read)."""
import argparse,ctypes,json,os,pathlib,platform,statistics,subprocess,time

from process_usage import read_usage,timebase

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid',type=int,required=True)
parser.add_argument('--scenario',required=True)
parser.add_argument('--seconds',type=int,default=60)
parser.add_argument('--settle',type=int,default=10)
parser.add_argument('--output',type=pathlib.Path,required=True)
args=parser.parse_args()
def sample():
    return dict(elapsed_seconds=time.monotonic()-start,**read_usage(args.pid))

time.sleep(args.settle)
start=time.monotonic();samples=[sample()]
for n in range(1,args.seconds+1):
    time.sleep(max(0,start+n-time.monotonic()));samples.append(sample())
if len({s['start_identity'] for s in samples})!=1:raise RuntimeError('Process restarted during measurement')
duration=samples[-1]['elapsed_seconds']-samples[0]['elapsed_seconds']
result=dict(pid=args.pid,scenario=args.scenario,duration_seconds=duration,settle_seconds=args.settle,
            os=platform.platform(),logical_cpus=os.cpu_count(),
            cpu_timebase_numer=timebase.numer,cpu_timebase_denom=timebase.denom,
            cpu_percent_one_core=100*(samples[-1]['cpu_seconds']-samples[0]['cpu_seconds'])/duration,
            rss_mib_mean=statistics.mean(s['rss_bytes'] for s in samples)/2**20,
            rss_mib_peak=max(s['rss_bytes'] for s in samples)/2**20,
            footprint_mib_mean=statistics.mean(s['footprint_bytes'] for s in samples)/2**20,
            footprint_mib_peak=max(s['footprint_bytes'] for s in samples)/2**20,
            idle_wakeups_per_second=(samples[-1]['idle_wakeups']-samples[0]['idle_wakeups'])/duration,
            samples=samples)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
