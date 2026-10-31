# python bake.py <name>...: stores vanilla's tick-by-tick state changes for rs_<name>.txt (from the reference client's
# run/log_rs_<name>.log) in expect/rs_<name>.txt, which the McClient.McSim.RefScripts test holds our simulation to.
import sys
from diff import parse,changes
for name in sys.argv[1:]:
    rows=parse(f'E:/mc-reference/run/log_rs_{name}.log','mcref')
    lines=sorted(f'{i}|{k}|{a or "-"}|{b or "-"}' for i,k,a,b in changes(rows))
    open(f'expect/rs_{name}.txt','w',newline='\n').write('\n'.join(lines)+'\n')
    print(name,len(lines))
