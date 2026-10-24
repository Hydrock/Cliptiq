"""T01 update delivery spike; all generated files stay in out/update-footprint."""
import hashlib, importlib.util, json, os, platform, shutil, subprocess, sys, tarfile, urllib.request, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'out/update-footprint'/platform.system().lower()
OUT.mkdir(parents=True,exist_ok=True)
def run(args,env=None,check=True):
    args=list(map(str,args));args[0]=shutil.which(args[0],path=(env or os.environ).get('PATH')) or args[0]
    p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True)
    with (OUT/'commands.log').open('a',encoding='utf-8') as f:f.write('$ '+' '.join(map(str,args))+'\n'+p.stdout+p.stderr+'\n')
    if check and p.returncode:raise RuntimeError(p.stderr or p.stdout)
    return p
def inventory(path):
    return [{'path':str(p.relative_to(path)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(path.rglob('*')) if p.is_file() and not p.is_symlink()]
def size(path):return sum(x['bytes'] for x in inventory(path))
def windows():
    spec=importlib.util.spec_from_file_location('base',ROOT/'tools/build-footprint.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    env,_=base.visual_studio_env(); result={'scope':'Standalone native HTTPS staging/AuthentiCode certificate pin gate; no installer execution, UI, feed parser, rollback or production signing','variants':{}}
    for crt in ['MT','MD']:
        folder=OUT/crt;folder.mkdir(exist_ok=True)
        exe=folder/'update-helper.exe'
        run(['cl','/nologo','/O1','/Gy','/Gw','/'+crt,ROOT/'experiments/update-footprint/windows.cpp','/Fe:'+str(exe),'/Fo:'+str(folder/'helper.obj'),'/link','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO'],env)
        imports=run(['dumpbin','/DEPENDENTS',exe],env).stdout
        (folder/'dependencies.txt').write_text(imports)
        unsigned=run([exe,'--verify',exe,'0'*64],env,False)
        if unsigned.returncode!=1:raise RuntimeError('unsigned helper unexpectedly accepted')
        result['variants'][crt]={'exe_bytes':exe.stat().st_size,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'unsigned_rejection':unsigned.stdout.strip(),'imports':imports}
        with zipfile.ZipFile(folder/'helper.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:z.write(exe,exe.name)
        result['variants'][crt]['zip_bytes']=(folder/'helper.zip').stat().st_size
        if crt=='MD':
            redist=next((Path(env['VCTOOLSREDISTDIR'])/'x64').glob('Microsoft.VC*.CRT'))
            dlls=[p for p in redist.glob('*.dll') if p.name.lower() in imports.lower()]
            if not dlls:raise RuntimeError('dynamic CRT closure was not found')
            result['variants'][crt]['direct_crt_files']=[{'name':p.name,'bytes':p.stat().st_size} for p in dlls]
            result['variants'][crt]['exe_plus_direct_crt_bytes']=exe.stat().st_size+sum(p.stat().st_size for p in dlls)
            signed=dlls[0]
            powershell=shutil.which('pwsh') or 'powershell.exe'
            command="$s=Get-AuthenticodeSignature -LiteralPath '"+str(signed).replace("'","''")+"'; $s.SignerCertificate.GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256)"
            pin=run([powershell,'-NoProfile','-Command',command]).stdout.strip()
            good=run([exe,'--verify',signed,pin]);bad=run([exe,'--verify',signed,'0'*64],check=False)
            if bad.returncode!=1:raise RuntimeError('wrong publisher pin accepted')
            result['signature_gate_checks']={'fixture':str(signed),'valid_signature_correct_pin':good.stdout.strip(),'valid_signature_wrong_pin':bad.stdout.strip(),'note':'Microsoft embedded signed CRT DLL, not Cliptiq release; catalog-only OS files are not supported by this spike'}
    result['toolchain']=env.get('VCTOOLSVERSION');return result
def macos():
    version='2.10.0';digest='c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c'
    archive=OUT/'Sparkle.tar.xz'
    if not archive.exists():urllib.request.urlretrieve(f'https://github.com/sparkle-project/Sparkle/releases/download/{version}/Sparkle-{version}.tar.xz',archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:raise RuntimeError('Sparkle digest mismatch')
    unpack=OUT/'distribution';unpack.mkdir(exist_ok=True)
    # Pinned official archive contains intentional framework symlinks.
    with tarfile.open(archive) as t:t.extractall(unpack,filter='fully_trusted')
    framework=next(unpack.rglob('Sparkle.framework'))
    arch=platform.machine();result={'sparkle_version':version,'release_sha256':digest,'architecture':arch,'distribution_bytes':archive.stat().st_size,'framework_original_files':inventory(framework),'original_signature':run(['codesign','--verify','--deep','--strict',framework],check=False).returncode,'variants':{}}
    source=OUT/'source';shutil.copytree(ROOT/'experiments/footprint',source,dirs_exist_ok=True)
    host=source/'native_macos.mm';text=host.read_text();text='#ifdef UPDATE_SPIKE\n#import <Sparkle/Sparkle.h>\nstatic SPUStandardUpdaterController* updater;\n#endif\n'+text
    text=text.replace('[NSApplication sharedApplication];','[NSApplication sharedApplication];\n#ifdef UPDATE_SPIKE\n updater=[[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:nil userDriverDelegate:nil];\n if(!updater) return false;\n#endif')
    host.write_text(text)
    with (source/'CMakeLists.txt').open('a') as f:f.write('\nif(UPDATE_SPIKE)\n target_compile_definitions(probe_cpp PRIVATE UPDATE_SPIKE)\n target_compile_options(probe_cpp PRIVATE "-F${SPARKLE_PARENT}")\n target_link_options(probe_cpp PRIVATE "-F${SPARKLE_PARENT}" "-framework" "Sparkle" "-Wl,-rpath,@executable_path/../Frameworks")\nendif()\n')
    for variant in ['base','sparkle-full','sparkle-no-xpc']:
        build=OUT/('build-'+variant);app=OUT/variant/'Cliptiq.app';contents=app/'Contents';(contents/'MacOS').mkdir(parents=True,exist_ok=True)
        run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=MinSizeRel',f'-DSQLITE_SOURCE={ROOT/".toolchains/sqlite"}',f'-DUPDATE_SPIKE={"OFF" if variant=="base" else "ON"}',f'-DSPARKLE_PARENT={framework.parent}'])
        run(['cmake','--build',build,'--target','probe_cpp','--parallel','2'])
        shutil.copy2(build/'probe_cpp',contents/'MacOS/Cliptiq')
        import plistlib
        (contents/'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Cliptiq','CFBundleIdentifier':'org.cliptiq.t01','CFBundleVersion':'1','CFBundlePackageType':'APPL','SUFeedURL':'https://example.invalid/appcast.xml'}))
        resources=contents/'Resources';resources.mkdir(exist_ok=True);shutil.copy2(ROOT/'experiments/footprint/NOTICE.txt',resources/'NOTICE.txt')
        if variant!='base':
            target=contents/'Frameworks/Sparkle.framework';shutil.copytree(framework,target,symlinks=True,dirs_exist_ok=True)
            shutil.copy2(unpack/'LICENSE',resources/'Sparkle-LICENSE.txt')
            # Keep every shipped helper/resource; trim only CPU slices, not symbols/locales.
            for p in target.rglob('*'):
                if not p.is_symlink() and p.is_file() and 'Mach-O' in run(['file',p]).stdout:
                    arches=run(['lipo','-archs',p]).stdout.split()
                    if len(arches)>1:
                        temporary=p.with_name(p.name+'.thin');run(['lipo',p,'-thin',arch,'-output',temporary]);shutil.copymode(p,temporary);temporary.replace(p)
            if variant=='sparkle-no-xpc':
                xpc=target/'Versions/B/XPCServices'
                if xpc.exists():shutil.rmtree(xpc)
                link=target/'XPCServices'
                if link.is_symlink():link.unlink()
            # Sign nested code inside-out, ad hoc ONLY; never Developer ID/notarization evidence.
            for p in sorted(target.rglob('*'),key=lambda p:len(p.parts),reverse=True):
                if not p.is_symlink() and p.is_file() and run(['file',p]).stdout.find('Mach-O')>=0:run(['codesign','--force','--sign','-',p])
            for p in sorted(target.rglob('*'),key=lambda p:len(p.parts),reverse=True):
                if p.suffix in ['.app','.xpc']:run(['codesign','--force','--sign','-',p])
            run(['codesign','--force','--sign','-',target])
        run(['codesign','--force','--sign','-',app]);run(['codesign','--verify','--deep','--strict',app])
        run([contents/'MacOS/Cliptiq','--lifecycle-smoke'])
        packed=OUT/(variant+'.zip');run(['ditto','-c','-k','--keepParent',app,packed])
        result['variants'][variant]={'app_bytes':size(app),'zip_bytes':packed.stat().st_size,'files':inventory(app),'signature':'ad hoc; no Developer ID, notarization, actual update/install or signed feed test'}
    base=result['variants']['base']
    for v in result['variants'].values():v['app_delta_bytes']=v['app_bytes']-base['app_bytes'];v['zip_delta_bytes']=v['zip_bytes']-base['zip_bytes']
    return result
if __name__=='__main__':
    report=windows() if platform.system()=='Windows' else macos()
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ['framework_original_files']},indent=2))
