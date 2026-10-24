import json,pathlib,re,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class CiPolicyTests(unittest.TestCase):
 def test_all_external_actions_have_verified_commit_pins(self):
  pins=json.loads((ROOT/'packaging/actions-lock.json').read_text())
  for path in (ROOT/'.github/workflows').glob('*.yml'):
   for action,ref in re.findall(r'uses:\s*([^\s@]+)@([^\s#]+)',path.read_text()):
    self.assertRegex(ref,r'^[0-9a-f]{40}$');repository='/'.join(action.split('/')[:2]);self.assertEqual(ref,pins[repository])
 def test_release_workflow_does_not_publish_or_gain_write_permissions(self):
  text=(ROOT/'.github/workflows/release.yml').read_text();self.assertNotIn('contents: write',text);self.assertNotIn('gh release create',text);self.assertIn('require_release_ready',text)
if __name__=='__main__':unittest.main()
