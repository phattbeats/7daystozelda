exec(open('/tmp/z4048/t/enter.py').read().split('both("kit')[0])
import time,json
def st(c):
    return json.loads(run(c,'d=VA(); print(json.dumps({k:d.get(k) for k in ("cs","fp","p4","bs","parts","sup","timer","yo","p2t","phase","clear","hearts","warps","health","scene")}))').strip().splitlines()[-1])
def pr(t):
    a=st('A'); b=st('B')
    f=lambda d:(d['cs'],d['fp'],d['p4'],d['bs'],d['yo'],d['phase'],d['sup'],[p for p in sorted(d['parts']) if 0<=p<16 or p==18],d['clear'],d['hearts'],d['warps'])
    print(t,'A',f(a)); print(t,'B',f(b))
