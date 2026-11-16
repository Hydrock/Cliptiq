import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('native_size_profile', ROOT / 'tools/native-size-profile.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NativeSizeProfileTests(unittest.TestCase):
    def test_strip_uses_independent_copies_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'input with spaces'
            original = b'native executable symbols'
            binary.write_bytes(original)
            calls = []

            def runner(argv):
                calls.append(argv)
                copy = Path(argv[-1])
                self.assertNotEqual(copy, binary)
                if argv[1] == 'strip':
                    self.assertEqual(copy.read_bytes(), original)
                    copy.write_bytes(original[:-len(argv[2:-1])])
                return {'returncode': 0, 'output': 'LC_SYMTAB', 'argv': argv}

            report = module.profile(binary, runner)
            self.assertEqual(binary.read_bytes(), original)
            self.assertTrue(report['input_unchanged'])
            self.assertEqual([row['bytes'] for row in report['variants']], [25, 24, 24, 23, 23])
            self.assertEqual([call[2:-1] for call in calls if call[1] == 'strip'], [['-x'], ['-S'], ['-x', '-S'], ['-u', '-r']])
            self.assertTrue(report['variants'][-1]['undefined_symbols_unchanged'])
            self.assertTrue(all(not Path(call[-1]).exists() for call in calls))

    def test_archive_measurement_is_repeatable(self):
        self.assertEqual(module.zipped_bytes(b'a' * 4096), module.zipped_bytes(b'a' * 4096))

    def test_changed_imports_are_reported_without_touching_input(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'binary'
            binary.write_bytes(b'input')

            def runner(argv):
                output = 'symbols'
                if argv[1:3] == ['nm', '-u'] and Path(argv[-1]).parent.name == 'strip-executable':
                    output = 'missing symbols'
                return {'returncode': 0, 'output': output}

            report = module.profile(binary, runner)
            self.assertFalse(report['variants'][-1]['undefined_symbols_unchanged'])
            self.assertEqual(binary.read_bytes(), b'input')

    def test_strip_failure_remains_visible(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'binary'
            binary.write_bytes(b'original')
            report = module.profile(binary, lambda argv: {'returncode': 1, 'output': 'tool failure'})
            self.assertEqual(report['variants'][1]['strip']['returncode'], 1)
            self.assertEqual(report['variants'][1]['bytes'], len(b'original'))


if __name__ == '__main__':
    unittest.main()
