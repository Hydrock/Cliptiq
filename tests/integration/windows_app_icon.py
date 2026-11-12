"""Read the production EXE resources as data; never launch the application."""
import ctypes as C
from ctypes import wintypes as W
import struct
import sys
kernel=C.WinDLL('kernel32',use_last_error=True)
kernel.LoadLibraryExW.argtypes=[W.LPCWSTR,W.HANDLE,W.DWORD]
kernel.LoadLibraryExW.restype=W.HMODULE
kernel.FindResourceW.argtypes=[W.HMODULE,C.c_void_p,C.c_void_p]
kernel.FindResourceW.restype=W.HRSRC
kernel.LoadResource.argtypes=[W.HMODULE,W.HRSRC]
kernel.LoadResource.restype=W.HGLOBAL
kernel.LockResource.argtypes=[W.HGLOBAL]
kernel.LockResource.restype=C.c_void_p
kernel.SizeofResource.argtypes=[W.HMODULE,W.HRSRC]
kernel.SizeofResource.restype=W.DWORD
kernel.FreeLibrary.argtypes=[W.HMODULE]
module=kernel.LoadLibraryExW(str(__import__('pathlib').Path(sys.argv[1]).resolve()),None,2)
assert module,'EXE resource load failed'
try:
    resource=kernel.FindResourceW(module,1,14)
    assert resource,'Application icon group is missing'
    data=C.string_at(kernel.LockResource(kernel.LoadResource(module,resource)),kernel.SizeofResource(module,resource))
    reserved,kind,count=struct.unpack_from('<HHH',data)
    assert reserved==0 and kind==1 and count>=3
    sizes={data[6+14*i] or 256 for i in range(count)}
    assert {16,24,32,48,64}<=sizes,sizes
    print('PASS embedded application icon sizes:',sorted(sizes))
finally:
    kernel.FreeLibrary(module)
