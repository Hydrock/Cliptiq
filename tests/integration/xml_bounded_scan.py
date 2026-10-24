"""Adversarial below-limit file must finish in a practical bounded time."""
import pathlib,subprocess,sys,tempfile,time
with tempfile.TemporaryDirectory(prefix='cliptiq-xml-linear-') as folder:
 root=pathlib.Path(folder);source=root/'many-segments.xml';output=root/'out.xml'
 prefix=b'<folders><folder><snippets><snippet><content>'
 suffix=b'</content></snippet></snippets></folder><!--'+b'z'*(63*1024*1024)+b'--></folders>'
 source.write_bytes(prefix+b'x<!---->'*37000+suffix)
 assert source.stat().st_size<64*1024*1024
 start=time.monotonic()
 try:subprocess.run([sys.argv[1],'--roundtrip',str(source),str(output)],check=True,timeout=20)
 except subprocess.TimeoutExpired:raise AssertionError('below-limit XML repeated whole-suffix scan exceeded20s') from None
 assert output.read_bytes().count(b'x')>=37000
 print('PASS adversarial37000 text segments +63MiB suffix completed in',round(time.monotonic()-start,3),'seconds;20s fixture gate, not throughput guarantee')
