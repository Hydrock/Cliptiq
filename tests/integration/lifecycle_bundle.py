"""Inspect a staged native Mac app, then smoke EN/RU without registration/capture."""
import pathlib,plistlib,subprocess,sys
bundle=pathlib.Path(sys.argv[1])
info=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
assert info['CFBundleIdentifier']=='io.github.hydrock.Cliptiq'
assert info['CFBundleName']=='Cliptiq' and info['LSUIElement'] is True
exe=bundle/'Contents/MacOS'/info['CFBundleExecutable'];assert exe.is_file()
for language in ['en','ru']:subprocess.run([str(exe),'--smoke','--locale',language],check=True,timeout=20)
print('PASS app bundle identity and EN/RU smoke; no login registration or signing acceptance')
