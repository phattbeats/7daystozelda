# run scripts on several clients at once: drv.py A:file.py B:file.py ...  (env vars passed as a prelude)
import sys, threading, urllib.request
PORTS = dict(A=19461, B=19462, C=19463, D=19464, E=19465)
out = {}
def go(c, code):
    req = urllib.request.Request('http://127.0.0.1:%d/' % PORTS[c], data=("exec(open('/tmp/z4060/t/h.py').read())\nCLIENT=%r\n" % c + code).encode())
    out[c] = urllib.request.urlopen(req, timeout=900).read().decode()
ts = []
for a in sys.argv[1:]:
    c, f = a.split(':', 1); ts.append(threading.Thread(target=go, args=(c, open(f).read())))
[t.start() for t in ts]; [t.join() for t in ts]
for c in sorted(out): print('==', c); print(out[c])
