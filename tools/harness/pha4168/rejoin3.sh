#!/bin/bash
cd /tmp/z4168rig/t
for c in A B C; do (echo "NAME='$c'; ROOM='${ROOM:-oc1}'"; cat join.py) | ./pc.sh $c > join$c.out 2>&1 & sleep 3; done
wait; tail -n2 joinA.out joinB.out joinC.out
