"""Check CPU units against the OS process CPU clock using a real busy loop."""
import os,time
from process_usage import read_usage,timebase
before=read_usage(os.getpid());started=time.process_time()
while time.process_time()-started<.15:
    sum(i*i for i in range(1000))
expected=time.process_time()-started;after=read_usage(os.getpid())
measured=after['cpu_seconds']-before['cpu_seconds']
assert .8*expected<measured<1.2*expected,(expected,measured)
assert after['rss_bytes']>0 and after['footprint_bytes']>0
print(f'PASS CPU clock {expected:.6f}s ~= rusage {measured:.6f}s; Mach timebase {timebase.numer}/{timebase.denom}')
