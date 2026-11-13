"""Extract aggregate heap counts only; never include object contents or addresses."""
import argparse,json,pathlib,re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input',type=pathlib.Path)
p.add_argument('output',type=pathlib.Path)
a=p.parse_args();text=a.input.read_text()
total=re.search(r'All zones: (\d+) nodes \((\d+) bytes\)',text)
if not total:raise SystemExit('No aggregate heap summary')
rows=[]
for line in text.splitlines():
 anonymous=re.match(r'\s*(\d+)\s+(\d+)\s+([\d.]+)\s+non-object\s*$',line)
 if anonymous:rows.append(dict(count=int(anonymous[1]),bytes=int(anonymous[2]),type='non-object'))
 m=re.match(r'\s*(\d+)\s+(\d+)\s+([\d.]+)\s+(.+?)\s{2,}(?:ObjC|Swift|C\+\+|C|CFType)\s',line)
 if m:rows.append(dict(count=int(m[1]),bytes=int(m[2]),type=m[4]))
result=dict(nodes=int(total[1]),allocated_bytes=int(total[2]),largest_types=rows[:20],
            controls=[r for r in rows if r['type'] in ['NSTextField','NSButton','NSButtonCell','NSStackView','NSTextView','NSLayoutConstraint','NSWindow','ViewGraph']])
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print(a.output, result['nodes'],'nodes,',round(result['allocated_bytes']/2**20,2),'MiB allocated')
