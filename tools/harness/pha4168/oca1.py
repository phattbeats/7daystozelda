import sys; sys.path.insert(0,'/tmp/<rig>/t')
exec(open('/tmp/<rig>/t/drv.py').read())
def O(c): return json.loads(run(c,'print(ev(\'Module.ccall("anchor_test_oca","string",[],[])\'))').strip().splitlines()[-1])
print(run('A',"raid('equip:ocarina'); print('eq')"))
print('A',O('A')); print('B',O('B'))
# open ocarina on C-left (arrow left), then play notes
print(run('A',"key('ArrowLeft'); time.sleep(3); shot('/tmp/<rig>/t/ev/oc_A_open.png'); print('opened')"))
for k,n in (('x','A'),('ArrowDown','Cdown'),('ArrowRight','Cright')):
    print(run('A',"page.keyboard.down(%r); time.sleep(1.5); print('hold')"%k))
    print(' A',O('A')); print(' B',O('B'))
    run('B',"shot('/tmp/<rig>/t/ev/oc_B_%s.png')"%n)
    print(run('A',"page.keyboard.up(%r); time.sleep(1); print('rel')"%k)); print(' B after',O('B'))
