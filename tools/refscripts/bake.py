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
        print('ignored',len(skip & set(lines)),'unexplained changes');lines=[l for l in lines if l not in skip]
    first=next(m for m in (re.search(r'states: t=(\d+)',l) for l in open(path,encoding='utf-8',errors='ignore')) if m)
    open(f'expect/rs_{name}.txt','w',newline='\n').write('\n'.join([f'#vt {int(first.group(1))%20}']+lines)+'\n')
    print(name,len(lines))
