"""Profile macOS executable size using disposable copies; never edit the input."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def zipped_bytes(data):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        entry = zipfile.ZipInfo('Cliptiq', date_time=(2026, 1, 1, 0, 0, 0))
        entry.create_system = 3
        entry.external_attr = 0o100755 << 16
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, data, compresslevel=9)
    return len(stream.getvalue())


def command(argv):
    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    return {'argv': argv[:-1] + ['<binary>'], 'returncode': result.returncode,
            'output': result.stdout}


def profile(binary, runner=command):
    binary = Path(binary).resolve(strict=True)
    if not binary.is_file():
        raise ValueError('binary must be a regular file')
    original = binary.read_bytes()
    result = {'schema_version': 1, 'binary': str(binary),
              'sha256': hashlib.sha256(original).hexdigest(),
              'scope': 'Disposable unsigned-size experiments; no signature or runtime validity claims. ZIP measures one binary only.',
              'variants': []}
    with tempfile.TemporaryDirectory(prefix='cliptiq-native-size-') as folder:
        for name, flags in [('original', None), ('strip-local', ['-x']),
                            ('strip-debug', ['-S']), ('strip-local-debug', ['-x', '-S'])]:
            copy = Path(folder) / name / 'Cliptiq'
            copy.parent.mkdir()
            shutil.copyfile(binary, copy)
            row = {'name': name, 'flags': flags or []}
            if flags:
                row['strip'] = runner(['xcrun', 'strip', *flags, str(copy)])
            row['sections'] = runner(['xcrun', 'size', '-m', str(copy)])
            row['load_commands'] = runner(['xcrun', 'otool', '-l', str(copy)])
            data = copy.read_bytes()
            row.update(bytes=len(data), zip_bytes=zipped_bytes(data),
                       sha256=hashlib.sha256(data).hexdigest())
            result['variants'].append(row)
    if binary.read_bytes() != original:
        raise RuntimeError('input changed during profiling')
    result['input_unchanged'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() == args.binary.resolve():
        parser.error('output must differ from input binary')
    report = profile(args.binary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    base = report['variants'][0]
    for row in report['variants']:
        status = row.get('strip', {}).get('returncode', 0)
        print(f"::notice title=Native size {row['name']}::bytes={row['bytes']} zip_bytes={row['zip_bytes']} saved={base['bytes']-row['bytes']} zip_saved={base['zip_bytes']-row['zip_bytes']} strip_exit={status}; diagnostic copies only")


if __name__ == '__main__':
    main()
