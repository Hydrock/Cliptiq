"""Compare built synthetic core benchmarks; no builds or performance acceptance gate."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess

RUNS = 7
PHASES = ('capture_ingest', 'list_recent_30', 'load_snapshot',
          'duplicate_ingest', 'touch_history')
COUNTERS = ('count', 'payload_bytes', 'loaded_bytes', 'rows_observed')


def validate(sample):
    if (not isinstance(sample, dict) or type(sample.get('schema_version')) is not int
            or sample['schema_version'] != 1
            or sample.get('workload') != 'synthetic-memory-history-v1'
            or sample.get('correct') is not True):
        raise ValueError('benchmark must report the supported workload and correct=true')
    for key in COUNTERS:
        if type(sample.get(key)) is not int or sample[key] <= 0:
            raise ValueError('invalid benchmark counter: ' + key)
    if sample['loaded_bytes'] != sample['payload_bytes'] or sample['rows_observed'] != sample['count'] * 30:
        raise ValueError('benchmark payload or row consistency failed')
    phases = sample.get('phases')
    if not isinstance(phases, list) or len(phases) != len(PHASES):
        raise ValueError('missing benchmark phases')
    names = []
    for phase in phases:
        if not isinstance(phase, dict):
            raise ValueError('invalid benchmark phase')
        names.append(phase.get('name'))
        if type(phase.get('operations')) is not int or phase['operations'] != sample['count']:
            raise ValueError('phase operation count mismatch')
        for metric in ('elapsed_ms', 'cpu_ms'):
            value = phase.get(metric)
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise ValueError('invalid phase timing: ' + metric)
    if tuple(names) != PHASES:
        raise ValueError('unexpected, duplicate, or reordered benchmark phases')
    return tuple(sample[key] for key in ('workload', *COUNTERS))


def metadata(directory):
    directory = Path(directory).resolve(strict=True)
    choices = [directory / 'core_package_benchmark', directory / 'core_package_benchmark.exe']
    binaries = [path for path in choices if path.is_file()]
    if len(binaries) != 1:
        raise ValueError('expected one built core_package_benchmark in ' + str(directory))
    cache = directory / 'CMakeCache.txt'
    selected = {}
    for line in cache.read_text(encoding='utf-8-sig').splitlines():
        if not line or line.startswith(('#', '//')) or '=' not in line or ':' not in line.split('=', 1)[0]:
            continue
        typed, value = line.split('=', 1)
        key = typed.split(':', 1)[0]
        if (key.startswith(('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER', 'CMAKE_C_FLAGS',
                            'CMAKE_CXX_FLAGS', 'CMAKE_EXE_LINKER_FLAGS',
                            'CMAKE_INTERPROCEDURAL_OPTIMIZATION', 'CMAKE_OSX_',
                            'CMAKE_MSVC_RUNTIME_LIBRARY', 'CLIPTIQ_SQLITE_'))
                or key in ('CMAKE_BUILD_TYPE', 'CMAKE_LINKER', 'CMAKE_LINKER_TYPE', 'CMAKE_GENERATOR')):
            selected[typed] = value
    binary = binaries[0]
    return {'directory': str(directory), 'binary': str(binary),
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'cmake_cache': selected}


def compare(reference, candidate, runner=subprocess.run):
    builds = {'reference': metadata(reference), 'candidate': metadata(candidate)}
    samples = {name: [] for name in builds}
    order = []
    expected = None
    for index in range(RUNS):
        for name in (('reference', 'candidate') if index % 2 == 0 else ('candidate', 'reference')):
            result = runner([builds[name]['binary']], capture_output=True, text=True,
                            encoding='utf-8', errors='strict', check=True, timeout=120)
            sample = json.loads(result.stdout)
            identity = validate(sample)
            if expected is None:
                expected = identity
            if identity != expected:
                raise ValueError('benchmark workload/counters differ between runs or builds')
            order.append(name)
            samples[name].append({'sample': sample, 'stdout': result.stdout, 'stderr': result.stderr})
    summary = []
    for index, phase in enumerate(PHASES):
        row = {'name': phase}
        for metric in ('elapsed_ms', 'cpu_ms'):
            medians = {name: statistics.median(run['sample']['phases'][index][metric]
                                              for run in samples[name]) for name in builds}
            row[metric] = {'reference_median': medians['reference'],
                           'candidate_median': medians['candidate'],
                           'candidate_over_reference': medians['candidate'] / medians['reference']
                           if medians['reference'] > 0 else None}
            if medians['reference'] == 0:
                row[metric]['ratio_unavailable'] = 'reference median is zero (clock resolution)'
        summary.append(row)
    identical = Path(builds['reference']['binary']).samefile(builds['candidate']['binary'])
    return {'schema_version': 1, 'scope': 'tool smoke: identical binary' if identical else
            'synthetic in-memory core workload only; no UI or system-memory measurement',
            'performance_acceptance': 'not assessed; no automatic threshold',
            'runs_per_build': RUNS, 'execution_order': order, 'builds': builds,
            'samples': samples, 'phases': summary}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.reference, args.candidate)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print('::notice title=Core benchmark scope::' + report['scope'])
    for phase in report['phases']:
        ratios = [phase[metric]['candidate_over_reference'] for metric in ('elapsed_ms', 'cpu_ms')]
        values = ['unavailable' if ratio is None else f'{ratio:.3f}x' for ratio in ratios]
        print(f"::notice title=Core benchmark {phase['name']}::median candidate/reference wall={values[0]} CPU={values[1]}; descriptive only")


if __name__ == '__main__':
    main()
