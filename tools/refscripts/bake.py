# python bake.py <name>...: stores vanilla's tick-by-tick state changes for rs_<name>.txt (from the reference client's
# run/log_rs_<name>.log) in expect/rs_<name>.txt, which the McClient.McSim.RefScripts test holds our simulation to.
# The first line "#vt N" is the vanilla game time of the first sample modulo 20, for things that run on multiples of 20.
import re,sys
from diff import parse,changes
for name in sys.argv[1:]:
    path=f'E:/mc-reference/run/log_rs_{name}.log'
    rows=parse(path,'mcref')
    lines=sorted(f'{i}|{k}|{a or "-"}|{b or "-"}' for i,k,a,b in changes(rows))
    import os
    ign=f'ignore/rs_{name}.txt'
    if os.path.exists(ign):
        skip={l.strip() for l in open(ign) if l.strip() and not l.startswith('#')}
        # "~line": vanilla's change is unexplained and ours may differ there; both are left out of the comparison, as "~sample|pos|"
        stars=sorted(l for l in skip if l.startswith('~*|')); skip-=set(stars)
        both={l[1:] for l in skip if l.startswith('~')}; skip-={'~'+l for l in both}
        print('ignored',len(skip & set(lines))+len(both & set(lines)),'unexplained changes')
        marks=sorted({'~'+'|'.join(l.split('|')[:2])+'|' for l in lines if l in both})
        # "~*|x,y,z|": every change at that cell is unexplained, vanilla's and ours
        marks+=stars
        starpos={l.split('|')[1] for l in stars}
        lines=[l for l in lines if l not in skip and l not in both and l.split('|')[1] not in starpos]+marks
    first=next(m for m in (re.search(r'states: t=(\d+)',l) for l in open(path,encoding='utf-8',errors='ignore')) if m)
    open(f'expect/rs_{name}.txt','w',newline='\n').write('\n'.join([f'#vt {int(first.group(1))%20}']+lines)+'\n')
    print(name,len(lines))
