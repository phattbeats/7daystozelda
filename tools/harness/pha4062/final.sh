#!/bin/bash
# final.sh <serve> : the full run on a build. Sections print their own headers.
cd /tmp/z4062/t
S=${1:-serve-final}
echo "##### build $S: $(grep -o 'soh.js?v=[0-9a-f]*' /tmp/z4062/$S/index.html)"
./restart.sh $S > restart.out 2>&1
echo "##### default pool scale (no cvar set)"; python3 x.py A 'exec(open("/tmp/z4062/t/gfx.py").read()); time.sleep(2); g=GFX(); print("scale",g["scale"],"sizes",g["opa"]["size"],g["xlu"]["size"],g["ovl"]["size"])' | grep scale
echo "##### 94-piece base (fort2), 48 raiders, draw distance 4, towers"
python3 x.py A -f fortbuild.py 2>&1 | tail -1 | cut -c1-200
./pressure.sh 4 2 4 0 | grep -v "^$"
echo "##### default cap: try to place 256 mixed with no BaseCap cvar (expect 160)"
python3 x.py A "CAP=0; N=256; KIND='mixed'

exec(open('/tmp/z4062/t/fortbuildn.py').read())" 2>&1 | tail -1 | cut -c1-600
echo "##### cap raised to 256 via gSevenDays.BaseCap"
python3 x.py A "CAP=256; N=256; KIND='mixed'
exec(open('/tmp/z4062/t/fortbuildn.py').read())" 2>&1 | tail -1 | cut -c1-600
echo "##### 256-piece base, 48 raiders, draw distance 4"
./pressure.sh 4 2 4 0 | grep -v "^$"
echo "##### failsafe: force 60 overrun frames on every client"
for c in A B C D; do python3 x.py $c 'FORCE=60
exec(open("/tmp/z4062/t/failsafe.py").read())' 2>&1 | grep force & done; wait
echo "##### churn: scene transitions + day/night/blood moon x3 on every client"
for c in A B C D; do python3 x.py $c 'ROUNDS=3; EXPECT=256
exec(open("/tmp/z4062/t/churn.py").read())' 2>&1 | grep -v "^$\|bashrc" & done; wait
grep -h "Unhandled OP\|PAGE CRASH\|SEGV\|PAGEERROR" playd-?.log | cut -c1-200 | head -5
echo "##### late join: fifth client joins the 256-piece room"
./joinE.sh 2>&1 | grep -v "^$\|bashrc"
echo "##### save / reload: owner saves, reloads, loads the file"
python3 x.py A 'print("before", len([p for p in B()["placeables"] if p["scene"]==81]))
try:
    ev("Module.ccall(\"sevendays_test_save\",null,[],[])"); time.sleep(1); ev("sohPersist()"); time.sleep(3)
except Exception as e: print("save err", e)
page.goto("http://127.0.0.1:18471/index.html?r=%d#room=trailer2&name=Kai&color=3CB043&fairy=FFFFFF-3CB043&tunic=3CB043&sevendays=1"%time.time()); time.sleep(40); page.mouse.click(480,270); time.sleep(2)
exec(open("/tmp/z4062/t/load.py").read())
time.sleep(5); b=B(); print("after", len([p for p in b["placeables"] if p["scene"]==81]), "actors", b.get("placeableActors"), "scene", b.get("scene"), b.get("dyna"))' 2>&1 | grep "before\|after\|err"
grep -h "Unhandled OP\|PAGE CRASH\|SEGV\|PAGEERROR" playd-?.log playd-E.log | cut -c1-200 | head -5
echo FINALDONE
