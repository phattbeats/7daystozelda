#!/bin/bash
cd /tmp/z4053/t
./fresh.sh > /dev/null
python3 merge1.py | tail -1
