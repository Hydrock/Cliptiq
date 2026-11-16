"""Remove local symbols from a staged Mach-O executable, checking code/data first.

Call before signing. The build executable must never be passed as the staged path.
"""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def sections(data):
    """Read bounded 64-bit Mach-O sections, excluding linker-owned __LINKEDIT."""
    if data[:4] == b'\xcf\xfa\xed\xfe':
        endian = '<'
    elif data[:4] == b'\xfe\xed\xfa\xcf':
        endian = '>'
    else:
        raise ValueError('expected thin 64-bit Mach-O')
    if len(data) < 32:
        raise ValueError('truncated Mach-O header')
    header = struct.unpack_from(endian + '8I', data)
    if header[3] != 2:
        raise ValueError('expected Mach-O executable')
    count, command_bytes = header[4:6]
    end = 32 + command_bytes
    if end > len(data) or count > command_bytes // 8:
        raise ValueError('invalid load-command bounds')
    result = {}
    cursor = 32
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError('truncated load command')
        command, size = struct.unpack_from(endian + 'II', data, cursor)
        if size < 8 or size % 8 or cursor + size > end:
            raise ValueError('invalid load command size')
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                raise ValueError('truncated segment command')
            segment = struct.unpack_from(endian + 'II16s4Q4I', data, cursor)
            number = segment[-2]
            if number > (size - 72) // 80:
                raise ValueError('invalid section count')
            for index in range(number):
                fields = struct.unpack_from(endian + '16s16s2Q8I', data, cursor + 72 + index * 80)
                name, segname, address, length, offset, align, _, _, flags, r1, r2, r3 = fields
                if segname.rstrip(b'\0') == b'__LINKEDIT':
                    continue
                key = (segname, name)
                if key in result:
                    raise ValueError('duplicate Mach-O section')
                # Zerofill, GB zerofill, and thread-local zerofill have no file bytes.
                if flags & 0xff in (1, 0xc, 0x12):
                    digest = None
                else:
                    if offset > len(data) or length > len(data) - offset:
                        raise ValueError('section exceeds file bounds')
                    digest = hashlib.sha256(data[offset:offset + length]).hexdigest()
                result[key] = (address, length, align, flags, r1, r2, r3, digest)
        cursor += size
    if cursor != end or not result:
        raise ValueError('invalid or empty section inventory')
    return result


def run(argv):
    return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)


def strip_staged_binary(staged, *, build_binary, runner=run):
    """Atomically replace only a staged copy after successful invariant checks."""
    staged, build_binary = Path(staged), Path(build_binary)
    if staged.is_symlink() or not staged.is_file():
        raise ValueError('staged binary must be a regular non-symlink file')
    if staged.samefile(build_binary):
        raise ValueError('staged binary must differ from build binary')
    original = staged.read_bytes()
    inventory = sections(original)
    with tempfile.TemporaryDirectory(prefix='.cliptiq-strip-', dir=staged.parent) as folder:
        candidate = Path(folder) / 'Cliptiq'
        shutil.copy2(staged, candidate)
        globals_before = runner(['xcrun', 'nm', '-g', '-U', str(candidate)])
        runner(['xcrun', 'strip', '-x', str(candidate)])
        modified = candidate.read_bytes()
        if len(modified) > len(original):
            raise RuntimeError('local-symbol stripping increased binary size')
        if sections(modified) != inventory:
            raise RuntimeError('local-symbol stripping changed code/data sections')
        if runner(['xcrun', 'nm', '-g', '-U', str(candidate)]) != globals_before:
            raise RuntimeError('local-symbol stripping changed defined global symbols')
        if staged.read_bytes() != original:
            raise RuntimeError('staged binary changed during stripping')
        candidate.replace(staged)
    return {'before_bytes': len(original), 'after_bytes': len(modified),
            'saved_bytes': len(original) - len(modified)}
