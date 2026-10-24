"""Security invariant: importing must not expand archived images or pre-read unbounded preferences."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[2]
def read(path):
 if len(sys.argv)>1:return subprocess.check_output(['git','show',sys.argv[1]+':'+path],cwd=root,text=True)
 return (root/path).read_text(encoding='utf-8')
swift=read('tools/import-clipy/legacy.swift');prefs=read('tools/import-clipy/main.mm')
assert 'tiffRepresentation' not in swift,'Legacy import must not expand an NSImage to TIFF before limits'
assert 'decodeObject(of:NSImage.self' not in swift,'Legacy NSImage must be mapped to inert archive reader'
assert 'dataWithContentsOfURL' not in prefs,'Preferences must use bounded regular-file reader'
assert '40000000/height' in swift and 'CGImageSourceCreateWithData' in swift
print('PASS no unbounded image expansion/whole-file preferences reader in migration')
