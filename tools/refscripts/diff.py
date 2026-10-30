# Prints how the states change tick by tick in a "states" log (vanilla's run/log_rs_*.log, or mc-unreal's test log).
import re,sys
def norm(st):
    st=st.replace('minecraft:','')
    m=re.match(r'([a-z_]+)\[(.*)\]$',st)
    if not m: return st
    return m.group(1)+'['+','.join(sorted(m.group(2).split(',')))+']'
def parse(path,tag):
    rows=[]
    for l in open(path,encoding='utf-8',errors='ignore'):
        m=re.search(tag+r' states: (?:t=(\d+) )?(.*)$',l)
        if m:
            d={}
            for it in m.group(2).strip().split(';'):
                if '=' in it:
                    k,v=it.split('=',1); d[k]=norm(v)
            rows.append(d)
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
