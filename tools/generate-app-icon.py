"""Package the Windows application artwork into ICNS without changing pixels.

The checked-in ICO is the shared source. Run after updating it; --check verifies
that the macOS resource is current. Uses only the Python standard library.
"""
import argparse
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parents[1]


def mac_icon():
    ico = (root / 'resources/windows/cliptiq.ico').read_bytes()
    reserved, kind, count = struct.unpack_from('<HHH', ico)
    assert (reserved, kind) == (0, 1)
    chunks = {}
    types = {16: b'icp4', 32: b'icp5', 64: b'icp6'}
    for index in range(count):
        width, height, _, _, _, _, length, offset = struct.unpack_from(
            '<BBBBHHII', ico, 6 + 16 * index)
        width, height = width or 256, height or 256
        if width == height and width in types:
            png = ico[offset:offset + length]
            assert png.startswith(b'\x89PNG\r\n\x1a\n')
            assert len(png) == length
            chunks[width] = types[width] + struct.pack('>I', length + 8) + png
    assert set(chunks) == set(types), 'Missing shared icon sizes'
    payload = b''.join(chunks[size] for size in sorted(chunks))
    return b'icns' + struct.pack('>I', len(payload) + 8) + payload


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = root / 'resources/macos/cliptiq.icns'
    expected = mac_icon()
    if args.check:
        assert target.read_bytes() == expected, 'Run tools/generate-app-icon.py'
        print('PASS macOS and Windows application artwork matches exactly')
    else:
        target.write_bytes(expected)
