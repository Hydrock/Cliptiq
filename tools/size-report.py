"""Measure a real distribution; unknown dependency/OS costs remain null."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def measure(dist, archive, *, target, arch, dependency_delta_bytes=None,
            ocr_bytes=0, toolchain=None, flags=None, runtime=None):
    dist, archive = Path(dist).resolve(), Path(archive).resolve()
    if not dist.is_dir() or archive.is_relative_to(dist):
        raise ValueError('dist must exist and archive must be outside dist')
    paths = sorted(dist.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('symlink in distribution: explicit closure required')
    files = [path for path in paths if path.is_file()]
    if not files:
        raise ValueError('empty distribution')
    for amount in (dependency_delta_bytes, ocr_bytes):
        if amount is not None and amount < 0:
            raise ValueError('byte counts cannot be negative')
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in files:
            info = zipfile.ZipInfo(path.relative_to(dist).as_posix(), date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = (path.stat().st_mode & 0xffff) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, path.read_bytes(), compresslevel=9)
    app_bytes = sum(path.stat().st_size for path in files)
    runtime = runtime or {}
    return {
        'schema_version': 1, 'target': target, 'arch': arch,
        'toolchain': toolchain, 'flags': flags,
        'app_bytes': app_bytes, 'archive_bytes': archive.stat().st_size,
        'dependency_delta_bytes': dependency_delta_bytes, 'ocr_bytes': ocr_bytes,
        'total_offline_bytes': None if dependency_delta_bytes is None or ocr_bytes is None else app_bytes + dependency_delta_bytes + ocr_bytes,
        'idle_memory_bytes': runtime.get('idle_memory_bytes'),
        'startup_ms': runtime.get('startup_ms'),
        'measurement_scope': 'prototype; host OS libraries excluded, clean-machine closure unverified',
        'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'files': [{'path': p.relative_to(dist).as_posix(), 'bytes': p.stat().st_size,
                   'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in files],
        'runtime': runtime,
    }


def budget_violations(report, limits):
    violations = []
    for key, limit in limits.items():
        value = report.get(key)
        if value is None:
            violations.append(f'{key}: not measured')
        elif value > limit:
            violations.append(f'{key}: {value} > {limit}')
    return violations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', required=True, type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--target', required=True)
    parser.add_argument('--arch', default='x86_64')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--toolchain')
    parser.add_argument('--flags')
    parser.add_argument('--dependency-delta-bytes', type=int)
    parser.add_argument('--ocr-bytes', type=int, default=0)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    report = measure(args.dist, args.archive or args.output.with_suffix('.zip'),
                     target=args.target, arch=args.arch,
                     dependency_delta_bytes=args.dependency_delta_bytes,
                     ocr_bytes=args.ocr_bytes, toolchain=args.toolchain, flags=args.flags,
                     runtime=json.loads(args.runtime.read_text()) if args.runtime else None)
    violations = budget_violations(report, json.loads(args.baseline.read_text())['limits']) if args.baseline else []
    report['budget_violations'] = violations
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('app_bytes', 'archive_bytes', 'total_offline_bytes')}))
    return 1 if violations else 0


if __name__ == '__main__':
    raise SystemExit(main())
