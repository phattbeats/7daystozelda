#!/bin/bash
cd /tmp/z4054/t
for c in A B; do (echo "NAME='$c'; ROOM='gd1'"; cat join.py) | T=300 ./pc.sh $c > rj-$c.out & done; wait
(echo "NAME='A'"; cat e2.py) | T=60 ./pc.sh A; sleep 40; (echo "NAME='B'"; cat e2.py) | T=60 ./pc.sh B; sleep 40
for c in A B; do (echo "NAME='$c'"; cat g1.py) | T=60 ./pc.sh $c 2>&1 | cut -c1-520 | tail -n 2; done
