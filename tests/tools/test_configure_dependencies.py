import pathlib,re,unittest,ast
ROOT=pathlib.Path(__file__).resolve().parents[2]

class ConfigureDependencyTests(unittest.TestCase):
 def test_core_workflow_prepares_tls_before_direct_configures(self):
  workflow=(ROOT/'.github/workflows/core.yml').read_text()
  jobs=re.split(r'^  ([a-z_]+):\s*$',workflow,flags=re.M)
  checked=[]
  for name,body in zip(jobs[1::2],jobs[2::2]):
   configures=list(re.finditer(r'run: cmake (?:--preset|-S )',body))
   if not configures:continue
   checked.append(name)
   for configure in configures:
    with self.subTest(job=name,command=configure.group()):
     self.assertRegex(body[:configure.start()],r'run: python3? tools/fetch-lan-deps\.py\s*\n')
  self.assertCountEqual(checked,['history','sanitizers','macos_backend_sanitizers','windows_backend_asan'])

 def test_standalone_configure_drivers_prepare_tls(self):
  for filename in ['tools/test-ocr.py','tests/size/update_sparkle.py']:
   with self.subTest(driver=filename):
    tree=ast.parse((ROOT/filename).read_text())
    calls=sorted((n for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='run'),key=lambda n:n.lineno)
    prepared=False;configured=False
    for call in calls:
     constants=[n.value for arg in call.args for n in ast.walk(arg) if isinstance(n,ast.Constant) and isinstance(n.value,str)]
     if 'tools/fetch-lan-deps.py' in constants:prepared=True
     if '-S' in constants:
      configured=True
      self.assertTrue(prepared,'Pinned TLS must be fetched before configuring '+filename)
    self.assertTrue(configured,'No configure invocation checked')

if __name__=='__main__':unittest.main()
