import importlib.util
import json
from pathlib import Path
import unittest


class RuntimeSummaryTests(unittest.TestCase):
    def test_summary_does_not_mutate_or_reference_itself(self):
        path = Path(__file__).resolve().parents[2] / 'tools/build-footprint.py'
        spec = importlib.util.spec_from_file_location('build_footprint', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        samples = [dict(startup_ms=30, idle_memory_bytes=10), dict(startup_ms=10, idle_memory_bytes=20), dict(startup_ms=20, idle_memory_bytes=30)]
        summary = module.summarize_samples(samples)
        self.assertEqual(summary['startup_ms'], 20)
        self.assertEqual(summary['idle_memory_bytes'], 20)
        self.assertNotIn('samples', samples[1])
        self.assertEqual(len(json.loads(json.dumps(summary))['samples']), 3)
