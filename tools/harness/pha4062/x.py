# x.py CLIENT 'code' | x.py CLIENT -f file  : run in a rig daemon with h.py prelude
import sys, urllib.request
PORTS = dict(A=19461, B=19462, C=19463, D=19464, E=19465)
c = sys.argv[1]; code = open(sys.argv[3]).read() if sys.argv[2] == '-f' else sys.argv[2]
pre = "exec(open('/tmp/z4062/t/h.py').read())\nCLIENT=%r\n" % c
print(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:%d/' % PORTS[c], data=(pre + code).encode()), timeout=900).read().decode())
