import sys,json,time
sys.path.insert(0,'/tmp/z4167/t')
from drv import run, both
IDS={'pot':0x111,'bush':0x125,'crate':0x1A0,'scrate':0x110,'rock':0x14E}
def W(c,cmd,a=0,b=0,cc=0):
    return json.loads(run(c,'exec(open("/tmp/z4167/t/h.py").read()) if "R" not in globals() else None\nprint(ev(\'Module.ccall("anchor_test_world","string",["number"]*4,[%d,%d,%d,%d])\'))'%(cmd,a,b,cc)).strip().splitlines()[-1])
def props(c):
    return [(a['id'],a['pos']) for a in W(c,-1)['actors'] if a['id'] in IDS.values()]
if __name__=='__main__':
    print(run('A','exec(open("/tmp/z4167/t/h.py").read())\nprint(R())'))
    print(run('B','exec(open("/tmp/z4167/t/h.py").read())\nprint(R())'))
