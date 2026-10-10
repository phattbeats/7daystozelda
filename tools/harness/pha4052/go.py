import sys; sys.path.insert(0,'/tmp/z4052/t')
from drv import *
import threading
def j(c):
    try: print(c, run(c, "NAME='%s'\nexec(open('/tmp/z4052/t/%s').read())"%(c,sys.argv[1]), 500)[-500:])
    except Exception as e: print(c,'ERR',e)
ts=[threading.Thread(target=j,args=(c,)) for c in 'AB']
[t.start() for t in ts]; [t.join() for t in ts]
