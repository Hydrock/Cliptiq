"""Windows-only idle probe: three connected real TLS Services in one test process.
Uses synthetic clipboard hooks and in-memory test credentials, not the user clipboard.
The result is the combined test-process cost, NOT main-application memory.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
shared = runpy.run_path(str(Path(__file__).with_name('measure.py')))
env = os.environ.copy()
env['CLIPTIQ_LAN_IDLE_BENCH'] = '1'
process = subprocess.Popen([str(args.build_dir.resolve() / 'lan_end_to_end_test.exe')],
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    while True:
        line = process.stdout.readline()
        if not line:
            raise RuntimeError('Test exited before connected-idle checkpoint')
        if line.startswith('READY'):
            break
    result = shared['measure'](process, 30, 5)
    result['fixture'] = 'three connected Services, one test process; fake clipboard and key store'
    output = process.communicate(timeout=20)[0]
    if process.returncode:
        raise RuntimeError(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
