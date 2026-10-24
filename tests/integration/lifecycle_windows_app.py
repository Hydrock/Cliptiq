"""Check production PE subsystem without launching/capturing/registering anything."""
import pathlib,struct,sys
exe=pathlib.Path(sys.argv[1]);data=exe.read_bytes()
assert data[:2]==b'MZ';pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[pe:pe+4]==b'PE\0\0'
assert struct.unpack_from('<H',data,pe+24+68)[0]==2,'Production app must use Windows GUI subsystem'
print('PASS native Windows GUI subsystem; no console on login')
