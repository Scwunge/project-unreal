# Vanilla (E:/mc-reference/run/log_icesnow.log) and ours (Saved/Logs/run_tests.log, "mcice chunk=N t=T ...") block counts per chunk of the ice and snow field.
import re,sys,collections
v=collections.defaultdict(dict); o=collections.defaultdict(dict)
rows=[l for l in open(sys.argv[1] if len(sys.argv)>1 else 'E:/mc-reference/run/log_icesnow.log',encoding='utf-8',errors='ignore') if 'mcref box' in l]
for i,l in enumerate(rows):
    m=re.search(r'\{(.*)\}',l); d={}
    for it in m.group(1).split(', '):
        k,n=it.rsplit('=',1); d[re.sub(r'\[layers=1\]|\[level=0\]','',k).replace('minecraft:','')]=int(n)
    v[(i//4)*100][i%4]=d
for l in open('Saved/Logs/run_tests.log',encoding='utf-8',errors='ignore'):
    m=re.search(r'mcice chunk=(\d) t=(\d+) (.*)$',l)
    if m:
        d={}
        for it in m.group(3).split():
            k,n=it.rsplit('=',1); d[re.sub(r'\[layers=1\]','',k).replace('minecraft:','')]=int(n)
        o[int(m.group(2))][int(m.group(1))]=d
for t in (500,1000,2000,3000,4000):
    for c in range(4):
        f=lambda d:' '.join(f'{k}={n}' for k,n in sorted(d.items()) if k!='stone')
        print(t,c,'vanilla:',f(v[t].get(c,{})),'| ours:',f(o[t].get(c,{})))
