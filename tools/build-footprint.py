"""Build and measure equivalent C++/Rust cores with one native UI host.

This is a spike runner, not a product/release build. Run from any directory.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def visual_studio_env():
    env = {key.upper(): value for key, value in os.environ.items()}
    if shutil.which('cl'):
        return env, shutil.which('cmake') or 'cmake'
    bases = {Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')),
             Path(os.environ.get('ProgramFiles', 'C:/Program Files'))}
    candidates = sorted(path for base in bases for path in (base / 'Microsoft Visual Studio').glob('*/*/Common7/Tools/VsDevCmd.bat'))
    if not candidates:
        raise RuntimeError('MSVC Build Tools not found; use a Developer shell')
    script = candidates[-1]
    # cmd quoting must be passed as a Windows command line, not list2cmdline's
    # C-runtime quoting (which inserts backslashes before embedded quotes).
    result = subprocess.run(f'cmd.exe /d /s /c ""{script}" -arch=x64 >nul && set"', capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            env[key.upper()] = value
    cmake = script.parents[1] / 'IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    ninja = script.parents[1] / 'IDE/CommonExtensions/Microsoft/CMake/Ninja'
    env['PATH'] = str(ninja) + os.pathsep + env.get('PATH', '')
    return env, str(cmake)


def run(command, env, log):
    command = [str(item) for item in command]
    command[0] = shutil.which(command[0], path=env.get('PATH')) or command[0]
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
    with log.open('a', encoding='utf-8') as output:
        output.write('$ ' + ' '.join(map(str, command)) + '\n' + result.stdout + result.stderr + '\n')
    if result.returncode:
        print((result.stdout + result.stderr)[-6000:], file=sys.stderr)
        raise RuntimeError(f'command failed ({result.returncode}); see {log}')
    return result.stdout.strip()


def summarize_samples(samples):
    if not samples:
        return {}
    import statistics
    result = dict(samples[len(samples)//2])
    result['startup_ms'] = statistics.median(s['startup_ms'] for s in samples)
    memory = [s['idle_memory_bytes'] for s in samples if s['idle_memory_bytes'] > 0]
    result['idle_memory_bytes'] = statistics.median(memory) if memory else None
    result['samples'] = [dict(sample) for sample in samples]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fts5', action='store_true')
    parser.add_argument('--dynamic-crt', action='store_true')
    parser.add_argument('--gtk4', action='store_true')
    parser.add_argument('--lto', action='store_true', help='also enable C/C++ interprocedural optimization')
    parser.add_argument('--ocr', action='store_true', help='run generated-image OCR probe; not a bundled-model deployment')
    parser.add_argument('--no-benchmark', action='store_true')
    parser.add_argument('--samples', type=int, default=5)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('samples must be positive')
    target = platform.system().lower()
    if target == 'darwin':
        target = 'macos'
    arch = platform.machine().lower().replace('amd64', 'x86_64')
    lane = f'{target}-{arch}' + ('-fts5' if args.fts5 else '-base')
    if target == 'windows':
        lane += '-md' if args.dynamic_crt else '-mt'
        env, cmake = visual_studio_env()
    else:
        env, cmake = os.environ.copy(), shutil.which('cmake') or 'cmake'
        if target == 'linux':
            lane += '-gtk4' if args.gtk4 else '-gtk3'
    if args.lto:
        lane += '-lto'
    if args.ocr:
        lane += '-ocr'
    build = ROOT / 'build' / 'footprint' / lane
    output = ROOT / 'out' / 'footprint' / lane
    output.mkdir(parents=True, exist_ok=True)
    log = output / 'build.log'
    log.write_text('', encoding='utf-8')
    run([sys.executable, ROOT / 'tools/fetch-footprint-deps.py'], env, log)
    run([cmake, '-S', ROOT / 'experiments/footprint', '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=MinSizeRel', f'-DSQLITE_SOURCE={ROOT / ".toolchains/sqlite"}',
         f'-DCMAKE_INTERPROCEDURAL_OPTIMIZATION={"ON" if args.lto else "OFF"}',
         f'-DPROBE_FTS5={"ON" if args.fts5 else "OFF"}',
         f'-DPROBE_STATIC_CRT={"OFF" if args.dynamic_crt else "ON"}',
         f'-DPROBE_GTK4={"ON" if args.gtk4 else "OFF"}',
         f'-DPROBE_OCR={"ON" if args.ocr else "OFF"}'], env, log)
    run([cmake, '--build', build, '--parallel', '2'], env, log)
    run([str(Path(cmake).with_name('ctest.exe')) if target == 'windows' else 'ctest', '--test-dir', build, '--output-on-failure'], env, log)
    if (build / 'cpp-snippets.xml').read_bytes() != (build / 'rust-snippets.xml').read_bytes():
        raise RuntimeError('C++ and Rust XML exports differ')
    spec = importlib.util.spec_from_file_location('size_report', ROOT / 'tools/size-report.py')
    reporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reporter)
    versions = {'cmake': run([cmake, '--version'], env, log).splitlines()[0],
                'rustc': run(['rustc', '--version'], env, log),
                'sqlite': '3.53.4', 'host': platform.platform()}
    if target == 'windows':
        versions['cc'] = env.get('VCTOOLSVERSION', 'MSVC; see build.log')
    else:
        versions['cc'] = run([env.get('CXX', 'c++'), '--version'], env, log).splitlines()[0]
    for language in ('cpp', 'rust'):
        executable = build / ('probe_' + language + ('.exe' if target == 'windows' else ''))
        dist = output / language / 'dist'
        dist.mkdir(parents=True, exist_ok=True)
        shutil.copy2(executable, dist / executable.name)
        shutil.copy2(ROOT / 'experiments/footprint/NOTICE.txt', dist / 'NOTICE.txt')
        samples = []
        runtime_file = output / language / 'runtime.json'
        if not args.no_benchmark:
            for _ in range(args.samples):
                run([executable, '--benchmark', runtime_file], env, log)
                samples.append(json.loads(runtime_file.read_text()))
        runtime = summarize_samples(samples)
        report = reporter.measure(dist, output / (language + '.zip'), target=target, arch=arch, ocr_bytes=None if args.ocr else 0,
                                  toolchain=versions, flags={'fts5': args.fts5, 'static_crt': (not args.dynamic_crt) if target == 'windows' else None,
                                  'gtk': (4 if args.gtk4 else 3) if target == 'linux' else None, 'cpp': 'MinSizeRel', 'cpp_lto': args.lto, 'ocr_probe': args.ocr,
                                  'rust': 'opt-level=z panic=abort lto=thin codegen-units=1'}, runtime=runtime)
        report['language'] = language
        report['lane'] = lane
        report['feature_scope'] = '33 entries / 35 representations, SQLite, XML, shared native host; no updater/history monitor' + ('; generated-image OCR availability probe, model closure unknown' if args.ocr else '; no OCR')
        report['native_ui_ready'] = runtime.get('native_ui_ready')
        (output / (language + '.json')).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'{lane} {language}: app={report["app_bytes"]} bytes zip={report["archive_bytes"]} bytes startup={report["startup_ms"]} ms ui={report["native_ui_ready"]}')
        imports = ['dumpbin', '/DEPENDENTS', executable] if target == 'windows' else (['otool', '-L', executable] if target == 'macos' else ['ldd', executable])
        dependency_text = run(imports, env, log)
        (output / (language + '-dependencies.txt')).write_text(dependency_text + '\n', encoding='utf-8')
        if target == 'linux':
            import re
            libraries = {Path(name).resolve() for name in re.findall(r'(?:=>\s+|^\s*)(/\S+)', dependency_text, re.MULTILINE)}
            linked = [{'path': str(path), 'bytes': path.stat().st_size} for path in sorted(libraries) if path.is_file()]
            report['linked_library_files'] = linked
            report['linked_library_bytes'] = sum(item['bytes'] for item in linked)
            report['linked_library_scope'] = 'ELF ldd closure only; includes OS libraries, excludes toolkit data/fonts/plugins; not clean-machine installation delta'
        (output / (language + '.json')).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: core fixtures and byte-identical XML; reports: {output}')


if __name__ == '__main__':
    main()
