#!/bin/bash
# big.sh <serve> <N> <kind> [scale] : fresh rig, N-piece base (mixed|heavy), then stats before any raid
cd /tmp/z4062/t
./restart.sh $1 > restart.out 2>&1
for c in A B C D; do python3 x.py $c "cv('gSevenDays.GfxPoolScale',${4:-4})" >/dev/null 2>&1; done
python3 x.py A "N=$2; KIND='$3'
exec(open('/tmp/z4062/t/fortbuildn.py').read())" 2>&1 | tail -2
echo DONE
