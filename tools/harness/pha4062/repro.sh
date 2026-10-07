#!/bin/bash
# repro.sh <serve> [scale] : restart rig on that build, 94-piece fort, 50 raiders, draw distance 4, watch 60 s for renderer errors, then pool stats
cd /tmp/z4062/t
SC=${2:-4}
./restart.sh $1 > restart.out 2>&1
python3 x.py A -f fortbuild.py 2>&1 | tail -1
for c in A B C D; do python3 x.py $c "cv('gSevenDays.GfxPoolScale',$SC)" >/dev/null 2>&1; done
python3 x.py A "N=24
exec(open('/tmp/z4062/t/spawn24.py').read())" | tail -1
python3 x.py A "N=24
exec(open('/tmp/z4062/t/spawn24.py').read())" | tail -1
for c in A B C D; do python3 x.py $c 'cv("gEnhancements.DisableDrawDistance",4); POST({"A":"towerN","B":"gateL","C":"gateR","D":"towerE"}[CLIENT],0); time.sleep(1); POST({"A":"towerN","B":"gateL","C":"gateR","D":"towerE"}[CLIENT],0)' >/dev/null 2>&1 & done; wait
for i in 1 2 3 4 5 6; do sleep 10; grep -h "Unhandled OP\|PAGE CRASH\|SEGV\|PAGEERROR" playd-?.log | cut -c1-200 | head -3; done
for c in A B C D; do python3 x.py $c "SCALE=$SC; DUR=30
exec(open('/tmp/z4062/t/scen.py').read())" 2>&1 | tail -3 & done; wait
echo DONE
