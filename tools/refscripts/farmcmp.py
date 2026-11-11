import re
exec(open('tools/refscripts/gen_farmland.py').read().split('L = [')[0])
rows=[re.search(r'=minecraft:(\w+)(?:\[moisture=(\d)\])?',l) for l in open('E:/mc-reference/run/log_farmland.log',encoding='utf-8',errors='ignore') if 'mcref states' in l]
vals=[(m[1],m[2]) if m else None for m in rows]
n=len(CELLS)
for ph,name in ((0,'no rain'),(1,'rain')):
    print(name)
    for i,(dy,dx,dz) in enumerate(CELLS):
        v=vals[ph*n+i]
        if ph==0 or dy is None: print(f"  dy={dy} dx={dx} dz={dz}: {v}")
