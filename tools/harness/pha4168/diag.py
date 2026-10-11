import sys; sys.path.insert(0,'/tmp/<rig>/t')
exec(open('/tmp/<rig>/t/drv.py').read())
def D(c): o=json.loads(run(c,'print(ev(\'Module.ccall("anchor_test_oca","string",[],[])\'))').strip().splitlines()[-1]); return hex(o['voices'])+' '+o['diag']
for c in 'ABC': run(c,"raid('equip:ocarina'); time.sleep(1); key('ArrowLeft'); time.sleep(3)")
K={'A':'x','B':'ArrowDown','C':'ArrowRight'}
for c in 'ABC': run(c,"page.keyboard.down(%r)"%K[c])
time.sleep(1.5); print('all3 A', D('A'))
run('A',"page.keyboard.up('x')")
for t in (0.1,0.5,1.5): time.sleep(t); print('A rel+%s'%t, D('A'))
for c in 'BC': run(c,"page.keyboard.up(%r)"%K[c])
time.sleep(1.5); print('none A', D('A'))
# remote-only from scratch: B and C hold while A idle
for c in 'BC': run(c,"page.keyboard.down(%r)"%K[c])
time.sleep(1.5); print('BC-only A', D('A'))
for c in 'BC': run(c,"page.keyboard.up(%r)"%K[c])
