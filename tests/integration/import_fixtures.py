# Synthetic SQL derived from pinned Clipy migrations; no personal clipboard data.
import sqlite3,subprocess,sys,tempfile,pathlib,hashlib,os
exe,test=sys.argv[1:];root=pathlib.Path(tempfile.mkdtemp(prefix='cliptiq-import-test-'))
def run(*args,ok=True):
 r=subprocess.run([exe,*map(str,args)],capture_output=True,text=True)
 assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
 return r
names=['Create initial tables','Create search indexes','Add createdAt to pasteboardHistories','Add OCR text to history search']
def fixture(p,v):
 d=sqlite3.connect(p)
 d.executescript('''CREATE TABLE grdb_migrations(identifier TEXT PRIMARY KEY);
 CREATE TABLE pasteboardHistories(id TEXT PRIMARY KEY,title TEXT,pasteboardTypes TEXT,deviceID TEXT,updateAt INTEGER);
 CREATE TABLE pasteboardHistoryAssets(id TEXT PRIMARY KEY,pasteboardHistoryID TEXT,"index" INTEGER,pasteboardType TEXT,data BLOB);
 CREATE TABLE pasteboardHistoryThumbnailAssets(pasteboardHistoryID TEXT PRIMARY KEY,kind TEXT,data BLOB);
 CREATE TABLE snippetFolders(id TEXT PRIMARY KEY,title TEXT,"index" INTEGER,isEnabled INTEGER);
 CREATE TABLE snippets(id TEXT PRIMARY KEY,folderID TEXT,title TEXT,content TEXT,"index" INTEGER,isEnabled INTEGER);''')
 for n in names[:v]:d.execute('INSERT INTO grdb_migrations VALUES(?)',(n,))
 if v>=2:d.execute('CREATE VIRTUAL TABLE ignored_search USING fts5(content)')
 if v>=3:d.execute('ALTER TABLE pasteboardHistories ADD COLUMN createdAt INTEGER NOT NULL DEFAULT 0')
 if v>=4:d.execute('ALTER TABLE pasteboardHistories ADD COLUMN ocrText TEXT')
 for i in range(2):
  d.execute('INSERT INTO pasteboardHistories(id,title,pasteboardTypes,deviceID,updateAt) VALUES(?,?,?,?,?)',(f'h{i}','Title','["public.utf8-plain-text"]','device',1700000000+i))
  if v>=3:d.execute('UPDATE pasteboardHistories SET createdAt=1600000000 WHERE id=?',(f'h{i}',))
  if v>=4:d.execute('UPDATE pasteboardHistories SET ocrText=? WHERE id=?',('OCR',f'h{i}'))
  d.execute('INSERT INTO pasteboardHistoryAssets VALUES(?,?,?,?,?)',(f'a{i}',f'h{i}',0,'public.utf8-plain-text',b'Original text'))
 d.execute('INSERT INTO snippetFolders VALUES(?,?,?,?)',('f','Folder',7,0));d.execute('INSERT INTO snippets VALUES(?,?,?,?,?,?)',('s','f','Title','Snippet',4,0));d.commit();d.close()
