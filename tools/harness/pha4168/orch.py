import sys; sys.path.insert(0,'/tmp/<rig>/t')
exec(open('/tmp/<rig>/t/drv.py').read())
def O(c): return json.loads(run(c,'print(ev(\'Module.ccall("anchor_test_oca","string",[],[])\'))').strip().splitlines()[-1])
def show(tag):
    for c in 'ABC':
        o=O(c); print(tag,c,'local',o['localNote'],'voices',hex(o['voices']),[(x['ocaNote'],x['played'],x['puppetOca']) for x in o['clients'] if not x['self']])
for c in 'ABC': print(run(c,"raid('equip:ocarina'); time.sleep(1); key('ArrowLeft'); time.sleep(3); print('open')"))
show('open')
KEYS={'A':'x','B':'ArrowDown','C':'ArrowRight'}
for c in 'ABC': run(c,"page.keyboard.down(%r); print('down')"%KEYS[c])
time.sleep(1.5); show('all3')
run('C',"shot('/tmp/<rig>/t/ev/orch_C_all3.png')")
run('A',"page.keyboard.up('x')"); time.sleep(1.5); show('B+C')
for c in 'BC': run(c,"page.keyboard.up(%r)"%KEYS[c])
time.sleep(1.5); show('none')
# change note while held: B holds C-down then switches to C-left
run('B',"page.keyboard.down('ArrowDown'); time.sleep(0.8); page.keyboard.up('ArrowDown'); page.keyboard.down('ArrowUp'); print(1)"); time.sleep(1.2); show('B-switch')
run('B',"page.keyboard.up('ArrowUp')"); time.sleep(1.5); show('end')
