import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('windows_manifest', ROOT / 'tools/inspect-windows-manifest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class WindowsManifestTests(unittest.TestCase):
    def manifest(self):
        return (ROOT / 'resources/windows/cliptiq.manifest').read_text()

    def test_explicit_security_and_controls_retained(self):
        module.validate_manifest(self.manifest())

    def test_bad_linker_namespace_or_elevation_rejected(self):
        for original, replacement in [('asm.v3', 'asm.v1'), ('level="asInvoker"', 'level="requireAdministrator"'), ('uiAccess="false"', 'uiAccess="true"'), ('level="asInvoker"', 'xmlns:bad="urn:schemas-microsoft-com:asm.v1" bad:level="asInvoker"')]:
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                module.validate_manifest(self.manifest().replace(original, replacement))

    def test_missing_controls_rejected(self):
        with self.assertRaises(ValueError):
            module.validate_manifest(self.manifest().replace('Microsoft.Windows.Common-Controls', 'private.controls'))
