"""Measure a real distribution; unknown dependency/OS costs remain null."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
import os,stat


def measure(dist, archive, *, target, arch, dependency_delta_bytes=None,
            ocr_bytes=0, toolchain=None, flags=None, runtime=None):
    dist, archive = Path(dist).resolve(), Path(archive).resolve()
    if not dist.is_dir() or archive.is_relative_to(dist):
        raise ValueError('dist must exist and archive must be outside dist')
    paths = sorted(dist.rglob('*'))
    if any(path.is_symlink() and (Path(os.readlink(path)).is_absolute() or not path.resolve().is_relative_to(dist) or not path.exists()) for path in paths):
        raise ValueError('unsafe symlink in distribution')
    files = [path for path in paths if path.is_file() and not path.is_symlink()]
    links = [path for path in paths if path.is_symlink()]
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
        for path in links:
            info=zipfile.ZipInfo(path.relative_to(dist).as_posix(),date_time=(2026,1,1,0,0,0));info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16;output.writestr(info,os.readlink(path).encode('utf-8'))
    app_bytes = sum(path.stat().st_size for path in files)+sum(len(os.readlink(p).encode("utf-8")) for p in links)
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
        'symlinks': [{'path':p.relative_to(dist).as_posix(),'target':os.readlink(p)} for p in links],
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


def selected_budget(baseline,target,arch,variant):
    variants=baseline.get('variants',{})
    if not variants:return baseline['limits']
    key=f'{target}-{arch}-{variant}'
    if key not in variants:raise ValueError('no measured baseline for '+key)
    previous=variants[key];growth=baseline.get('growth_review_fraction',0.1)
    if not 0<=growth<=1:raise ValueError('invalid growth fraction')
    ceilings=baseline.get('limits' if variant=='base' else 'optional_limits',{})
    return {key:min(int(value*(1+growth)),ceilings.get(key,2**63-1)) for key,value in previous.items() if key in ['app_bytes','archive_bytes']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', required=True, type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--target')
    parser.add_argument('--variant',default='base')
    parser.add_argument('--arch')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--toolchain')
    parser.add_argument('--flags')
    parser.add_argument('--dependency-delta-bytes', type=int)
    parser.add_argument('--ocr-bytes', type=int, default=0)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    from distribution import validate_distribution
    manifest_path=next((args.dist/name for name in ['cliptiq-package.json','usr/share/cliptiq/cliptiq-package.json'] if (args.dist/name).is_file()),args.dist/'cliptiq-package.json')
    if not manifest_path.is_file():parser.error('distribution manifest required; stage with packaging/package.py')
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    errors=validate_distribution(args.dist,manifest)
    if errors:raise ValueError('; '.join(errors))
    args.target=args.target or manifest['target'];args.arch=args.arch or manifest['arch'];args.variant=manifest.get('variant',args.variant)
    if (args.target,args.arch)!=(manifest['target'],manifest['arch']):raise ValueError('CLI target does not match inspected package')
    report = measure(args.dist, args.archive or args.output.with_suffix('.zip'),
                     target=args.target, arch=args.arch,
                     dependency_delta_bytes=args.dependency_delta_bytes,
                     ocr_bytes=args.ocr_bytes, toolchain=args.toolchain, flags=args.flags,
                     runtime=json.loads(args.runtime.read_text()) if args.runtime else None)
    violations = budget_violations(report, selected_budget(json.loads(args.baseline.read_text()),args.target,args.arch,args.variant)) if args.baseline else []
    report['budget_violations'] = violations
    report['variant']=args.variant
    report['included_ocr_bytes']=sum(row['bytes'] for row in report['files'] if row['path'].endswith('.traineddata'))
    report['distribution_manifest']=manifest
    report['measurement_scope']='candidate app-owned files; included models counted once; clean-consumer system delta unverified'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('app_bytes', 'archive_bytes', 'total_offline_bytes')}))
    return 1 if violations else 0


if __name__ == '__main__':
    raise SystemExit(main())
