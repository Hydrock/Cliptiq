"""Expose synthetic CTest failures in Actions annotations; never change test status."""
import argparse,pathlib,sys,xml.etree.ElementTree as ET

def escape(value,property=False):
 value=value.replace('%','%25').replace('\r','%0D').replace('\n','%0A')
 return value.replace(':','%3A').replace(',','%2C') if property else value

def report(paths,stream):
 failures=0
 for path in paths:
  for case in ET.parse(path).getroot().iter('testcase'):
   problems=list(case.findall('failure'))+list(case.findall('error'))
   if not problems:continue
   failures+=1
   body='\n'.join((node.get('message','')+'\n'+''.join(node.itertext())).strip() for node in problems)
   body+='\n'+(case.findtext('system-out') or '')
   title='CTest: '+case.get('name','unnamed')
   print('::error title='+escape(title,True)+'::'+escape(body[:3500]),file=stream)
 print(f'CTest reports: {len(paths)}, failed cases: {failures}',file=stream)
 return failures

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('pattern');args=parser.parse_args()
 paths=sorted(pathlib.Path('.').glob(args.pattern));report(paths,sys.stdout)
 if not paths:print('No CTest XML report found; configure/build may have failed before tests.')
if __name__=='__main__':main()
