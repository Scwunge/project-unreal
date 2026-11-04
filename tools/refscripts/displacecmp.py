import re
B=re.findall(r'"(\w+)"',open('tools/refscripts/gen_displace.py').read().split('B = [')[1].split(']')[0])
rows=[l for l in open('E:/mc-reference/run/log_displace.log',encoding='utf-8',errors='ignore') if 'mcref states' in l]
snap=rows[-1].split('states:')[1]
cells={}
for m in re.finditer(r'(-?\d+),(-?\d+),(-?\d+)=([^;]+);',snap): cells[(int(m[1]),int(m[3]))]=m[4].replace('minecraft:','')
def short(v): return re.sub(r'\[.*','',v) + ('[wl]' if 'waterlogged=true' in v else '')
for i,b in enumerate(B):
    z=3*i+1
    print(f"{b:20s} W:{short(cells.get((2,z),'air')):16s} L:{short(cells.get((40,z),'air')):14s} C:{short(cells.get((60,z),'air'))}")
