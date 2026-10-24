"""Fetch pinned measurement-only SQLite; verify archive before extraction."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
URL = 'https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip'
SHA256 = '1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d'


def main():
    folder = ROOT / '.toolchains' / 'downloads'
    folder.mkdir(parents=True, exist_ok=True)
    archive = folder / 'sqlite.zip'
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            archive.write_bytes(response.read(8 * 1024 * 1024))
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit('SQLite archive SHA256 mismatch; refusing extraction')
    target = ROOT / '.toolchains' / 'sqlite'
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for name in ('sqlite3.c', 'sqlite3.h'):
            data = source.read('sqlite-amalgamation-3530400/' + name)
            (target / name).write_bytes(data)
    print(target)


if __name__ == '__main__':
    main()
