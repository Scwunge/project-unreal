# Prints how the states change tick by tick in a "states" log (vanilla's run/log_rs_*.log, or mc-unreal's test log).
import re,sys
def norm(st):
    st=st.replace('minecraft:','')
    m=re.match(r'([a-z_]+)\[(.*)\]$',st)
    if not m: return st
    return m.group(1)+'['+','.join(sorted(m.group(2).split(',')))+']'
def parse(path,tag):
    rows=[];last=None
    for l in open(path,encoding='utf-8',errors='ignore'):
        m=re.search(tag+r' states: (?:t=(\d+) )?(.*)$',l)
        if m:
            d={}
            t=m.group(1)
            for it in m.group(2).strip().split(';'):
                if '=' in it:
                    k,v=it.split('=',1); d[k]=norm(v)
            if t is not None and t==last and rows[-1] and d and not (set(rows[-1])&set(d)): rows[-1].update(d)   # different boxes logged in one tick are one sample
            else: rows.append(d)
            last=t
    return rows
def changes(rows,ys=None):
    out=[];prev=rows[0]
    for i,r in enumerate(rows[1:],1):
        for k in sorted(set(prev)|set(r)):
            if prev.get(k)!=r.get(k): out.append((i,k,prev.get(k),r.get(k)))
        prev=r
    return out
if __name__=='__main__':
    tag=sys.argv[2] if len(sys.argv)>2 else 'mcref'
    rows=parse(sys.argv[1],tag)
    for c in changes(rows): print(*c)
