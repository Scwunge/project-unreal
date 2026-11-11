# python tools/refscripts/cmp_local.py <script.txt> [log]: vanilla-only (V) and ours-only (O) changes of one script in a local test log
# (default Saved/Logs/run_tests.log), run from the repo root.
import re,sys
n=sys.argv[1];lg=sys.argv[2] if len(sys.argv)>2 else 'Saved/Logs/run_tests.log'
sec=None;ours=set()
for l in open(lg,encoding='utf-8',errors='ignore'):
    m=re.search(r'mcours script (\S+)',l)
    if m and '[log]' not in l: sec=m.group(1);continue
    if sec==n and 'mcours change' in l and '[log]' not in l: ours.add(re.search(r'mcours change (.*?)$',l.strip()).group(1))
exp={l.strip() for l in open('tools/refscripts/expect/'+n) if l.strip() and not l.startswith('#')}
k=lambda s:(int(s.split('|')[0]),s)
print(len(ours),len(exp))
for t,s in (('V',exp-ours),('O',ours-exp)):
    print(t,len(s))
    for x in sorted(s,key=k)[:int(sys.argv[3]) if len(sys.argv)>3 else 30]: print(' ',x)