def snapshot(p):return {x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in p.parent.iterdir() if x.is_file() and x.name.startswith(p.name)}
try:
 for v in range(1,5):
  p=root/f'v{v}.sqlite3';fixture(p,v);before=snapshot(p);neutral=root/f'v{v}.neutral';run('export',p,neutral);assert snapshot(p)==before
  subprocess.run([test,str(neutral)],check=True)
  dest=root/f'v{v}.dest';run('import',neutral,dest);d=sqlite3.connect(dest)
  assert d.execute('SELECT id FROM history ORDER BY id').fetchall()==[('h0',),('h1',)]
  assert d.execute('SELECT enabled FROM snippet_states').fetchone()==(0,)
  assert d.execute('SELECT created_ms FROM history ORDER BY id').fetchone()==((1600000000 if v>=3 else 1700000000)*1000,)
  assert d.execute('SELECT data FROM representations LIMIT 1').fetchone()==(b'Original text',)
  d.close();run('export',p,neutral,ok=False)
 # Missing asset and future schema fail without publishing an interchange.
 p=root/'missing.sqlite3';fixture(p,4);d=sqlite3.connect(p);d.execute('DELETE FROM pasteboardHistoryAssets');d.commit();d.close();before=snapshot(p);run('export',p,root/'missing.neutral',ok=False);assert snapshot(p)==before and not (root/'missing.neutral').exists()
 p=root/'future.sqlite3';fixture(p,4);d=sqlite3.connect(p);d.execute("INSERT INTO grdb_migrations VALUES('Future')");d.commit();d.close();run('export',p,root/'future.neutral',ok=False)
 # Late ID conflict must roll back previously inserted history and migration tables.
 n=root/'v4.neutral';dest=root/'conflict.dest';run('import',root/'v1.neutral',dest);d=sqlite3.connect(dest);d.execute("DELETE FROM migration_receipts");d.execute("DELETE FROM history");d.commit();d.close();before=hashlib.sha256(dest.read_bytes()).hexdigest();run('import',n,dest,ok=False);assert hashlib.sha256(dest.read_bytes()).hexdigest()==before
 # Unknown neutral versions, malformed DBs and wrong source cannot mutate target.
 bad=root/'invalid';bad.write_bytes(b'not sqlite');run('import',bad,dest,ok=False)
 n2=root/'bad-version';n2.write_bytes(n.read_bytes());d=sqlite3.connect(n2);d.execute('UPDATE cliptiq_interchange SET format_version=2');d.commit();d.close();run('import',n2,dest,ok=False)
 # Malformed UTF-8, typed flags and empty IDs must never reach native UI.
 for field,value in [('title',sqlite3.Binary(b'\xff')),('isEnabled',1.5)]:
  bad=root/('typed-'+field);bad.write_bytes(n.read_bytes());d=sqlite3.connect(bad)
  if field=='title':d.execute("UPDATE snippets SET title=CAST(? AS TEXT)",(value,))
  else:d.execute('UPDATE snippets SET isEnabled=?',(value,))
  d.commit();d.close();run('import',bad,root/('typed-'+field+'.dest'),ok=False)
 # v3/v4 timestamps and nullable metadata must not be coerced by SQLite.
 for col,value in [('createdAt',None),('createdAt','invalid'),('createdAt',1.5),('deviceID',123),('ocrText',123)]:
  malformed=root/('metadata-'+col+'-'+str(value));malformed.write_bytes(n.read_bytes());d=sqlite3.connect(malformed)
  if value is None:
   d.execute('PRAGMA writable_schema=ON');d.execute("UPDATE sqlite_schema SET sql=replace(sql,'createdAt INTEGER NOT NULL','createdAt') WHERE name='pasteboardHistories'");d.commit();d.close();d=sqlite3.connect(malformed)
  if col in ('deviceID','ocrText'):d.execute('UPDATE pasteboardHistories SET '+col+'=?',(sqlite3.Binary(b'not text'),))
  else:d.execute('UPDATE pasteboardHistories SET '+col+'=?',(value,))
  d.commit();d.close();run('import',malformed,root/('meta-dest-'+col+'-'+str(value)),ok=False)
 # Settings are inert by default; nil disables, conflicts reject atomically.
 settings=root/'settings.neutral';settings.write_bytes(n.read_bytes());d=sqlite3.connect(settings)
 d.executemany('INSERT INTO migration_settings VALUES(?,?,?)',[('history_count',b'raw-count','50'),('hotkey:main',b'raw-nil',''),('excluded',b'raw-exclusions','org.example.Secret'),('type_html',b'raw-type','0')]);d.commit();d.close()
 dest2=root/'settings.dest';run('import',settings,dest2,'--apply-settings');d=sqlite3.connect(dest2)
 assert d.execute("SELECT value FROM settings WHERE key='t09.hotkey:main'").fetchone()==('',)
 assert d.execute("SELECT value FROM settings WHERE key='t09.history_count'").fetchone()==('50',);d.close()
 conflict=root/'shortcut-conflict.neutral';conflict.write_bytes(settings.read_bytes());d=sqlite3.connect(conflict)
 d.executemany('INSERT OR REPLACE INTO migration_settings VALUES(?,?,?)',[('hotkey:main',b'main','Ctrl+Shift+Q'),('hotkey:history',b'history','Ctrl+Shift+Q')]);d.commit();d.close()
 empty=root/'empty-settings.dest';run('import',conflict,empty,'--apply-settings',ok=False);d=sqlite3.connect(empty);assert d.execute('SELECT count(*) FROM history').fetchone()==(0,);d.close()
 # A legacy clip archive is entry-owned payload, not a16MiB preferences plist.
 archived=root/'large-archive.neutral';archived.write_bytes(n.read_bytes());d=sqlite3.connect(archived);d.execute('INSERT INTO migration_settings VALUES(?,?,NULL)',('legacy.archive.h0',b'A'*(17*1024*1024)));d.commit();d.close();run('import',archived,root/'large-archive.dest')
 # Unicode paths must work on Windows as well as POSIX.
 unicode=root/'данные-clipboard';unicode.write_bytes(n.read_bytes());run('import',unicode,root/'история.dest')
 # WAL source remains byte-for-byte unchanged, including sidecars.
 p=root/'wal.sqlite3';fixture(p,4);d=sqlite3.connect(p);d.execute('PRAGMA journal_mode=WAL');d.execute("UPDATE snippets SET content='In WAL'");d.commit();before=snapshot(p);run('export',p,root/'wal.neutral');assert snapshot(p)==before;d.close()
 # A hot rollback journal belongs to the snapshot; never import uncommitted rows.
 p=root/'journal.sqlite3';fixture(p,4);d=sqlite3.connect(p);d.executemany('INSERT INTO snippets VALUES(?,?,?,?,?,?)',[(f'j{i}','f','Title','Original'*2000,i+5,1) for i in range(32)]);d.commit();d.execute('PRAGMA cache_size=1');d.execute('BEGIN IMMEDIATE')
 d.execute("UPDATE snippets SET content=?",('X'*16000,));before=snapshot(p);run('export',p,root/'journal.neutral');assert snapshot(p)==before
 saved=sqlite3.connect(root/'journal.neutral');assert saved.execute("SELECT count(*) FROM snippets WHERE content LIKE 'X%'").fetchone()==(0,);saved.close();d.rollback();d.close()
 print('PASS Clipy SQLite v1-v4, duplicate preservation, states/order/times/assets, idempotence, atomic failures, WAL/source hashes')
finally:
 import shutil;shutil.rmtree(root)
