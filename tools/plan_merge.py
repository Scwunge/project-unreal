# Adds the lines a branch put into PLAN.md (relative to a base commit) to the PLAN.md in the working tree.
# Usage: python tools/plan_merge.py <branch> <merge-base>   (run after taking our PLAN.md in a conflicted merge). UTF-8 throughout.
import sys
import subprocess,re
def sh(*a): return subprocess.run(a,capture_output=True).stdout.decode('utf-8')
new=sh('git','show',sys.argv[1]+':PLAN.md').replace('\r\n','\n')
old=sh('git','show',sys.argv[2]+':PLAN.md').replace('\r\n','\n')
head=open('PLAN.md',newline='',encoding='utf-8').read(); crlf='\r\n' in head; head=head.replace('\r\n','\n')
ol=old.split('\n'); nl=new.split('\n')
import difflib
sm=difflib.SequenceMatcher(None,ol,nl,autojunk=False)
adds=[]
for tag,i1,i2,j1,j2 in sm.get_opcodes():
    if tag in('insert','replace'):
        adds.append((ol[i1-1] if i1>0 else None, ol[i1] if i1<len(ol) else None, nl[j1:j2], tag))
print(len(adds),[ (a[3],len(a[2])) for a in adds])
hl=head.split('\n')
for prev,nxt,block,tag in adds:
    if tag=='replace': print('REPLACE',prev,nxt)
    # anchor on next line (insert before it) if present uniquely in head, else after prev
    idx=None
    if nxt is not None and nxt.strip() and hl.count(nxt)==1: idx=hl.index(nxt)
    elif prev is not None and prev.strip() and hl.count(prev)==1: idx=hl.index(prev)+1
    if idx is None:
        idx=len(hl); print('append at end',(prev or '')[:50])
    hs=set(hl); block=[x for x in block if x not in hs or not x.strip()]
    hl[idx:idx]=block
out='\n'.join(hl)
if crlf: out=out.replace('\n','\r\n')
open('PLAN.md','w',newline='',encoding='utf-8').write(out)
