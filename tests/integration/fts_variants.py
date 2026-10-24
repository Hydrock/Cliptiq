"""Run separately linked Base/indexed binaries against the same profile."""
import pathlib,subprocess,sys,tempfile
base,indexed=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='cliptiq-fts-variants-') as folder:
 db=pathlib.Path(folder)/'history.sqlite3';index=db.parent/'index/search.db'
 def run(binary,mode):subprocess.run([str(binary.resolve()),str(db),mode],check=True)
 run(base,'seed');run(indexed,'index');assert index.exists()
 run(base,'base');assert not index.exists()
 run(indexed,'index');run(base,'clear');assert not index.exists()
 run(indexed,'empty')
 # Simulate a committed main change with a stale companion left by a crash.
 import sqlite3
 from contextlib import closing
 with closing(sqlite3.connect(db)) as connection:
  connection.execute('UPDATE store_state SET data_revision=data_revision+1');connection.commit()
 run(indexed,'empty')
print('indexed -> Base -> indexed, Base clear, crash recovery: pass')
