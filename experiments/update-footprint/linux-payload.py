"""Run on Linux/WSL, from repository root; packages existing C++ Base fixture only."""
import hashlib,json,platform,shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'out/update-footprint/linux';out.mkdir(parents=True,exist_ok=True)
binary=root/'out/footprint/linux-x86_64-base-gtk3/cpp/dist/probe_cpp'
if not binary.exists():raise RuntimeError('Build Linux C++ Base first')
temporary=tempfile.TemporaryDirectory(prefix='cliptiq-update-');stage=Path(temporary.name)/'package';(stage/'usr/bin').mkdir(parents=True,exist_ok=True);(stage/'usr/share/doc/cliptiq-t01').mkdir(parents=True,exist_ok=True)
shutil.copy2(binary,stage/'usr/bin/cliptiq-t01');(stage/'usr/bin/cliptiq-t01').chmod(0o755)
shutil.copy2(root/'experiments/footprint/NOTICE.txt',stage/'usr/share/doc/cliptiq-t01/copyright')
(stage/'DEBIAN').mkdir(exist_ok=True)
(stage/'DEBIAN/control').write_text('Package: cliptiq-t01\nVersion: 0.0.1\nArchitecture: amd64\nMaintainer: T01 spike <noreply@example.invalid>\nDepends: libgtk-3-0t64, libc6, libstdc++6\nDescription: Measurement fixture only; not product packaging\n')
deb=out/'cliptiq-t01_0.0.1_amd64.deb'
subprocess.run(['dpkg-deb','--root-owner-group','-Zxz','-z9','--build',str(stage),str(deb)],check=True)
tar=out/'cliptiq-t01.tar.xz'
subprocess.run(['tar','--sort=name','--mtime=@0','--owner=0','--group=0','--numeric-owner','-cJf',str(tar),'-C',str(stage),'usr'],check=True)
files=[{'name':p.name,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [binary,deb,tar]]
report={'host':platform.platform(),'scope':'existing C++ GTK3 Base full update payload; shared system dependencies excluded; no install/repository signing test','files':files,'updater_app_owned_bytes':0,'method':'distribution repository/package manager owns download/signature/install; tar lane requires explicit external/manual replacement, no bundled updater','delta_bytes':None,'delta_reason':'No two real releases or binary patch/apply/verification implementation; tiny deltas are not claimed','package_tool':subprocess.check_output(['dpkg-deb','--version'],text=True).splitlines()[0]}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
