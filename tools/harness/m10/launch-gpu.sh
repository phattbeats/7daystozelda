#!/bin/bash
# PHA-3915: local headless Chrome (browserless caps sessions ~5 min and is full). Persistent profile keeps the save.
for f in /proc/[0-9]*/cmdline; do c=$(tr '\0' ' ' <$f 2>/dev/null); case "$c" in "python3 playd-gpu.py 19711"*) kill ${f//[^0-9]/} 2>/dev/null;; esac; done
sleep 1; . /tmp/vlibs/gpu-env.sh; cd /tmp/m9tools
setsid nohup python3 playd-gpu.py 19711 local "http://127.0.0.1:18110/" /tmp/vq-m10prof-local >/tmp/m10web/playd-local.log 2>&1 &
for i in $(seq 60); do curl -s -o /dev/null -m2 -X POST -d 'print(1)' http://127.0.0.1:19711/ && break; sleep 2; done
T=300 PORT=19711 ./pc.sh < /tmp/m10web/boot.py
