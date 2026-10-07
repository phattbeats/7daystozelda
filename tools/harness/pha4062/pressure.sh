#!/bin/bash
# pressure.sh [scale] [raiders-x24] [draw-distance] [shrink]: on a rig that already has the base: top up raiders to N*24, draw distance, posts,
# watch for crashes, then pool stats + fps + memory. shrink=1 makes A, C, D 320x180 so B's frame rate is not shared with three full-size renderers.
cd /tmp/z4062/t
SC=${1:-4}; NR=${2:-2}; DD=${3:-4}; SH=${4:-0}
for c in A B C D; do python3 x.py $c "cv('gSevenDays.GfxPoolScale',$SC)" >/dev/null 2>&1; done
python3 x.py A "N=24
for _ in range(max(0,$NR - len(R().get('raiders',[]))//24)):
    exec(open('/tmp/z4062/t/spawn24.py').read())
print(len(R().get('raiders',[])))" | tail -1
for c in A B C D; do python3 x.py $c "cv('gEnhancements.DisableDrawDistance',$DD); POST({'A':'towerN','B':'gateL','C':'gateR','D':'towerE'}[CLIENT],0); time.sleep(1); POST({'A':'towerN','B':'gateL','C':'gateR','D':'towerE'}[CLIENT],0)
if $SH and CLIENT!='B': page.set_viewport_size({'width':320,'height':180})
if not $SH: page.set_viewport_size({'width':960,'height':540})" >/dev/null 2>&1 & done; wait
for i in 1 2 3 4; do sleep 10; grep -h "Unhandled OP\|PAGE CRASH\|SEGV\|PAGEERROR" playd-?.log | cut -c1-200 | head -3; done
for c in A B C D; do python3 x.py $c "SCALE=$SC; DD=$DD; DUR=30
exec(open('/tmp/z4062/t/scen.py').read())" 2>&1 | tail -4 & done; wait
echo DONE
