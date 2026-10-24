"""Install/run Base ZIP in a fresh profile; not a stock-OS VM claim."""
import importlib.util
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def command(args, *, env=None, cwd=None):
    result = subprocess.run(list(map(str, args)), env=env, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
    if result.returncode:
        raise RuntimeError(f'command failed: {args[0]} ({result.returncode}): {result.stderr[-2000:]} {result.stdout[-1000:]}')
    return result.stdout


def extract_result(output, required):
    for line in reversed(output.splitlines()):
        if line.startswith('{'):
            value = json.loads(line)
            if value.get(required) is True:
                return value
    raise RuntimeError(f'no verified {required} marker in probe output')


def main():
    target = platform.system().lower()
    if target not in ('windows', 'darwin'):
        raise SystemExit('Use run_linux.py for a real clean Linux runtime container')
    arch = platform.machine().lower().replace('amd64', 'x86_64')
    lane = ('windows-' + arch + '-base-mt') if target == 'windows' else ('macos-' + arch + '-base')
    output = ROOT / 'out/clean-profile' / (target + '-' + arch)
    output.mkdir(parents=True, exist_ok=True)
    if target == 'windows':
        spec = importlib.util.spec_from_file_location('build_probe', ROOT / 'tools/build-footprint.py')
        build_probe = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build_probe)
        developer_env, _ = build_probe.visual_studio_env()
        dumpbin = shutil.which('dumpbin', path=developer_env['PATH'])
    report = {'schema_version': 1, 'host': platform.platform(), 'architecture': arch,
              'scope': 'fresh HOME/profile and minimal PATH on ephemeral CI/developer OS; not a clean stock-OS VM',
              'clean_vm_verified': False, 'third_party_runtime_closure_check': 'declared linked dependency allowlist, not absence of preinstalled software',
              'variants': {}}
    with tempfile.TemporaryDirectory(prefix='profile-', dir=output) as scratch_name:
        scratch = Path(scratch_name).resolve()
        if not scratch.is_relative_to(output.resolve()):
            raise RuntimeError('unsafe temporary directory')
        home = scratch / 'home'
        home.mkdir()
        temporary = scratch / 'tmp'
        temporary.mkdir()
        if target == 'windows':
            system = os.environ['SYSTEMROOT']
            env = {'SYSTEMROOT': system, 'WINDIR': system, 'PATH': str(Path(system)/'System32'),
                   'TEMP': str(temporary), 'TMP': str(temporary), 'USERPROFILE': str(home),
                   'APPDATA': str(home/'AppData/Roaming'), 'LOCALAPPDATA': str(home/'AppData/Local')}
            Path(env['APPDATA']).mkdir(parents=True)
            Path(env['LOCALAPPDATA']).mkdir(parents=True)
        else:
            env = {'HOME': str(home), 'PATH': '/usr/bin:/bin', 'TMPDIR': str(temporary) + '/', 'LANG': 'en_US.UTF-8'}
        for language in ('cpp', 'rust'):
            stage = scratch / ('stage-' + language)
            stage.mkdir()
            original = ROOT / 'build/footprint' / lane / ('probe_' + language + ('.exe' if target == 'windows' else ''))
            if target == 'windows':
                executable = stage / original.name
                shutil.copy2(original, executable)
                imports = command([dumpbin, '/DEPENDENTS', executable], env=developer_env)
                dlls = re.findall(r'^\s+([\w.-]+\.dll)\s*$', imports, re.MULTILINE | re.IGNORECASE)
                forbidden = [dll for dll in dlls if dll.lower() not in {'kernel32.dll','ntdll.dll','user32.dll','shell32.dll','advapi32.dll','bcrypt.dll','userenv.dll','ws2_32.dll','psapi.dll'} and not dll.lower().startswith(('api-ms-win-', 'ext-ms-win-'))]
                if not dlls or forbidden:
                    raise RuntimeError('non-OS or unparsed dependencies: ' + str(forbidden))
                dependency_evidence = dlls
            else:
                app = stage / 'Cliptiq.app'
                binary = app / 'Contents/MacOS/Cliptiq'
                binary.parent.mkdir(parents=True)
                shutil.copy2(original, binary)
                (app/'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Cliptiq','CFBundleIdentifier':'org.cliptiq.t01.cleanprofile','CFBundleVersion':'1','CFBundlePackageType':'APPL'}))
                command(['/usr/bin/codesign','--force','--sign','-',app])
                command(['/usr/bin/codesign','--verify','--deep','--strict',app])
                executable = binary
                imports = command(['/usr/bin/otool','-L',binary])
                dependencies = [line.strip().split(' (',1)[0] for line in imports.splitlines()[1:]]
                if not dependencies or any(not name.startswith(('/usr/lib/','/System/Library/')) for name in dependencies):
                    raise RuntimeError('non-OS linked dependency: '+str(dependencies))
                dependency_evidence = dependencies
            shutil.copy2(ROOT/'experiments/footprint/NOTICE.txt', stage/'NOTICE.txt')
            archive = output / (language + '.zip')
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as packed:
                for path in sorted(stage.rglob('*')):
                    if path.is_file():
                        packed.write(path,path.relative_to(stage).as_posix())
            installed = scratch / ('Applications-' + language)
            installed.mkdir()
            with zipfile.ZipFile(archive) as packed:
                packed.extractall(installed)
            relative = executable.relative_to(stage)
            installed_exe = installed / relative
            if target == 'darwin':
                installed_exe.chmod(0o755)
                command(['/usr/bin/codesign','--verify','--deep','--strict',installed/'Cliptiq.app'])
            selftest = json.loads(command([installed_exe,'--self-test',scratch/(language+'.xml')],env=env,cwd=installed).splitlines()[-1])
            if selftest.get('entries') != 33 or selftest.get('representations') != 35:
                raise RuntimeError('fixture parity failed after install')
            lifecycle = extract_result(command([installed_exe,'--lifecycle-smoke'],env=env,cwd=installed),'native_window_lifecycle')
            report['variants'][language] = {'installed_app_owned_bytes':sum(path.stat().st_size for path in installed.rglob('*') if path.is_file()),
                'download_zip_bytes':archive.stat().st_size, 'linked_os_dependencies':dependency_evidence,
                'fresh_profile_fixture':selftest, 'fresh_profile_lifecycle':lifecycle,
                'signature':'ad hoc only' if target=='darwin' else 'unsigned measurement executable; not a signed installer'}
        if (scratch/'cpp.xml').read_bytes() != (scratch/'rust.xml').read_bytes():
            raise RuntimeError('XML differs after clean profile installation')
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
