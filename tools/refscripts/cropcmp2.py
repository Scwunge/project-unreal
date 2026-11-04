# Crop calibration (gen_crops2.py): total wheat age per shape (its two rows together) at 400, 800, 1200, 1600 ticks,
# vanilla (E:/mc-reference/run/log_crops2.log) beside ours (Saved/Remote/calib_ours.log, "mccalib" lines).
import re,sys
VLOG='E:/mc-reference/run/log_crops2.log'
SH=['dry1','dry9','wet1','wet3','wet5','wet9']; REPS=2; NROW=len(SH)*REPS
rows=[l for l in open(VLOG,encoding='utf-8',errors='ignore') if 'mcref box' in l]
v={}
for i,l in enumerate(rows):
    step,row=divmod(i,NROW)
    n=s=0
    for m in re.finditer(r'wheat\[age=(\d+)\]=(\d+)',l): n+=int(m.group(2)); s+=int(m.group(2))*int(m.group(1))
    v[(step+1,row)]=(n,s)
o={}
for l in open('Saved/Remote/calib_ours.log',encoding='utf-8',errors='ignore'):
    m=re.search(r'mccalib row=(\d+) t=(\d+) plants=(\d+) sum=(\d+)',l)
    if m: o[((int(m.group(2))-20)//80,int(m.group(1)))]=(int(m.group(3)),int(m.group(4)))
for k,name in enumerate(SH):
    print('==',name)
    for step in (5,10,15,20):
        a=[sum(v.get((step,k*REPS+r),(0,0))[i] for r in range(REPS)) for i in (0,1)]
        b=[sum(o.get((step,k*REPS+r),(0,0))[i] for r in range(REPS)) for i in (0,1)]
        print(f't={step*80:4}: vanilla plants={a[0]:3} sum={a[1]:4}   ours plants={b[0]:3} sum={b[1]:4}   ratio v/o={a[1]/max(b[1],1):.2f}')
