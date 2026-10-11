from prop import *
H='exec(open("/tmp/z4167/t/h.py").read())\n'
ct=lambda c:[sum(1 for a in W(c,-1)['actors'] if a['id']==273), sum(1 for a in W(c,-1)['actors'] if a['id']==21)]
print(run('A',H+'kit(); time.sleep(2); print(1)').strip()[-20:])
for i in range(3):
    run('A',H+'key("c",0.12); time.sleep(1.3)')
    print('slash',i,ct('A'),ct('B'))
