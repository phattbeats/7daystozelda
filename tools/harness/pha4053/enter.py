import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
CODE = """
kit(); TW(6); raid('warp:8D'); time.sleep(14)
ev('Module.ccall("anchor_test_load_room",null,["number","number","number","number"],[3,0,240,300])'); time.sleep(5)
W(0,240,%d,0x8000); time.sleep(1)
print(json.dumps({k:S()[k] for k in ('scene','room','stage','link','twActors')}))
"""
a,b=both(CODE%int(sys.argv[1] if len(sys.argv)>1 else 100))
print(a,b)
for i in range(12):
    time.sleep(3)
    print(i, [json.loads(run(c,"print(json.dumps({k:S()[k] for k in ('stage','link')}))")) for c in 'AB'])
