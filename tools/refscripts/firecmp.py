# Vanilla (run/log_fire3.log, or the log named on the command line) and ours (Saved/Remote/fire_ours.log) block counts per row of the fire field, side by side.
import re,collections,sys
VLOG=sys.argv[1] if len(sys.argv)>1 else 'E:/mc-reference/run/log_fire3.log'
def vanilla():
    out={}
    rows=[l for l in open(VLOG,encoding='utf-8',errors='ignore') if 'mcref box' in l]
    for i,l in enumerate(rows):
        m=re.search(r't=(\d+) \{(.*)\}',l); d=collections.Counter()
        for it in m.group(2).split(', '):
            if '=' in it:
                k,v=it.rsplit('=',1); d[re.sub(r'\[.*','',k).replace('minecraft:','')]+=int(v)
        out[((i//4+1)*20,(0,4,8,12)[i%4])]=d
    return out
def ours():
    out={}
    for l in open('Saved/Remote/fire_ours.log',encoding='utf-8',errors='ignore'):
        m=re.search(r'mcfire row=(\d+) t=(\d+) (.*)$',l)
        if m:
            d=collections.Counter()
            for it in m.group(3).split():
                k,v=it.split('='); d[k.replace('minecraft:','')]=int(v)
            out[(int(m.group(2)),int(m.group(1)))]=d
    return out
v,o=vanilla(),ours()
names={0:'bare',4:'planks',8:'wool',12:'leaves'}
for z in (0,4,8,12):
    print('==',names[z])
    for t in range(20,640,60):
        a,b=v.get((t,z),{}),o.get((t,z),{})
        f=lambda d:' '.join(f'{k}={n}' for k,n in sorted(d.items()) if k!='water')
        print(f'{t:4} vanilla: {f(a):40} ours: {f(b)}')
