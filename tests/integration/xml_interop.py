"""Independent XML1.0 parser/serializer comparison; not live Clipy acceptance."""
import pathlib,sys,tempfile,subprocess,xml.etree.ElementTree as ET
probe=pathlib.Path(sys.argv[1]);fixture=pathlib.Path(sys.argv[2])
def model(path):
 root=ET.parse(path).getroot();assert root.tag=='folders'
 return [(f.findtext('title') or 'untitled folder',[(s.findtext('title') or 'untitled snippet',s.findtext('content') or '') for s in f.findall('snippets/snippet')]) for f in root.findall('folder')]
with tempfile.TemporaryDirectory(prefix='cliptiq-xml-interop-') as directory:
 d=pathlib.Path(directory);out=d/'out.xml';again=d/'again.xml';generated=d/'generated.xml'
 subprocess.run([str(probe),'--roundtrip',str(fixture),str(out)],check=True)
 assert model(out)==model(fixture)
 root=ET.parse(fixture).getroot();ET.ElementTree(root).write(generated,encoding='utf-8',xml_declaration=True)
 subprocess.run([str(probe),'--roundtrip',str(generated),str(again)],check=True)
 # ElementTree emits literal CR (then normalizes it to LF on reparse).
 # Compare each serializer against the semantic model of its actual wire;
 # the first fixture→C++ check above independently verifies numeric CR retention.
 assert model(again)==model(generated)
 assert model(out)[0][1][0][1]=="  first\nsecond\rlast  "
 print('PASS independent ElementTree reads C++ output and C++ reads ElementTree output; whitespace/Unicode/entities/CDATA/fallback/order')
