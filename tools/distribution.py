"""Distribution-only validation. Never executes a packaged binary."""
import os,hashlib,json,pathlib,re,struct
SYSTEM_WINDOWS={s.lower() for s in ['KERNEL32.dll','USER32.dll','GDI32.dll','COMCTL32.dll','COMDLG32.dll','SHELL32.dll','ole32.dll','OLEAUT32.dll','ADVAPI32.dll','WINHTTP.dll','WS2_32.dll','SHLWAPI.dll','CRYPT32.dll','WINTRUST.dll','bcrypt.dll','ntdll.dll','RPCRT4.dll','VERSION.dll','DWMAPI.dll','UXTHEME.dll','PROPSYS.dll','IMM32.dll','MSIMG32.dll','WTSAPI32.dll','SETUPAPI.dll','IPHLPAPI.dll']}
SYSTEM_LINUX={'libwayland-client.so.0','libxcb.so.1','libxcb-xfixes.so.0','libX11.so.6','libXtst.so.6','libgtk-3.so.0','libgdk-3.so.0','libatk-1.0.so.0','libgdk_pixbuf-2.0.so.0','libgio-2.0.so.0','libgobject-2.0.so.0','libglib-2.0.so.0','libstdc++.so.6','libm.so.6','libgcc_s.so.1','libc.so.6','libpthread.so.0','libdl.so.2','librt.so.1','ld-linux-x86-64.so.2','ld-linux-aarch64.so.1'}
def _unpack(data,fmt,offset):
 size=struct.calcsize(fmt)
 if offset<0 or offset+size>len(data):raise ValueError('truncated binary structure')
 return struct.unpack_from(fmt,data,offset)
def _string(data,offset,end=None):
 end=min(len(data),end if end is not None else offset+4096)
 if offset<0 or offset>=end:raise ValueError('invalid binary string offset')
 stop=data.find(b'\0',offset,end)
 if stop<0:raise ValueError('unterminated binary string')
 return data[offset:stop].decode('utf-8',errors='strict')
