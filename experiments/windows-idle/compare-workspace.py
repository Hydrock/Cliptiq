"""Alternate matched synthetic native workspace benchmark processes.

Run without builds, tests, preview windows or GUI automation in parallel.
Results are CPU-side UI dispatch + synchronous control paint, not compositor
presentation latency. No clipboard, database or real user content is accessed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--before', type=Path, required=True)
p.add_argument('--after', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--trials', type=int, default=5)
a = p.parse_args()
if a.trials < 1:
    p.error('trials must be positive')
result = dict(method=__doc__, environment=platform.platform(), trials=[])
for n in range(a.trials):
    for variant in (['before', 'after'] if n % 2 == 0 else ['after', 'before']):
        exe = getattr(a, variant).resolve()
        run = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
        result['trials'].append(dict(variant=variant, trial=n+1,
                                    exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                                    **json.loads(run.stdout)))
metrics = [k for k in result['trials'][0] if k not in ('variant', 'trial', 'exe_sha256')]
result['medians'] = {v: {k: statistics.median(t[k] for t in result['trials'] if t['variant'] == v)
                         for k in metrics} for v in ('before', 'after')}
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result['medians'], indent=2))
