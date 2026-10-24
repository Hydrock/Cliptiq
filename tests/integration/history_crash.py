"""Exercise process interruption and rollback-journal recovery with real SQLite."""
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import closing

exe = pathlib.Path(sys.argv[1]).resolve()
for mode in ['--interrupt', '--interrupt-wal']:
    with tempfile.TemporaryDirectory(prefix='cliptiq-crash-') as folder:
        path = pathlib.Path(folder) / 'history.db'
        subprocess.run([str(exe), '--seed', str(path)], check=True)
        result = subprocess.run([str(exe), mode, str(path)])
        assert result.returncode == 23, result.returncode
        journal = path.with_name(path.name + ('-journal' if mode == '--interrupt' else '-wal'))
        assert journal.exists() and journal.stat().st_size > (512 if mode == '--interrupt' else 32)
        with closing(sqlite3.connect(path)) as db:
            assert db.execute('SELECT count(*) FROM history').fetchone()[0] == 1
            assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
            assert db.execute('SELECT count(*) FROM representations').fetchone()[0] == 1
        # Read through the production core after hot-journal recovery; add/replace
        # the same content and verify the original entry remains intact.
        subprocess.run([str(exe), '--seed', str(path)], check=True)
        with closing(sqlite3.connect(f'file:{path.as_posix()}?mode=ro', uri=True)) as db:
            assert db.execute('SELECT count(*) FROM history').fetchone()[0] == 1
print('PASS process interruption rollback/WAL and reopen')
