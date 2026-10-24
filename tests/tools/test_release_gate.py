import importlib.util,pathlib,unittest,sys
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
class ReleaseGateTests(unittest.TestCase):
 def tool(self):
  spec=importlib.util.spec_from_file_location('release_gate',ROOT/'tools/verify-release.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
 def test_manual_open_cannot_become_a_green_release(self):
  import json
  m=self.tool();r=json.loads((ROOT/'docs/support/release-checklist.json').read_text());self.assertTrue(m.checklist_errors(r))
 def test_missing_gate_or_false_acceptance_is_rejected(self):
  m=self.tool();self.assertTrue(m.checklist_errors({'schema_version':1,'gates':[]}));self.assertTrue(m.checklist_errors({'schema_version':1,'gates':[{'id':'T04','status':'closed'}]}))
 def test_unsigned_and_wrong_signer_proof_fails(self):
  m=self.tool();self.assertTrue(m.signature_errors({'trusted':False},'1'*64));self.assertTrue(m.signature_errors({'trusted':True,'signer_sha256':'0'*64},'1'*64))
if __name__=='__main__':unittest.main()
