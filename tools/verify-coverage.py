"""Validate every audited feature/platform, preserving manual acceptance gates."""
import argparse,json,pathlib,re
PLATFORMS={'macos','windows','linux-x11','linux-wayland'}
STATUSES={'verified','adapted','unsupported','pending'}
def validate(audit,data,root):
 root=pathlib.Path(root).resolve();ids=set(re.findall(r'^\| ((?:C|H|S|B)\d{2})\b',pathlib.Path(audit).read_text(encoding='utf-8'),re.M));errors=[];seen=set()
 if not ids:return ['audit contains no feature IDs']
 if data.get('schema_version')!=1 or not isinstance(data.get('features'),list):return ['invalid feature matrix schema']
 for row in data['features']:
  id=row.get('feature_id');platform=row.get('platform');key=(id,platform)
  if id not in ids or platform not in PLATFORMS:errors.append(f'unknown feature/platform: {key}')
  if key in seen:errors.append(f'duplicate feature/platform: {key}')
  seen.add(key)
  if row.get('status') not in STATUSES:errors.append(f'invalid status: {key}')
  for field in ['backend','limitation']:
   if not isinstance(row.get(field),str) or not row[field].strip():errors.append(f'missing {field}: {key}')
  evidence=row.get('evidence')
  if not isinstance(evidence,list) or not evidence:errors.append(f'missing evidence: {key}');continue
  for reference in evidence:
   if not isinstance(reference,str):errors.append(f'invalid evidence: {key}');continue
   path=(root/reference).resolve()
   if not path.is_relative_to(root) or not path.is_file():errors.append(f'missing/unsafe evidence: {reference}')
  if row.get('verification_scope')!='automated-contract':errors.append(f'unscoped verification: {key}')
  if row.get('manual_acceptance')!='open':errors.append(f'manual acceptance must remain open: {key}')
  if row.get('status')=='verified' and row.get('adaptation'):errors.append(f'adaptation cannot claim exact verified parity: {key}')
 expected={(id,platform) for id in ids for platform in PLATFORMS}
 for key in sorted(expected-seen):errors.append(f'unclassified: {key}')
 return errors
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--audit',type=pathlib.Path,required=True);parser.add_argument('--status',type=pathlib.Path,required=True);args=parser.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
 errors=validate(args.audit,json.loads(args.status.read_text(encoding='utf-8')),root)
 for error in errors:print(error)
 if not errors:print('PASS: all audited IDs classified on all four platform backends; manual gates OPEN')
 return int(bool(errors))
if __name__=='__main__':raise SystemExit(main())
