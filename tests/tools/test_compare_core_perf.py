import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('compare_core_perf', ROOT / 'tools/compare-core-perf.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sample():
    return {'schema_version': 1, 'workload': 'synthetic-memory-history-v1',
            'correct': True, 'count': 1000, 'payload_bytes': 2000,
            'loaded_bytes': 2000, 'rows_observed': 30000,
            'phases': [{'name': name, 'operations': 1000, 'elapsed_ms': 2,
                        'cpu_ms': 0} for name in module.PHASES]}


class CompareCorePerfTests(unittest.TestCase):
    def test_invalid_timing_or_workload_is_rejected(self):
        for value in (float('nan'), float('inf'), -1, True, '2'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                invalid = sample()
                invalid['phases'][0]['cpu_ms'] = value
                module.validate(invalid)
        for key, value in [('correct', False), ('count', True), ('loaded_bytes', 1),
                           ('rows_observed', 1), ('workload', 'other')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                invalid = sample()
                invalid[key] = value
                module.validate(invalid)
        for change in ('missing', 'reordered', 'operations'):
            with self.subTest(change=change), self.assertRaises(ValueError):
                invalid = sample()
                if change == 'missing': invalid['phases'].pop()
                elif change == 'reordered': invalid['phases'].reverse()
                else: invalid['phases'][0]['operations'] = 1
                module.validate(invalid)

    def test_interleaving_raw_samples_metadata_and_zero_cpu(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('reference', 'candidate'):
                build = root / name
                build.mkdir()
                (build / 'core_package_benchmark').write_bytes(b'not executed')
                (build / 'CMakeCache.txt').write_text('CMAKE_CXX_FLAGS:STRING=-Oz\nPRIVATE_SECRET:STRING=excluded\n')
            calls = []

            def runner(argv, **kwargs):
                name = Path(argv[0]).parent.name
                calls.append(name)
                data = sample()
                for phase in data['phases']:
                    phase['elapsed_ms'] = 4 if name == 'candidate' else 2
                return SimpleNamespace(stdout=json.dumps(data), stderr='diagnostic')

            report = module.compare(root / 'reference', root / 'candidate', runner)
            self.assertEqual(calls[:4], ['reference', 'candidate', 'candidate', 'reference'])
            self.assertEqual(calls.count('reference'), 7)
            self.assertEqual(calls.count('candidate'), 7)
            self.assertEqual(report['phases'][0]['elapsed_ms']['candidate_over_reference'], 2)
            self.assertIsNone(report['phases'][0]['cpu_ms']['candidate_over_reference'])
            self.assertEqual(report['samples']['reference'][0]['stderr'], 'diagnostic')
            self.assertEqual(report['builds']['reference']['cmake_cache'], {'CMAKE_CXX_FLAGS:STRING': '-Oz'})

    def test_cross_run_counter_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'core_package_benchmark').write_bytes(b'not executed')
            (root / 'CMakeCache.txt').write_text('')
            calls = []

            def runner(argv, **kwargs):
                data = sample()
                if calls:
                    data['payload_bytes'] = data['loaded_bytes'] = 3000
                calls.append(1)
                return SimpleNamespace(stdout=json.dumps(data), stderr='')

            with self.assertRaisesRegex(ValueError, 'counters differ'):
                module.compare(root, root, runner)


if __name__ == '__main__':
    unittest.main()
