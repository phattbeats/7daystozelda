#!/bin/bash
cd /tmp/z4053/t
./install.sh >/dev/null
ROOM=${ROOM:-tw$RANDOM} ./rejoin_both.sh > /dev/null 2>&1
python3 enter.py ${1:-100} > enter.out 2>&1
python3 waitstage.py ${2:-1} 150 | tail -2
