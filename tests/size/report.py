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
                            for folder_name in ('core', 'cmake', 'platform/macos', 'platform/windows', 'platform/linux', 'third_party/libgrapheme')
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
if system == 'Linux':
    import sys
    def linux_variant(name):
        path = folder / name
        for transport in ('x11', 'wayland'):
            subprocess.run([sys.executable, str(root / 'tools/run-linux-isolated.py'), transport,
                            str(path), transport], check=True, timeout=20)
        raw = path.read_bytes()
        compressed = io.BytesIO()
        with zipfile.ZipFile(compressed, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            item = zipfile.ZipInfo('clipboard_probe', date_time=(2026, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(item, raw)
        linked = subprocess.check_output(['ldd', str(path)], text=True).splitlines()
        paths = {line.split('=>', 1)[1].strip().split()[0] for line in linked if '=>' in line and 'not found' not in line}
        libraries = {p: pathlib.Path(p).stat().st_size for p in sorted(paths) if pathlib.Path(p).is_file()}
        return {'executable_bytes': len(raw), 'zip_bytes': len(compressed.getvalue()),
                'sha256': hashlib.sha256(raw).hexdigest(), 'linked_libraries': linked,
                'resolved_shared_library_bytes': libraries,
                'resolved_shared_library_total_bytes': sum(libraries.values())}
    baseline = linux_variant('linux_clipboard_baseline')
    native = linux_variant('linux_clipboard_probe')
    report['linux_clipboard'] = {'baseline': baseline, 'with_backend': native,
        'added_bytes': native['executable_bytes'] - baseline['executable_bytes'],
        'added_zip_bytes': native['zip_bytes'] - baseline['zip_bytes'],
        'scope': 'paired core policy/history host vs combined X11+Wayland/session/worker; private Xvfb and Sway runs; system libraries not bundled; not full product or offline package closure'}
def menu_variant(name):
    path = folder / (name + ('.exe' if system == 'Windows' else ''))
    if system == 'Linux':
        for transport in ('x11', 'wayland'):
            subprocess.run([sys.executable, str(root / 'tools/run-linux-isolated.py'), transport, str(path), '--smoke'], check=True, timeout=20)
    else:
        subprocess.run([str(path), '--smoke'], check=True, timeout=20)
    raw = path.read_bytes()
    compressed = io.BytesIO()
    with zipfile.ZipFile(compressed, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        item = zipfile.ZipInfo('menu_host', date_time=(2026, 1, 1, 0, 0, 0))
        item.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(item, raw)
    command = ['dumpbin', '/DEPENDENTS', str(path)] if system == 'Windows' else ['otool', '-L', str(path)] if system == 'Darwin' else ['ldd', str(path)]
    linked = subprocess.check_output(command, text=True).splitlines()
    return {'executable_bytes': len(raw), 'zip_bytes': len(compressed.getvalue()), 'sha256': hashlib.sha256(raw).hexdigest(), 'linked_libraries': linked}
menu_baseline = menu_variant('menu_history_baseline')
menu_model = menu_variant('menu_model_probe')
menu_native = menu_variant('menu_native_probe')
report['menu'] = {'baseline': menu_baseline, 'model': menu_model, 'native': menu_native,
    'model_added_bytes': menu_model['executable_bytes'] - menu_baseline['executable_bytes'],
    'native_added_bytes': menu_native['executable_bytes'] - menu_model['executable_bytes'],
    'total_added_bytes': menu_native['executable_bytes'] - menu_baseline['executable_bytes'],
    'scope': 'same history/capture-capable review host baseline, shared model + Unicode17, then native popup/tray/launcher; no capture in smoke; system GUI libraries not bundled; not full product'}
report['source_files_sha256']['platform/MenuHost.cpp'] = hashlib.sha256((root / 'platform/MenuHost.cpp').read_bytes()).hexdigest()
paste_native = menu_variant('paste_native_probe')
report['paste'] = {'baseline': menu_native, 'with_paste': paste_native, 'added_bytes': paste_native['executable_bytes']-menu_native['executable_bytes'], 'added_zip_bytes': paste_native['zip_bytes']-menu_native['zip_bytes'], 'scope': 'paired same native menu/capture-capable host without and with selection pipeline, modifier mapping and native paste; smoke never reads/writes clipboard or sends input; system libraries not bundled; not full product'}
settings_native = menu_variant('settings_native_probe')
report['settings'] = {'baseline': paste_native, 'with_settings': settings_native, 'added_bytes': settings_native['executable_bytes']-paste_native['executable_bytes'], 'added_zip_bytes': settings_native['zip_bytes']-paste_native['zip_bytes'], 'scope': 'same native history/menu/paste review host without and with settings UI/global shortcut services; no global registration or clipboard IO in smoke; system libraries not bundled; not complete product'}
snippet_editor = menu_variant('snippet_editor_probe')
report['snippets'] = {'baseline': settings_native, 'with_editor': snippet_editor, 'added_bytes': snippet_editor['executable_bytes']-settings_native['executable_bytes'], 'added_zip_bytes': snippet_editor['zip_bytes']-settings_native['zip_bytes'], 'scope': 'same native settings/menu/history/paste host without/with bounded snippet repository/session and native plain-text editor; smoke no clipboard/global grabs/input; system GUI libraries not bundled; not complete product'}
if system == 'Windows':
    import os
    rich = pathlib.Path(os.environ['WINDIR']) / 'System32' / 'Msftedit.dll'
    report['snippets']['dynamic_system_component'] = {'name': 'Msftedit.dll', 'bytes': rich.stat().st_size, 'sha256': hashlib.sha256(rich.read_bytes()).hexdigest(), 'scope': 'existing OS RichEdit component loaded from System32, not bundled; not full system dependency closure'}
# Offline converter is a separate delivery, never part of the app executable.
migration_binary=folder/'tools'/'import-clipy'/('cliptiq-import.exe' if system=='Windows' else 'cliptiq-import')
b=migration_binary.read_bytes();archive=io.BytesIO()
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 item=zipfile.ZipInfo(migration_binary.name,date_time=(2026,1,1,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED;z.writestr(item,b)
report['migration']={'optional_converter_bytes':len(b),'optional_converter_zip_bytes':len(archive.getvalue()),'sha256':hashlib.sha256(b).hexdigest(),'app_linked_migration_bytes':0,'scope':'separate C++ offline converter; Realm helper measured separately, no app linkage'}
for p in sorted((root/'tools/import-clipy').rglob('*')):
 if p.is_file() and '.build' not in p.parts and p.name!='Package.resolved':report['source_files_sha256'][p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
(root / 'out' / 't02-size.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

snippet_xml = menu_variant('snippet_xml_probe')
media = menu_variant('media_native_probe')
lifecycle = menu_variant('lifecycle_native_probe')
report['lifecycle'] = {'baseline': media, 'with_lifecycle': lifecycle, 'added_bytes': lifecycle['executable_bytes']-media['executable_bytes'], 'added_zip_bytes': lifecycle['zip_bytes']-media['zip_bytes'], 'scope': 'paired media host without/with single-instance lock/mailbox, autostart, shared translation catalogues and native lifecycle integration; smoke creates no real IPC/registration/capture; not installer'}
for directory in ('resources', 'platform/posix'):
    for p in sorted((root/directory).rglob('*')):
        if p.is_file(): report['source_files_sha256'][p.relative_to(root).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
for name in ('platform/Lifecycle.cpp', 'platform/LifecycleNative.hpp', 'platform/LocaleNative.hpp'):
    report['source_files_sha256'][name] = hashlib.sha256((root/name).read_bytes()).hexdigest()
report['media'] = {'baseline': snippet_xml, 'with_media': media, 'added_bytes': media['executable_bytes']-snippet_xml['executable_bytes'], 'added_zip_bytes': media['zip_bytes']-snippet_xml['zip_bytes'], 'scope': 'paired XML host with/without native media codecs, bounded worker, cache and screenshot file monitor; no watcher/clipboard/input in smoke; not installer'}
for name in ['platform/Media.cpp', 'platform/Media.hpp']:
    report['source_files_sha256'][name] = hashlib.sha256((root/name).read_bytes()).hexdigest()
report['xml'] = {'baseline': snippet_editor, 'with_xml': snippet_xml, 'added_bytes': snippet_xml['executable_bytes']-snippet_editor['executable_bytes'], 'added_zip_bytes': snippet_xml['zip_bytes']-snippet_editor['zip_bytes'], 'scope': 'paired same native snippet editor host with XML disabled/enabled; pinned static TinyXML2 + bounded codec, atomic append/file IO, native file pickers and worker; no clipboard/input/global grabs in smoke; not installer/full product'}
report['source_files_sha256'].update({p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'third_party/tinyxml2').rglob('*')) if p.is_file()})
report['source_files_sha256']['platform/SnippetXml.hpp']=hashlib.sha256((root/'platform/SnippetXml.hpp').read_bytes()).hexdigest()
(root / 'out' / 't02-size.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
