import json,time
exec(open('/tmp/z4050/t/h.py').read())
def VF(c=0,a=0): return json.loads(ev('Module.ccall("anchor_test_vf","string",["number","number"],[%d,%d])'%(c,a)))
def kit():
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    raid('gohma'); raid('sword')
BRIEF=('scene','own','auth','phase','hp','intro','fdWait','fd2Wait','fd2Act','fd2Death','face','supFd','supFd2','dyingFd','fdPos','fd2Pos','rocks','hearts','warps','clear','link','health','burning','pending')
def S(k=BRIEF):
    d=VF()
    if not d.get('present'): return {x:d.get(x) for x in ('scene','link','health','auth','own','clear')}
    return {x:d.get(x) for x in k}