def inspect_binary(path):
 path=pathlib.Path(path)
 with path.open('rb') as stream:magic=stream.read(4)
 if magic[:2]!=b'MZ' and magic not in [b'\x7fELF',b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca']:return None
 if path.stat().st_size>128*1024*1024:raise ValueError('binary exceeds inspection bound')
 data=path.read_bytes();imports=[];rpaths=[]
 if magic[:2]==b'MZ':
  pe=_unpack(data,'<I',60)[0]
  if data[pe:pe+4]!=b'PE\0\0':raise ValueError('invalid PE signature')
  machine,sections=_unpack(data,'<HH',pe+4);arch={0x8664:'x86_64',0xaa64:'aarch64',0x14c:'x86'}.get(machine,'unknown');optional=_unpack(data,'<H',pe+20)[0];start=pe+24
  def address(rva):
   for i in range(sections):
    section=start+optional+40*i;virtual_size,virtual,raw_size,raw=_unpack(data,'<IIII',section+8)
    if virtual<=rva<virtual+max(virtual_size,raw_size):return raw+rva-virtual
   raise ValueError('unmapped PE import address')
  if optional:
   if _unpack(data,'<H',start)[0]!=0x20b:raise ValueError('only PE32+ distributions supported')
   for directory,descriptor_size,name_offset in [(1,20,12),(13,32,4)]:
    rva,size=_unpack(data,'<II',start+112+directory*8)
    if not rva:continue
    offset=address(rva)
    for index in range(min(size//descriptor_size+1,4096)):
     position=offset+index*descriptor_size;fields=_unpack(data,'<'+'I'*(descriptor_size//4),position)
     if not any(fields):break
     imports.append(_string(data,address(_unpack(data,'<I',position+name_offset)[0])))
    else:raise ValueError('unterminated PE imports')
  return dict(format='pe',arch=arch,imports=imports,rpaths=rpaths)
 if magic==b'\x7fELF':
  if data[4:6]!=b'\x02\x01':raise ValueError('only little-endian ELF64 supported')
  arch={62:'x86_64',183:'aarch64'}.get(_unpack(data,'<H',18)[0],'unknown');offset=_unpack(data,'<Q',32)[0];entry,count=_unpack(data,'<HH',54);loads=[];dynamic=None
  for i in range(count):
   kind,flags,file_offset,virtual,physical,file_size,memory,align=_unpack(data,'<IIQQQQQQ',offset+i*entry)
   if kind==1:loads.append((virtual,file_offset,file_size))
   if kind==2:dynamic=(file_offset,file_size)
  if dynamic:
   values=[];string_table=None;string_size=None
   for pos in range(dynamic[0],dynamic[0]+dynamic[1],16):
    tag,value=_unpack(data,'<qQ',pos)
    if tag==0:break
    if tag==5:string_table=value
    if tag==10:string_size=value
    values.append((tag,value))
   if any(tag in [1,15,29] for tag,value in values):
    mapped=next((raw+string_table-virtual for virtual,raw,size in loads if string_table is not None and virtual<=string_table<virtual+size),None)
    if mapped is None or string_size is None:raise ValueError('invalid ELF dynamic string table')
    for tag,value in values:
     if tag==1:imports.append(_string(data,mapped+value,mapped+string_size))
     if tag in [15,29]:rpaths.extend(_string(data,mapped+value,mapped+string_size).split(':'))
  return dict(format='elf',arch=arch,imports=imports,rpaths=rpaths)
 if magic==b'\xcf\xfa\xed\xfe':
  arch={0x1000007:'x86_64',0x100000c:'aarch64'}.get(_unpack(data,'<I',4)[0],'unknown');count,size=_unpack(data,'<II',16);offset=32
  if count>4096 or offset+size>len(data):raise ValueError('invalid Mach-O load commands')
  for _ in range(count):
   command,length=_unpack(data,'<II',offset)
   if length<8 or offset+length>32+size:raise ValueError('invalid Mach-O load command')
   if command in [0xc,0x80000018,0x8000001f,0x80000023]:imports.append(_string(data,offset+_unpack(data,'<I',offset+8)[0],offset+length))
   if command==0x8000001c:rpaths.append(_string(data,offset+_unpack(data,'<I',offset+8)[0],offset+length))
   offset+=length
  return dict(format='macho',arch=arch,imports=imports,rpaths=rpaths)
 raise ValueError('universal binary requires its own explicit measured lane')
def system_import(target,name):
 if target=='windows':return name.lower() in SYSTEM_WINDOWS or name.lower().startswith(('api-ms-win-','ext-ms-win-'))
 if target=='linux':return name in SYSTEM_LINUX
 return name.startswith(('/System/Library/','/usr/lib/'))
def signature_errors(proof,expected):
 if not re.fullmatch(r'[0-9a-fA-F]{64}',expected or ''):return ['trusted signer SHA256 pin required']
 if proof.get('trusted') is not True:return ['unsigned or untrusted package']
 if (proof.get('signer_sha256') or '').lower()!=expected.lower():return ['wrong signer']
 return []
def validate_distribution(root,manifest,*,inspector=inspect_binary):
 root=pathlib.Path(root).resolve();errors=[];target=manifest.get('target');arch=manifest.get('arch')
 if manifest.get('schema_version')!=1 or target not in ['macos','windows','linux'] or arch not in ['x86_64','aarch64']:errors.append('invalid package target/schema')
 rows=manifest.get('files',[]);declared={};binaries={};actual=set();links={}
 declared_links={row['path']:row['target'] for row in manifest.get('symlinks',[])}
 for row in rows:
  name=row.get('path','');p=(root/name).resolve()
  if not name or pathlib.PurePosixPath(name).is_absolute() or not p.is_relative_to(root):errors.append('unsafe inventory path');continue
  if name in declared:errors.append('duplicate inventory path')
  declared[name]=row
 for p in root.rglob('*'):
  name=p.relative_to(root).as_posix()
  if hasattr(p,"is_junction") and p.is_junction():errors.append("junction in package: "+name);continue
  if p.is_symlink():
   links[name]=os.readlink(p)
   if not p.resolve().is_relative_to(root) or not p.exists():errors.append('unsafe symlink: '+name)
   continue
  if not p.is_file():
   if not p.is_dir():errors.append("special file in package: "+name)
   continue
  if name in ['cliptiq-package.json','usr/share/cliptiq/cliptiq-package.json']:continue
  actual.add(name)
  if any(part.lower() in ['reference','.git','build','out','__pycache__','node_modules','cmakefiles'] for part in p.relative_to(root).parts) or p.suffix.lower() in ['.pdb','.dsym','.o','.obj','.a','.lib','.pyc','.sqlite3','.db'] or p.name=='CMakeCache.txt':errors.append('forbidden package content: '+name)
  row=declared.get(name)
  if not row or row.get('bytes')!=p.stat().st_size or row.get('sha256')!=hashlib.sha256(p.read_bytes()).hexdigest():errors.append('inventory mismatch: '+name)
  try:binary=inspector(p)
  except (ValueError,UnicodeError,struct.error) as e:errors.append(f'invalid binary {name}: {e}');continue
  if binary:
   binaries[p]=binary
   if binary['arch']!=arch:errors.append('wrong architecture: '+name)
   if binary['format']!={'macos':'macho','linux':'elf','windows':'pe'}.get(target):errors.append('wrong binary format: '+name)
 if links!=declared_links:errors.append('symlink inventory mismatch')
 if actual!=set(declared):errors.append('inventory does not exactly match package files')
 if not binaries:errors.append('no native binary in package')
 by_name={p.name.lower():p for p in binaries}
 for path,binary in binaries.items():
  for name in binary['imports']:
   if system_import(target,name):continue
   if target=='windows':found=any(p.name.lower()==name.lower() and p.parent in [path.parent,root] for p in binaries)
   elif target=='linux':
    candidates=[pathlib.Path(r.replace('${ORIGIN}',str(path.parent)).replace('$ORIGIN',str(path.parent)))/name for r in binary['rpaths'] if r]
    found=any(p.resolve() in binaries and p.resolve().is_relative_to(root) for p in candidates)
   else:
    expand=lambda text:pathlib.Path(text.replace('@loader_path',str(path.parent)).replace('@executable_path',str(next((p.parent for p in binaries if p.name=='Cliptiq'),path.parent))))
    candidates=[expand(name)] if not name.startswith('@rpath/') else [expand(r)/name[len('@rpath/'):] for r in binary['rpaths']]
    found=any(p.resolve().is_relative_to(root) and p.resolve() in binaries for p in candidates)
   if not found:errors.append(f'missing dependency for {path.name}: {name}')
 return errors
