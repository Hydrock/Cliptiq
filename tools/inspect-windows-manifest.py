"""Extract and validate the experimental executable manifest using Windows SDK tools."""
import argparse
from pathlib import Path
import subprocess
import io
import zipfile
import xml.etree.ElementTree as ET

def escape(value):
    return value.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')

def validate_manifest(manifest):
    root = ET.fromstring(manifest)
    namespace = '{urn:schemas-microsoft-com:asm.v3}'
    levels = list(root.iter(namespace + 'requestedExecutionLevel'))
    all_levels = [node for node in root.iter() if node.tag.rsplit('}', 1)[-1] == 'requestedExecutionLevel']
    path = '/'.join(namespace + part for part in ['trustInfo', 'security', 'requestedPrivileges', 'requestedExecutionLevel'])
    expected = root.find(path)
    if len(levels) != 1 or len(all_levels) != 1 or expected is None or expected.attrib != {'level': 'asInvoker', 'uiAccess': 'false'}:
        raise ValueError('embedded manifest must retain one explicit asm.v3 asInvoker/uiAccess=false declaration')
    controls = [node for node in root.iter('{urn:schemas-microsoft-com:asm.v1}assemblyIdentity') if node.get('name') == 'Microsoft.Windows.Common-Controls']
    if len(controls) != 1 or controls[0].get('version') != '6.0.0.0' or controls[0].get('publicKeyToken') != '6595b64144ccf1df':
        raise ValueError('embedded manifest must retain Common Controls v6')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['mt.exe', '-nologo', f'-inputresource:{args.binary};#1', f'-out:{args.output}'], check=True)
    manifest = args.output.read_text(encoding='utf-8-sig')
    print('::notice title=Experimental embedded manifest::' + escape(manifest[:6000]))
    data = args.binary.read_bytes()
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        entry = zipfile.ZipInfo('Cliptiq.exe', date_time=(2026, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, data, compresslevel=9)
    print(f'::notice title=Experimental Windows executable size::bytes={len(data)} zip_bytes={len(stream.getvalue())}; executable only, not package/signature/runtime acceptance')
    validate_manifest(manifest)

if __name__ == '__main__':
    main()
