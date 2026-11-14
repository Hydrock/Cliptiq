"""Shared Win32 process counters; no UI runtime dependency."""
import ctypes as C
from ctypes import wintypes as W

kernel = C.WinDLL("kernel32", use_last_error=True)
psapi = C.WinDLL("psapi", use_last_error=True)
user = C.WinDLL("user32", use_last_error=True)
kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
kernel.OpenProcess.restype = W.HANDLE
kernel.CloseHandle.argtypes = [W.HANDLE]
kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(C.c_ulonglong)] * 4
kernel.GetProcessTimes.restype = W.BOOL
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.PostMessageW.restype = W.BOOL


class Memory(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(Memory), W.DWORD]
psapi.GetProcessMemoryInfo.restype = W.BOOL


def snapshot(handle):
    ticks = [C.c_ulonglong() for _ in range(4)]
    if not kernel.GetProcessTimes(handle, *(C.byref(t) for t in ticks)):
        raise C.WinError(C.get_last_error())
    mem = Memory()
    mem.cb = C.sizeof(mem)
    if not psapi.GetProcessMemoryInfo(handle, C.byref(mem), mem.cb):
        raise C.WinError(C.get_last_error())
    return dict(cpu_seconds=(ticks[2].value + ticks[3].value) / 1e7,
                working_set_bytes=mem.WorkingSetSize, private_bytes=mem.PrivateUsage,
                peak_working_set_bytes=mem.PeakWorkingSetSize)
