"""Install a local Mac build with a persistent signing identity.

Never falls back to ad-hoc signing. The installed app's designated requirement
must accept the staged replacement, unless an explicit one-time migration is
requested. User profile, Keychain items and other applications' rights are kept.
"""
import argparse
import datetime
import os
import pathlib
import plistlib
import signal
import subprocess
import tempfile
import time

BUNDLE_ID = 'io.github.hydrock.Cliptiq'
DESTINATION = pathlib.Path('/Applications/Cliptiq.app')
LSREGISTER = '/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister'


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def requirement(app):
    result = run('/usr/bin/codesign', '-d', '-r-', str(app), capture_output=True)
    for line in (result.stdout + result.stderr).splitlines():
        if line.startswith('designated => '):
            return line.removeprefix('designated => ')
        if line.startswith('# designated => '):
            return line.removeprefix('# designated => ')
    raise RuntimeError('App has no designated requirement')


def validate_bundle(app):
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('CFBundleIdentifier') != BUNDLE_ID or info.get('CFBundleExecutable') != 'Cliptiq':
        raise RuntimeError('Wrong or incomplete app bundle; configure CMake before rebuilding')
    if not (app / 'Contents/MacOS/Cliptiq').is_file():
        raise RuntimeError('App executable missing')


def stop_installed_app():
    target = str(DESTINATION / 'Contents/MacOS/Cliptiq')
    result = run('/bin/ps', '-axo', 'pid=,comm=', capture_output=True)
    for line in result.stdout.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) != 2 or fields[1] != target:
            continue
        pid = int(fields[0])
        os.kill(pid, signal.SIGTERM)
        for _ in range(100):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Cliptiq did not stop; installation cancelled')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = pathlib.Path(__file__).resolve().parents[1]
    parser.add_argument('--app', type=pathlib.Path, default=root / 'out/core-Darwin-release/Cliptiq.app')
    parser.add_argument('--identity', default=os.environ.get('CLIPTIQ_MACOS_SIGNING_IDENTITY', 'Cliptiq Local Development'))
    parser.add_argument('--migrate-identity', action='store_true', help='Allow a one-time identity change and reset only Cliptiq Accessibility')
    parser.add_argument('--check-only', action='store_true', help='Stage, sign and validate without changing the installed app')
    parser.add_argument('--no-launch', action='store_true')
    args = parser.parse_args()
    if args.identity.strip() in ('', '-'):
        parser.error('A persistent certificate is required; ad-hoc signing is not supported')
    source = args.app.resolve()
    if source == DESTINATION:
        parser.error('Source must be a separate build; never re-sign a running installed app')
    validate_bundle(source)
    with tempfile.TemporaryDirectory(prefix='cliptiq-install-') as folder:
        staged = pathlib.Path(folder) / 'Cliptiq.app'
        run('/usr/bin/ditto', str(source), str(staged))
        run('/usr/bin/codesign', '--force', '--sign', args.identity, '--timestamp=none', str(staged))
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(staged))
        new_requirement = requirement(staged)
        if 'certificate' not in new_requirement or 'cdhash' in new_requirement:
            raise RuntimeError('Signing identity is not stable across builds')
        if DESTINATION.exists() and not args.migrate_identity:
            old_requirement = requirement(DESTINATION)
            run('/usr/bin/codesign', '--verify', '--strict', '-R=' + old_requirement, str(staged))
            print('Replacement satisfies the installed identity; existing grants can be retained.')
        print('Verified persistent signature:', new_requirement)
        if args.check_only:
            return
        stop_installed_app()
        backup = None
        if DESTINATION.exists():
            run(LSREGISTER, '-u', str(DESTINATION))
            trash = pathlib.Path.home() / '.Trash/cliptiq-removed.noindex' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            trash.mkdir(parents=True)
            backup = trash / 'Cliptiq.app'
            DESTINATION.rename(backup)
            print('Previous app removed from Applications and moved to Trash.')
        try:
            run('/usr/bin/ditto', str(staged), str(DESTINATION))
            validate_bundle(DESTINATION)
            run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(DESTINATION))
        except Exception:
            # Keep a failed replacement for inspection instead of permanently deleting it.
            if DESTINATION.exists():
                failed_dir = pathlib.Path.home() / '.Trash/cliptiq-removed.noindex' / ('failed-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
                failed_dir.mkdir(parents=True)
                DESTINATION.rename(failed_dir / 'Cliptiq.app')
            if backup:
                backup.rename(DESTINATION)
                run(LSREGISTER, '-f', str(DESTINATION))
            raise
        # Build bundles and old Trash copies must not win bundle-ID resolution.
        for build in (root / 'out').glob('core-Darwin-*/Cliptiq.app'):
            subprocess.run([LSREGISTER, '-u', str(build)], check=False, capture_output=True)
        run(LSREGISTER, '-f', str(DESTINATION))
        if args.migrate_identity:
            run('/usr/bin/tccutil', 'reset', 'Accessibility', BUNDLE_ID)
            print('One-time identity migration: grant Cliptiq access again in System Settings.')
        if not args.no_launch:
            run('/usr/bin/open', str(DESTINATION))
        print('Installed signed Cliptiq. Profile and LAN credentials retained.')


if __name__ == '__main__':
    main()
