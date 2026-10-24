import os,hashlib,importlib.util,pathlib,struct,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
def module():
 spec=importlib.util.spec_from_file_location('distribution',ROOT/'tools/distribution.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
class DistributionTests(unittest.TestCase):
 def fixture(self,root):
  (root/'Cliptiq.exe').write_bytes(b'candidate');return {'schema_version':1,'target':'windows','arch':'x86_64','variant':'base','files':[{'path':'Cliptiq.exe','bytes':9,'sha256':hashlib.sha256(b'candidate').hexdigest()}]}
 def test_actual_architectures_are_read_from_binary_headers(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   p=pathlib.Path(folder)/'app';b=bytearray(512);b[:2]=b'MZ';struct.pack_into('<I',b,60,64);b[64:68]=b'PE\0\0';struct.pack_into('<H',b,68,0x8664);p.write_bytes(b);self.assertEqual(m.inspect_binary(p)['arch'],'x86_64')
 def test_missing_dll_so_and_wrong_architecture(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   root=pathlib.Path(folder);manifest=self.fixture(root)
   inspect=lambda p:dict(format='pe',arch='x86_64',imports=['KERNEL32.dll'],rpaths=[])
   self.assertEqual(m.validate_distribution(root,manifest,inspector=inspect),[])
   for import_name in ['missing.dll','libmissing.so.1']:
    self.assertTrue(m.validate_distribution(root,manifest,inspector=lambda p:dict(format='pe',arch='x86_64',imports=[import_name],rpaths=[])))
   self.assertTrue(m.validate_distribution(root,manifest,inspector=lambda p:dict(format='pe',arch='aarch64',imports=[],rpaths=[])))
 def test_cache_reference_unlisted_hash_and_escape_are_rejected(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   root=pathlib.Path(folder);manifest=self.fixture(root);inspect=lambda p:dict(format='pe',arch='x86_64',imports=[],rpaths=[])
   for name in ['reference/upstream.txt','build/cache.obj','__pycache__/cache.pyc']:
    p=root/name;p.parent.mkdir(exist_ok=True);p.write_text('forbidden');self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect));p.unlink()
   manifest['files'][0]['sha256']='0'*64;self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect));manifest['files'][0]['path']='../escape';self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect))
 def test_dependency_in_unreachable_directory_does_not_count_as_bundled(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   root=pathlib.Path(folder);manifest=self.fixture(root);(root/'unused').mkdir();dll=root/'unused/private.dll';dll.write_bytes(b'dependency');manifest['files'].append({'path':'unused/private.dll','bytes':10,'sha256':hashlib.sha256(b'dependency').hexdigest()})
   inspector=lambda p:dict(format='pe',arch='x86_64',imports=['private.dll'] if p.name=='Cliptiq.exe' else [],rpaths=[])
   self.assertTrue(m.validate_distribution(root,manifest,inspector=inspector))
 def test_unlisted_internal_symlink_is_not_smuggled_into_package(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   root=pathlib.Path(folder);manifest=self.fixture(root)
   try:(root/'alias').symlink_to('Cliptiq.exe')
   except OSError:self.skipTest('symlink privilege unavailable')
   inspect=lambda p:dict(format='pe',arch='x86_64',imports=[],rpaths=[])
   self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect))
 def test_absolute_and_cwd_dependent_loader_routes_are_rejected(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   root=pathlib.Path(folder);manifest=self.fixture(root);manifest['target']='linux'
   private=root/'libprivate.so';private.write_bytes(b'private');manifest['files'].append({'path':private.name,'bytes':7,'sha256':hashlib.sha256(b'private').hexdigest()})
   for route in [str(root),'.','relative']:
    inspect=lambda p:dict(format='elf',arch='x86_64',imports=['libprivate.so'] if p.name=='Cliptiq.exe' else [],rpaths=[route])
    self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect),route)
   inspect=lambda p:dict(format='elf',arch='x86_64',imports=['libprivate.so'] if p.name=='Cliptiq.exe' else [],rpaths=['$ORIGIN'])
   self.assertEqual(m.validate_distribution(root,manifest,inspector=inspect),[])
   manifest['target']='macos'
   for route in [str(root),'.']:
    inspect=lambda p:dict(format='macho',arch='x86_64',imports=['@rpath/libprivate.so'] if p.name=='Cliptiq.exe' else [],rpaths=[route])
    self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect),route)
   inspect=lambda p:dict(format='macho',arch='x86_64',imports=[str(private)] if p.name=='Cliptiq.exe' else [],rpaths=[])
   self.assertTrue(m.validate_distribution(root,manifest,inspector=inspect))
 def test_unsupported_native_magic_cannot_hide_as_data(self):
  m=module()
  with tempfile.TemporaryDirectory() as folder:
   path=pathlib.Path(folder)/'helper'
   for magic in [b'\xce\xfa\xed\xfe',b'\xfe\xed\xfa\xce',b'\xfe\xed\xfa\xcf',b'\xca\xfe\xba\xbf',b'\xbf\xba\xfe\xca']:
    path.write_bytes(magic+b'\0'*128)
    with self.assertRaises(ValueError,msg=magic.hex()):m.inspect_binary(path)
 def test_unsigned_wrong_signer_and_size_excess_fail(self):
  m=module();self.assertTrue(m.signature_errors({'trusted':False,'signer_sha256':None},'1'*64));self.assertTrue(m.signature_errors({'trusted':True,'signer_sha256':'2'*64},'1'*64));self.assertEqual(m.signature_errors({'trusted':True,'signer_sha256':'1'*64},'1'*64),[])
if __name__=='__main__':unittest.main()
