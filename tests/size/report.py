"""Report linked core probe and archive sizes; never claim a full product size."""
import hashlib
import io
import json
import pathlib
import platform
import subprocess
import zipfile

root = pathlib.Path(__file__).resolve().parents[2]
system = platform.system()
folder = root / 'out' / f'core-{system}-release'
probe = folder / ('core_probe.exe' if system == 'Windows' else 'core_probe')
subprocess.run([str(probe)], check=True)
data = probe.read_bytes()
archive = io.BytesIO()
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    item = zipfile.ZipInfo(probe.name, date_time=(2026, 1, 1, 0, 0, 0))
    item.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(item, data)
report = {
    'scope': 'linked T02 core API fixture with pinned SQLite; no UI, not complete Cliptiq',
    'system': platform.platform(), 'architecture': platform.machine(),
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'working_tree_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
    'source_files_sha256': {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                            for folder_name in ('core', 'cmake', 'platform/macos', 'platform/windows')
                            for p in sorted((root / folder_name).rglob('*')) if p.is_file()},
    'build_type': 'MinSizeRel', 'windows_crt': 'static' if system == 'Windows' else None,
    'probe_bytes': len(data), 'probe_zip_bytes': len(archive.getvalue()),
    'probe_sha256': hashlib.sha256(data).hexdigest(),
    'static_archives_bytes': {p.name: p.stat().st_size for p in folder.iterdir() if p.suffix in ('.a', '.lib')},
    'full_product_bytes': None, 'offline_dependencies_bytes': None,
}
cache = (folder / 'CMakeCache.txt').read_text(encoding='utf-8')
report['cmake_configuration'] = [line for line in cache.splitlines()
    if line.startswith(('CMAKE_C_COMPILER:', 'CMAKE_CXX_COMPILER:', 'CMAKE_C_FLAGS_MINSIZEREL:',
                        'CMAKE_CXX_FLAGS_MINSIZEREL:', 'CMAKE_MSVC_RUNTIME_LIBRARY:', 'CMAKE_OSX_DEPLOYMENT_TARGET:'))]
policy_probe = folder / ('core_policy_probe.exe' if system == 'Windows' else 'core_policy_probe')
if policy_probe.exists():
    report['scope'] = 'paired T02 core baseline and T03 policy dispatch integration fixtures; no native UI, not complete Cliptiq'
    subprocess.run([str(policy_probe)], check=True)
    policy_data = policy_probe.read_bytes()
    policy_archive = io.BytesIO()
    with zipfile.ZipFile(policy_archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        item = zipfile.ZipInfo(probe.name, date_time=(2026, 1, 1, 0, 0, 0))
        item.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(item, policy_data)
    report['policy_probe_bytes'] = len(policy_data)
    report['policy_probe_zip_bytes'] = len(policy_archive.getvalue())
    report['policy_probe_sha256'] = hashlib.sha256(policy_data).hexdigest()
    report['policy_added_bytes'] = len(policy_data) - len(data)
    report['policy_added_zip_bytes'] = len(policy_archive.getvalue()) - len(archive.getvalue())
    report['policy_comparison'] = 'same source/compiler/flags/native-free API fixture; policy dispatch linked only in second variant'
if system == 'Darwin':
    report['os_build'] = subprocess.check_output(['sw_vers', '-buildVersion'], text=True).strip()
    def native_variant(name):
        path = folder / name
        subprocess.run([str(path)], check=True, timeout=15)
        raw = path.read_bytes()
        compressed = io.BytesIO()
        with zipfile.ZipFile(compressed, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            item = zipfile.ZipInfo('clipboard_probe', date_time=(2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(item, raw)
        return {'executable_bytes': len(raw), 'zip_bytes': len(compressed.getvalue()),
                'sha256': hashlib.sha256(raw).hexdigest(),
                'linked_libraries': subprocess.check_output(['otool', '-L', str(path)], text=True).splitlines()[1:],
                'build_version': subprocess.check_output(['otool', '-l', str(path)], text=True).split('LC_BUILD_VERSION')[-1].split('Load command')[0].strip()}
    baseline = native_variant('macos_clipboard_baseline')
    native = native_variant('macos_clipboard_probe')
    report['macos_clipboard'] = {'baseline': baseline, 'with_backend': native,
        'added_bytes': native['executable_bytes'] - baseline['executable_bytes'],
        'added_zip_bytes': native['zip_bytes'] - baseline['zip_bytes'],
        'scope': 'paired AppKit/core host vs native clipboard + timer/session/worker integration; not isolated backend machine code or full app'}
if system == 'Windows':
    def windows_variant(name):
        path = folder / (name + '.exe')
        subprocess.run([str(path)], check=True, timeout=15)
        raw = path.read_bytes()
        compressed = io.BytesIO()
        with zipfile.ZipFile(compressed, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            item = zipfile.ZipInfo('clipboard_probe.exe', date_time=(2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(item, raw)
        return {'executable_bytes': len(raw), 'zip_bytes': len(compressed.getvalue()),
                'sha256': hashlib.sha256(raw).hexdigest(),
                'linked_libraries': subprocess.check_output(['dumpbin', '/DEPENDENTS', str(path)], text=True).splitlines()}
    baseline = windows_variant('windows_clipboard_baseline')
    native = windows_variant('windows_clipboard_probe')
    report['windows_clipboard'] = {'baseline': baseline, 'with_backend': native,
        'added_bytes': native['executable_bytes'] - baseline['executable_bytes'],
        'added_zip_bytes': native['zip_bytes'] - baseline['zip_bytes'],
        'scope': 'paired Win32/core host vs clipboard + listener/session/worker/WIC integration; isolated noninteractive station, not full app'}
(root / 'out' / 't02-size.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
