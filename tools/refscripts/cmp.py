# python cmp.py <name>: the vanilla and our tick-by-tick state changes for rs_<name>.txt side by side.
import sys,re
sys.path.insert(0,__file__.rsplit('/',1)[0] if '/' in __file__ else '.')
from diff import parse,changes
name=sys.argv[1]
ref=parse(f'E:/mc-reference/run/log_rs_{name}.log','mcref')
ours=[];cur=None
for l in open('Saved/Remote/ours.log',encoding='utf-8',errors='ignore'):
    m=re.search(r'mcours script (\S+)',l)
    if m: cur=m.group(1)
    if cur==f'rs_{name}.txt' and 'mcours states:' in l:
        ours.append(l)
open('Saved/Remote/ours_tmp.log','w').write(''.join(ours))
ours=parse('Saved/Remote/ours_tmp.log','mcours')
a=changes(ref);b=changes(ours)
print(f'vanilla {len(a)} changes, ours {len(b)}')
sa=set(a);sb=set(b)
for c in sorted(sa|sb,key=lambda c:(c[0],c[1])):
    print('both   ' if c in sa and c in sb else ('VANILLA' if c in sa else 'OURS   '),*c)
