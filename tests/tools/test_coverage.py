import copy,importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
def load(name):
 spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
class CoverageTests(unittest.TestCase):
 def test_complete_real_feature_matrix(self):
  tool=load('verify-coverage');self.assertEqual(tool.validate(ROOT/'docs/research/2026-10-23-clipy-audit.md',json.loads((ROOT/'docs/support/feature-status.json').read_text()),ROOT),[])
 def test_missing_sensitive_ids_and_unproven_verified_are_rejected(self):
  tool=load('verify-coverage');source=json.loads((ROOT/'docs/support/feature-status.json').read_text())
  for id in ['C05','S11','B02']:
   broken=copy.deepcopy(source);broken['features']=[r for r in broken['features'] if r['feature_id']!=id];self.assertTrue(tool.validate(ROOT/'docs/research/2026-10-23-clipy-audit.md',broken,ROOT))
  broken=copy.deepcopy(source);broken['features'][0]['status']='verified';broken['features'][0]['evidence']=[];self.assertTrue(tool.validate(ROOT/'docs/research/2026-10-23-clipy-audit.md',broken,ROOT))
 def test_duplicate_rows_unknown_status_escape_and_manual_closure_fail(self):
  tool=load('verify-coverage');source=json.loads((ROOT/'docs/support/feature-status.json').read_text())
  for mutate in [lambda x:x['features'].append(x['features'][0]),lambda x:x['features'][0].update(status='complete'),lambda x:x['features'][0].update(evidence=['../outside']),lambda x:x['features'][0].update(manual_acceptance='closed')]:
   broken=copy.deepcopy(source);mutate(broken);self.assertTrue(tool.validate(ROOT/'docs/research/2026-10-23-clipy-audit.md',broken,ROOT))
if __name__=='__main__':unittest.main()
