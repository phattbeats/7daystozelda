import json,time
exec(open('/tmp/z4053/t/h2.py').read())
def TW(c=0,a=0): return json.loads(ev('Module.ccall("anchor_test_tw","string",["number","number"],[%d,%d])'%(c,a)))
def kit():
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    raid('sword')
def S(keys=('age','layer','bossCat','enemyCat','scene','room','roomStatus','twActors','own','auth','stage','link','health','mirror','shielding','frozen','burning','blasts','pools','kotake','koume','twinrova','clear','began')):
    d=TW(); return {k:d.get(k) for k in keys}
