import json
print(CLIENT, json.loads(ev('Module.ccall("sevendays_test_draw","string",["string"],["tier:%d"])'%TIERV))['tier'])
