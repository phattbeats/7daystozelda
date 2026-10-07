#!/bin/bash
# perf.sh <serve> <N...> : for each N a fresh rig with an N-piece base (mixed), then 24 raiders at default draw distance, A/C/D shrunk, B measured
cd /tmp/z4062/t
S=$1; shift
for N in "$@"; do
  echo "##### N=$N"
  ./restart.sh $S > restart.out 2>&1
  for c in A B C D; do python3 x.py $c "cv('gSevenDays.GfxPoolScale',4)" >/dev/null 2>&1; done
  if [ "$N" != "0" ]; then python3 x.py A "N=$N; KIND='${KIND:-mixed}'
exec(open('/tmp/z4062/t/fortbuildn.py').read())" 2>&1 | tail -1 | cut -c1-200; fi
  ./pressure.sh 4 1 1 1
done
echo ALLDONE
