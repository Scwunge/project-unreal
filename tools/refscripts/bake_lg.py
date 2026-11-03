# python bake_lg.py <name>...: stores vanilla's tick-by-tick changes for lg_<name>.txt (block states and, for containers, item
# counts per cell, keys "i:x,y,z"; has_bottle_N reads as false, ours does not mirror bottles into the block state yet) from run/log_<name>.log in expect/lg_<name>.txt, for McClient.McSim.RefScriptsLogistics.
import re,sys
HB=re.compile('has_bottle_([0-9])=true')
def st(body):
    return dict(p.split('=',1) for p in body.split(';') if '=' in p and ',' in p.split('=',1)[0])
for name in sys.argv[1:]:
    states=[];items=[];t0=None
    for l in open(f'E:/mc-reference/run/log_{name}.log',encoding='utf-8',errors='ignore'):
        m=re.search(r'mcref states: t=(\d+) (.*)',l)
        if m:
            if t0 is None:t0=int(m.group(1))
            states.append({k:HB.sub(lambda m:'has_bottle_'+m.group(1)+'=false',v.replace('minecraft:','')) for k,v in st(m.group(2)).items()})
        m=re.search(r'mcref items: t=(\d+) (.*)',l)
        if m:
            d={}
            for k,v in st(m.group(2)).items():
                v=v.strip('{}').replace('minecraft:','').replace(', ',',')
                if v:d['i:'+k]=v
            items.append(d)
    out=[]
    for seq in (states,items):
        for i in range(1,len(seq)):
            for k in set(seq[i-1])|set(seq[i]):
                a,b=seq[i-1].get(k),seq[i].get(k)
                if a!=b:out.append(f'{i}|{k}|{a or "-"}|{b or "-"}')
    open(f'expect/{name}.txt','w',newline='\n').write('\n'.join([f'#vt {t0%20}']+sorted(out))+'\n')
    print(name,len(out))
