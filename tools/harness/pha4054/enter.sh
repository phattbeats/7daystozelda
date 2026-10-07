#!/bin/bash
# usage: enter.sh  -> both clients rejoin, warp to the ruins (Ganon2), and run the intro to the fight
cd /tmp/z4054/t
for c in A B; do (echo "NAME='$c'; ROOM='gd1'"; cat join.py) | T=300 ./pc.sh $c > rj-$c.out & done; wait
(echo "NAME='A'"; cat e1.py) | T=60 ./pc.sh A; sleep 8; (echo "NAME='B'"; cat e1.py) | T=60 ./pc.sh B; sleep 45
for r in 1 2; do for c in A B; do (echo "NAME='$c'"; cat t6.py) | T=330 ./pc.sh $c > t6-$c.out & done; wait; done
tail -n 1 t6-A.out t6-B.out | cut -c1-120
