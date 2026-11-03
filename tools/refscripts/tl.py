# python tl.py <log> <z> [ours]: one row of a tripwire log as a compact timeline of its changes (S = string, H = hook; a/A attached, d/D disarmed, p/P powered, nsew connections).
import re,sys
def short(v):
    if v.startswith('tripwire_hook'):
        a=re.search(r'attached=(\w+),facing=(\w+),powered=(\w+)',v);return 'H'+a.group(2)[0]+('A' if a.group(1)=='true' else 'a')+('P' if a.group(3)=='true' else 'p')
    if v.startswith('tripwire['):
        a=dict(re.findall(r'(\w+)=(\w+)',v.split('[')[1]))
        return 'S'+('A' if a['attached']=='true' else 'a')+('D' if a['disarmed']=='true' else 'd')+('P' if a['powered']=='true' else 'p')+''.join(c.upper() if a[k]=='true' else c for c,k in zip('nesw',('north','east','south','west')))
    return v
z=int(sys.argv[2]);prev=None;t=0
for l in open(sys.argv[1],encoding='utf-8',errors='ignore'):
    m=re.search(r'mc(?:ref|ours) states: t?=?\d* ?(.*)',l) if ('mcref states' in l or ('mcours states' in l and 'LogAutomationController' not in l)) else None
    if not m: continue
    body=l.split('states: ',1)[1]
    body=re.sub(r'^t=\d+ ','',body)
    cells={}
    for p in body.split(';'):
        if '=' in p:
            k,v=p.split('=',1);x,y,zz=k.split(',') if k.count(',')==2 else (None,None,None)
            if zz==str(z) and y=='-60': cells[int(x)]=short(v.replace('minecraft:',''))
    s=' '.join(f'{x}:{v}' for x,v in sorted(cells.items()))
    if s!=prev: print(t,s);prev=s
    t+=1
