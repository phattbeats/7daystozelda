import json,time
exec(open('/tmp/z4052/t/h2.py').read())
def SST(c=0,a=0): return json.loads(ev('Module.ccall("anchor_test_sst","string",["number","number"],[%d,%d])'%(c,a)))
def kit():
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    raid('gohma'); raid('sword'); raid('tier:bomb')
def S(k):
    d=SST()
    if not d.get('present'): return {x:d.get(x) for x in ('scene','link','health','auth','own')}
    return {x:d.get(x) for x in k}
BRIEF=('scene','own','auth','phase','introDone','hp','sup','dying','pos','vv','cylOn','drum','effMode','link','health','grabbed','parent','hearts','warps','clear')
