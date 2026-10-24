import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile


TOOL = Path(__file__).resolve().parents[2] / 'tools' / 'size-report.py'


class SizeReportTests(unittest.TestCase):
    def module(self):
        spec = importlib.util.spec_from_file_location('size_report', TOOL)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_counts_installed_bytes_and_roundtrip_archive(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist = root / 'dist'
            dist.mkdir()
            (dist / 'app.exe').write_bytes(b'A' * 4096)
            (dist / 'notice.txt').write_text('license', encoding='utf-8')
            report = module.measure(dist, root / 'app.zip', target='windows', arch='x86_64')
            self.assertEqual(report['app_bytes'], 4103)
            self.assertGreater(report['archive_bytes'], 0)
            self.assertLess(report['archive_bytes'], report['app_bytes'])
            with zipfile.ZipFile(root / 'app.zip') as archive:
                self.assertEqual(archive.read('app.exe'), b'A' * 4096)

    def test_unknown_dependency_cost_is_not_zero(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'app').write_bytes(b'app')
            report = module.measure(root, root.parent / (root.name + '.zip'), target='test', arch='x64')
            self.addCleanup((root.parent / (root.name + '.zip')).unlink, missing_ok=True)
            self.assertIsNone(report['dependency_delta_bytes'])
            self.assertIsNone(report['total_offline_bytes'])
            self.assertIsNone(report['idle_memory_bytes'])

    def test_archive_inside_dist_is_rejected(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'app').write_bytes(b'app')
            with self.assertRaises(ValueError):
                module.measure(root, root / 'recursive.zip', target='test', arch='x64')

    def test_dependency_ocr_cost_and_budget(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist = root / 'dist'
            dist.mkdir()
            (dist / 'app').write_bytes(b'app')
            report = module.measure(dist, root / 'app.zip', target='test', arch='x64', dependency_delta_bytes=20, ocr_bytes=40)
            self.assertEqual(report['total_offline_bytes'], 63)
            self.assertEqual(module.budget_violations(report, {'app_bytes': 2}), ['app_bytes: 3 > 2'])
            self.assertEqual(module.budget_violations(report, {'app_bytes': 3}), [])

    def test_symlink_is_not_silently_followed(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist = root / 'dist'
            dist.mkdir()
            outside = root / 'secret'
            outside.write_bytes(b'secret')
            try:
                (dist / 'link').symlink_to(outside)
            except OSError:
                self.skipTest('symlink creation not permitted')
            with self.assertRaises(ValueError):
                module.measure(dist, root / 'app.zip', target='test', arch='x64')

    def test_unknown_ocr_pack_keeps_offline_total_unknown(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist = root / 'dist'
            dist.mkdir()
            (dist / 'app').write_bytes(b'app')
            report = module.measure(dist, root / 'app.zip', target='test', arch='x64', dependency_delta_bytes=20, ocr_bytes=None)
            self.assertIsNone(report['ocr_bytes'])
            self.assertIsNone(report['total_offline_bytes'])


class InternalLinkTests(unittest.TestCase):
    def test_internal_relative_symlink_is_preserved_without_counting_target_twice(self):
        spec=importlib.util.spec_from_file_location('size_report',TOOL);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);dist=root/'dist';dist.mkdir();(dist/'real').write_bytes(b'abc')
            try:(dist/'alias').symlink_to('real')
            except OSError:self.skipTest('symlink privilege unavailable')
            r=m.measure(dist,root/'app.zip',target='linux',arch='x86_64');self.assertEqual(r['app_bytes'],7)
            with zipfile.ZipFile(root/'app.zip') as z:self.assertEqual(z.read('alias'),b'real');self.assertEqual(z.getinfo('alias').external_attr>>28,10)

class DistributionBudgetTests(unittest.TestCase):
    def test_variant_limits_and_growth_are_enforced(self):
        spec=importlib.util.spec_from_file_location('size_report',TOOL);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        baseline={'limits':{'app_bytes':1000,'archive_bytes':500},'growth_review_fraction':0.1,'variants':{'linux-x86_64-base':{'app_bytes':100,'archive_bytes':50}}}
        limits=module.selected_budget(baseline,'linux','x86_64','base')
        self.assertEqual(limits['app_bytes'],110);self.assertTrue(module.budget_violations({'app_bytes':111,'archive_bytes':50},limits))
        with self.assertRaises(ValueError):module.selected_budget(baseline,'linux','aarch64','base')

if __name__ == '__main__':
    unittest.main()
