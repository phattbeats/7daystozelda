#!/bin/bash
cd /tmp/z4053/t
(echo "NAME='A'; ROOM='${ROOM:-tw1}'"; cat join.py) | ./pc.sh A > joinA.out 2>&1 &
(echo "NAME='B'; ROOM='${ROOM:-tw1}'"; cat join.py) | ./pc.sh B > joinB.out 2>&1
wait; cat joinA.out joinB.out
