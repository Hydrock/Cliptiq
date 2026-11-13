"""macOS process counters; CPU counters are Mach absolute-time ticks, not ns."""
import ctypes

class Usage(ctypes.Structure):
    _fields_=[('uuid',ctypes.c_uint8*16)]+[(name,ctypes.c_uint64) for name in (
        'user_ticks','system_ticks','idle_wakeups','interrupt_wakeups','pageins','wired_bytes',
        'rss_bytes','footprint_bytes','start','exit','child_user','child_system',
        'child_idle','child_interrupt','child_pageins','child_elapsed','read_bytes','written_bytes')]
class Timebase(ctypes.Structure):
    _fields_=[('numer',ctypes.c_uint32),('denom',ctypes.c_uint32)]

lib=ctypes.CDLL('/usr/lib/libproc.dylib',use_errno=True)
lib.proc_pid_rusage.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_void_p]
lib.proc_pid_rusage.restype=ctypes.c_int
system=ctypes.CDLL('/usr/lib/libSystem.B.dylib')
system.mach_timebase_info.argtypes=[ctypes.POINTER(Timebase)]
system.mach_timebase_info.restype=ctypes.c_int
timebase=Timebase()
if system.mach_timebase_info(ctypes.byref(timebase)) or not timebase.denom:
    raise RuntimeError('Cannot read Mach timebase')

def read_usage(pid):
    value=Usage()
    if lib.proc_pid_rusage(pid,2,ctypes.byref(value))!=0:
        raise OSError(ctypes.get_errno(),'proc_pid_rusage failed')
    ticks=value.user_ticks+value.system_ticks
    return dict(cpu_ticks=ticks,cpu_seconds=ticks*timebase.numer/timebase.denom/1e9,
                rss_bytes=value.rss_bytes,footprint_bytes=value.footprint_bytes,
                idle_wakeups=value.idle_wakeups,interrupt_wakeups=value.interrupt_wakeups,
                read_bytes=value.read_bytes,written_bytes=value.written_bytes,start_identity=value.start)
