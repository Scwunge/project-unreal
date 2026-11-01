# Total crop age per variant row every 80 ticks, vanilla (E:/mc-reference/run/log_crops1.log or the log named) beside ours
# (Saved/Remote/crops_ours.log from McClient.McSim.CropField). The sum of ages over all the crops in the row is the figure: it
# is what random growth averages, so the two should agree within a few percent.
import re,sys,collections
VLOG=sys.argv[1] if len(sys.argv)>1 else 'E:/mc-reference/run/log_crops1.log'
NAMES=['dry_single','wet_single','wet_row','wet_checker','wet_full','wet_stem']
def total(d):
    n=s=0
    for k,v in d.items():
        m=re.search(r'(wheat|melon_stem)\[age=(\d+)\]',k)
        if m: n+=v; s+=v*int(m.group(2))
        elif 'attached' in k or k.endswith('melon') : pass
    return n,s
def vanilla():
    rows=[l for l in open(VLOG,encoding='utf-8',errors='ignore') if 'mcref box' in l]
    out={}
    for i,l in enumerate(rows):
        m=re.search(r't=(\d+) \{(.*)\}',l); d=collections.Counter()
        for it in m.group(2).split(', '):
            if '=' in it:
                k,v=it.rsplit('=',1); d[k.replace('minecraft:','')]+=int(v)
        out[(i//len(NAMES),i%len(NAMES))]=d
    return out
def ours():
    out={}
    for l in open('Saved/Remote/crops_ours.log',encoding='utf-8',errors='ignore'):
        m=re.search(r'mccrop row=(\d+) t=(\d+) (.*)$',l)
        if m:
            d=collections.Counter()
            for it in m.group(3).split(';'):
                if '=' in it:
                    k,v=it.rsplit('=',1); d[k.replace('minecraft:','')]+=int(v)
            out[(int(m.group(2))//80,int(m.group(1)))]=d   # step index 1..10 -> t=100..820
    return out
v,o=vanilla(),ours()
for r,name in enumerate(NAMES):
    print('==',name)
    for step in range(10):
        a=total(v.get((step,r),{})); b=total(o.get((step+1,r),{}))
        print(f'step {step+1:2}: vanilla crops={a[0]:3} age sum={a[1]:4}   ours crops={b[0]:3} age sum={b[1]:4}')
