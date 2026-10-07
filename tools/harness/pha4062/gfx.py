import json
def GFX(c=''): return json.loads(ev('Module.ccall("sevendays_test_gfx","string",["string"],[%s])'%json.dumps(c)))
