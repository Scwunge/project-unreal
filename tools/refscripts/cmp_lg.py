# python cmp_lg.py <name>: vanilla-only and ours-only changes of a McClient.McSim.RefScriptsLogistics run (Saved/Remote/run_tests.log).
import re,sys
n=sys.argv[1]
ours={re.search(r'mcours change (.*?)(?: \[log\])?$',l.strip()).group(1) for l in open('Saved/Remote/run_tests.log',encoding='utf-8',errors='ignore') if 'mcours change' in l}
exp={l.strip() for l in open(f'tools/refscripts/expect/{n}.txt') if l.strip() and not l.startswith('#')}
k=lambda s:(int(s.split('|')[0]),s)
print(len(ours),len(exp))
for t,s in (('vanilla only',exp-ours),('ours only',ours-exp)):
    print(t);[print(' ',x) for x in sorted(s,key=k)[:24]]
