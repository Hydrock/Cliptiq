"""Profile macOS executable size using disposable copies; never edit the input."""
import argparse
import hashlib
import io
import importlib.util
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


def dyld_view(binary, flag, runner):
    result = runner(['xcrun', 'dyld_info', flag, str(binary)])
    lines = result['output'].splitlines(keepends=True)
    # Apple dyld_info prints '<input path> [<arch>]:' before the actual table.
    # Normalize only that exact heading prefix, preserving the arch and tables.
    if lines and lines[0].startswith(str(binary) + ' ['):
        lines[0] = '<binary>' + lines[0][len(str(binary)):]
    result['normalized_output'] = ''.join(lines)
    result['recognized_table'] = any(line.strip() == flag + ':' for line in lines)
    return result


def same_dyld_view(before, after):
    if (before['returncode'] or after['returncode'] or
            not before['recognized_table'] or not after['recognized_table']):
        return None
    return before['normalized_output'] == after['normalized_output']


def dynamic_references(view):
    if view['returncode']:
        return None
    return [line for line in view['output'].splitlines()
            if '[referenced dynamically]' in line]


def profile(binary, runner=command):
    binary = Path(binary).resolve(strict=True)
    if not binary.is_file():
        raise ValueError('binary must be a regular file')
    original = binary.read_bytes()
    result = {'schema_version': 1, 'binary': str(binary),
              'sha256': hashlib.sha256(original).hexdigest(),
              'scope': 'Disposable unsigned-size experiments; no signature or runtime validity claims. ZIP measures one binary only.',
              'variants': []}
    spec = importlib.util.spec_from_file_location('macos_strip_profile', Path(__file__).with_name('macos_strip.py'))
    inspector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inspector)
    try:
        original_sections = inspector.sections(original)
        section_error = None
    except ValueError as error:
        original_sections, section_error = None, str(error)
    with tempfile.TemporaryDirectory(prefix='cliptiq-native-size-') as folder:
        for name, flags in [('original', None), ('strip-local', ['-x']),
                            ('strip-debug', ['-S']), ('strip-local-debug', ['-x', '-S']),
                            ('strip-executable', ['-u', '-r'])]:
            copy = Path(folder) / name / 'Cliptiq'
            copy.parent.mkdir()
            shutil.copyfile(binary, copy)
            row = {'name': name, 'flags': flags or []}
            if flags:
                row['strip'] = runner(['xcrun', 'strip', *flags, str(copy)])
            row['sections'] = runner(['xcrun', 'size', '-m', str(copy)])
            row['load_commands'] = runner(['xcrun', 'otool', '-l', str(copy)])
            if name in ('original', 'strip-executable'):
                row['undefined_symbols'] = runner(['xcrun', 'nm', '-u', str(copy)])
                # Includes REFERENCED_DYNAMICALLY annotations for comparison.
                row['external_symbol_details'] = runner(['xcrun', 'nm', '-m', '-g', str(copy)])
                row['dyld_exports'] = dyld_view(copy, '-exports', runner)
                row['dyld_imports'] = dyld_view(copy, '-imports', runner)
            data = copy.read_bytes()
            try:
                row['code_data_sections_unchanged'] = inspector.sections(data) == original_sections if original_sections is not None else None
                if section_error:
                    row['section_verification_error'] = section_error
            except ValueError as error:
                row['code_data_sections_unchanged'] = False
                row['section_verification_error'] = str(error)
            row.update(bytes=len(data), zip_bytes=zipped_bytes(data),
                       sha256=hashlib.sha256(data).hexdigest())
            result['variants'].append(row)
        baseline = result['variants'][0]['undefined_symbols']
        stripped = result['variants'][-1]
        observed = stripped['undefined_symbols']
        stripped['undefined_symbols_unchanged'] = (
            baseline['output'] == observed['output']
            if baseline['returncode'] == observed['returncode'] == 0 else None)
        original_row = result['variants'][0]
        for kind in ('exports', 'imports'):
            stripped['dyld_' + kind + '_unchanged'] = same_dyld_view(
                original_row['dyld_' + kind], stripped['dyld_' + kind])
        before_dynamic = dynamic_references(original_row['external_symbol_details'])
        after_dynamic = dynamic_references(stripped['external_symbol_details'])
        stripped['referenced_dynamic_globals_unchanged'] = (
            before_dynamic == after_dynamic
            if before_dynamic is not None and after_dynamic is not None else None)
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
        print(f"::notice title=Native size {row['name']}::bytes={row['bytes']} zip_bytes={row['zip_bytes']} saved={base['bytes']-row['bytes']} zip_saved={base['zip_bytes']-row['zip_bytes']} strip_exit={status} sections_unchanged={row.get('code_data_sections_unchanged')} imports_unchanged={row.get('undefined_symbols_unchanged')} dyld_exports_unchanged={row.get('dyld_exports_unchanged')} dyld_imports_unchanged={row.get('dyld_imports_unchanged')} referenced_dynamic_globals_unchanged={row.get('referenced_dynamic_globals_unchanged')}; diagnostic copies only")


if __name__ == '__main__':
    main()
