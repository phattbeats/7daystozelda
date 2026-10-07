exec(open('/tmp/z4048/t/enter.py').read())
exec(open('/tmp/z4048/t/fl.py').read())
import time
def shots(n): run('A','shot("/tmp/z4048/a_%s.png")'%n); run('B','shot("/tmp/z4048/b_%s.png")'%n)
for i in range(5):
    both('for i in range(3):\n    key("x"); time.sleep(0.5)'); time.sleep(6)
pr('fight start'); shots('01_fight')
for k,p in enumerate([0,1,2]): run('BA'[k%2],'VA(1,%d)'%p); time.sleep(3)
time.sleep(5); pr('supports cut'); shots('02_supports')
for r in range(6):
    for p in [6,7,8,9,10]: run('AB'[(p+r)%2],'VA(1,%d)'%p)
    time.sleep(3)
    if st('A')['fp']>=9: break
pr('uppers dead'); shots('03_uppers')
run('B','VA(2,-1)'); time.sleep(1.5); shots('04_stun')
for i,p in enumerate([11,12,13,14,15]): run('AB'[i%2],'VA(1,%d)'%p); time.sleep(0.5)
time.sleep(3); pr('lowers dead'); shots('05_phase4')
for r in range(20):
    if r%4==0: run('B','VA(2,-1)'); time.sleep(1.5)
    run('AB'[r%2],'VA(1,-1)'); time.sleep(1.2)
    a=st('A')
    if a['fp']>=18 or a['cs']>=14: break
pr('defeat'); 
for r in range(16):
    time.sleep(5); a=st('A'); b=st('B'); pr('d%d'%r)
    if r==2: shots('06_death')
    if a['warps'] and b['warps'] and a['hearts'] and b['hearts']: break
shots('07_end'); pr('END')
