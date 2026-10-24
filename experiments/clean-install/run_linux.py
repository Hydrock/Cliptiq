"""Measure offline runtime closure in disposable, clean Ubuntu containers."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lane', choices=['gtk3', 'gtk4', 'ocr'], required=True)
    parser.add_argument('--image', default='ubuntu:24.04')
    parser.add_argument('--deb', type=Path, help='optional local packaging fixture to install/remove offline (GTK3 lane)')
    args = parser.parse_args()
    docker = shutil.which('docker') or 'C:/Program Files/Docker/Docker/resources/bin/docker.exe'
    lane = 'linux-x86_64-base-' + ('gtk4' if args.lane == 'gtk4' else 'gtk3') + ('-ocr' if args.lane == 'ocr' else '')
    run_id = uuid.uuid4().hex[:10]
    run_root = ROOT / 'out/clean-install' / args.lane / 'runs' / run_id
    dist = run_root / 'app'
    output = run_root / 'evidence'
    dist.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    for language in ('cpp', 'rust'):
        shutil.copy2(ROOT / 'build/footprint' / lane / ('probe_' + language), dist)
    name = 'cliptiq-clean-' + args.lane + '-' + run_id
    log = output / 'container.log'

    def run(*command, timeout=900):
        result = subprocess.run([docker, *map(str, command)], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
        with log.open('a', encoding='utf-8') as stream:
            stream.write('$ docker ' + ' '.join(map(str, command)) + '\n' + result.stdout + result.stderr + '\n')
        if result.returncode:
            raise RuntimeError(f'docker command failed; see {log}: {(result.stdout + result.stderr)[-1500:]}')
        return result.stdout.strip()

    log.write_text('', encoding='utf-8')
    run('pull', args.image)
    image = json.loads(run('image', 'inspect', args.image))[0]
    image_ref = image['RepoDigests'][0]
    try:
        extra_mount = []
        if args.deb:
            if args.lane != 'gtk3' or not args.deb.resolve().is_file():
                raise RuntimeError('--deb requires an existing GTK3 packaging fixture')
            extra_mount = ['--mount', f'type=bind,source={args.deb.resolve()},target=/payload/update.deb,readonly']
        run('create', '--name', name,
            '--mount', f'type=bind,source={ROOT / "experiments/clean-install"},target=/work,readonly',
            '--mount', f'type=bind,source={dist},target=/app,readonly',
            '--mount', f'type=bind,source={output},target=/out', *extra_mount, image_ref, 'sleep', 'infinity')
        run('start', name)
        run('exec', name, 'sh', '/work/linux_measure.sh', 'download', args.lane)
        run('network', 'disconnect', 'bridge', name)
        networks = json.loads(run('inspect', '--format', '{{json .NetworkSettings.Networks}}', name))
        if networks:
            raise RuntimeError('container still has a network attachment')
        run('exec', name, 'sh', '/work/linux_measure.sh', 'install', args.lane)
        report = json.loads((output / 'report.json').read_text())
        report['image_digest'] = image_ref
        report['measurement_run_id'] = run_id
        report['image_id'] = image['Id']
        report['app_bytes'] = {p.name: p.stat().st_size for p in dist.iterdir() if p.is_file()}
        report['before_runtime_exit'] = int((output / 'before-runtime-exit.txt').read_text())
        report['complete_install_apparent_bytes'] = {name: size + report['runtime_apparent_install_delta_bytes'] for name, size in report['app_bytes'].items()}
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        (ROOT / 'out/clean-install' / args.lane / 'latest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, indent=2))
    finally:
        # Only the exact, freshly generated container name is removed.
        subprocess.run([docker, 'rm', '-f', name], capture_output=True)


if __name__ == '__main__':
    main()
