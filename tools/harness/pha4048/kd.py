import json,time
exec(open('/tmp/z4048/t/h.py').read())
def VA(c=0,a=0): return json.loads(ev('Module.ccall("anchor_test_va","string",["number","number"],[%d,%d])'%(c,a)))
KD=VA
def kit():
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    raid('gohma'); raid('sword'); raid('tier:bomb')
def S(k):
    d=VA()
    if not d.get('present'): return {x:d.get(x) for x in ('scene','link','health','auth','own','clear','hearts','warps')}
    return {x:d.get(x) for x in k}
BRIEF=('scene','own','auth','phase','cs','fp','bs','p4','p2t','door','bb','parts','sup','dying','key','pos','timer','link','health','clear','hearts','warps')
