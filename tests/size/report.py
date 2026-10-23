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
    'build_type': 'MinSizeRel', 'windows_crt': 'static' if system == 'Windows' else None,
    'probe_bytes': len(data), 'probe_zip_bytes': len(archive.getvalue()),
    'probe_sha256': hashlib.sha256(data).hexdigest(),
    'static_archives_bytes': {p.name: p.stat().st_size for p in folder.iterdir() if p.suffix in ('.a', '.lib')},
    'full_product_bytes': None, 'offline_dependencies_bytes': None,
}
(root / 'out' / 't02-size.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
