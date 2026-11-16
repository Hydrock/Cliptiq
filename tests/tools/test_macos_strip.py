import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('macos_strip', ROOT / 'tools/macos_strip.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def executable():
    header = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, 1, 152, 0, 0)
    segment = struct.pack('<II16s4Q4I', 0x19, 152, b'__TEXT', 0, 4096, 0, 188, 7, 5, 1, 0)
    section = struct.pack('<16s16s2Q8I', b'__text', b'__TEXT', 184, 4, 184, 2, 0, 0, 0, 0, 0, 0)
    return header + segment + section + b'CODE' + b'local-symbol-table'


class MacosStripTests(unittest.TestCase):
    def fixture(self, folder):
        build, staged = Path(folder) / 'build', Path(folder) / 'staged'
        build.write_bytes(executable())
        staged.write_bytes(executable())
        return build, staged

    def test_only_verified_staged_copy_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            build, staged = self.fixture(folder)
            calls = []

            def runner(argv):
                calls.append(argv)
                self.assertNotIn(str(staged), argv)
                self.assertNotIn(str(build), argv)
                if argv[1] == 'strip':
                    self.assertEqual(argv[2], '-x')
                    p = Path(argv[-1])
                    p.write_bytes(p.read_bytes()[:188])
                return '00000000 T _main\n'

            result = module.strip_staged_binary(staged, build_binary=build, runner=runner)
            self.assertEqual(build.read_bytes(), executable())
            self.assertEqual(staged.read_bytes(), executable()[:188])
            self.assertEqual(result['saved_bytes'], len(b'local-symbol-table'))
            self.assertEqual(len(calls), 3)
            self.assertTrue(all(not Path(call[-1]).exists() for call in calls))

    def test_unexpected_section_or_global_changes_abort_without_replacement(self):
        for change in ('code', 'globals', 'tool-error'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                build, staged = self.fixture(folder)
                nm_calls = []

                def runner(argv):
                    if argv[1] == 'strip':
                        if change == 'tool-error':
                            raise RuntimeError('strip failed')
                        p = Path(argv[-1])
                        data = bytearray(p.read_bytes())
                        if change == 'code':
                            data[184] ^= 1
                        p.write_bytes(data)
                    else:
                        nm_calls.append(1)
                        return str(len(nm_calls)) if change == 'globals' else 'globals'
                    return ''

                with self.assertRaises(RuntimeError):
                    module.strip_staged_binary(staged, build_binary=build, runner=runner)
                self.assertEqual(staged.read_bytes(), executable())
                self.assertEqual(build.read_bytes(), executable())

    def test_build_path_cannot_be_stripped(self):
        with tempfile.TemporaryDirectory() as folder:
            build, _ = self.fixture(folder)
            with self.assertRaises(ValueError):
                module.strip_staged_binary(build, build_binary=build)

    def test_malformed_bounds_are_rejected(self):
        for data in (b'', executable()[:31], executable()[:50], executable()[:187]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                module.sections(data)
        data = bytearray(executable())
        struct.pack_into('<I', data, 36, 0xffffffff)
        with self.assertRaises(ValueError):
            module.sections(data)


if __name__ == '__main__':
    unittest.main()
