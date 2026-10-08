DS=[900,1500,2200,3000,4000,5200,6500]
for T in TIERS:
    ev('Module.ccall("sevendays_test_draw","string",["string"],["tier:%d"])'%T)
    TAG='t%d'%T
    exec(open('/tmp/z4062/t/dd2.py').read())
