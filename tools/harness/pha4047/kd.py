import json,time
exec(open('/tmp/z4047/t/h.py').read())
def KD(c=0,a=0): return json.loads(ev('Module.ccall("anchor_test_kd","string",["number","number"],[%d,%d])'%(c,a)))
def kit():
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    raid('gohma'); raid('sword'); raid('tier:bomb')
def S(k):
    d=KD(); 
    if not d.get('present'): return {x:d.get(x) for x in ('scene','link','health','auth','own')}
    return {x:d.get(x) for x in k}
BRIEF=('scene','own','auth','act','phase','hp','cs','bc','sup','dying','pos','rotY','yo','spin','ih','ic','fl','flames','link','health','burning','iframes','roll')
