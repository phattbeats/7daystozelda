import urllib.request, json, threading, time, sys
PORTS={'A':19851,'B':19862}
PRE="exec(open('/tmp/z4051/t/mo.py').read())\n"
def run(c, code, t=600):
    req=urllib.request.Request('http://127.0.0.1:%d/'%PORTS[c], data=(PRE+code).encode())
    return urllib.request.urlopen(req, timeout=t).read().decode()
def both(code_a, code_b=None):
    out={}
    def go(c,code): out[c]=run(c,code)
    ts=[threading.Thread(target=go,args=('A',code_a)), threading.Thread(target=go,args=('B',code_b if code_b is not None else code_a))]
    [t.start() for t in ts]; [t.join() for t in ts]
    return out['A'], out['B']
def kd(c, cmd=0, arg=0):
    return json.loads(run(c, "print(json.dumps(KD(%d,%d)))"%(cmd,arg)).strip().splitlines()[-1])
