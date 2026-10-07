#!/bin/bash
# #3915: like launch.sh but with a persistent browserless profile so the past-intro save survives restarts.
for f in /proc/[0-9]*/cmdline; do c=$(tr '\0' ' ' <$f 2>/dev/null); case "$c" in "python3 playd.py"*) kill ${f//[^0-9]/} 2>/dev/null;; esac; done
sleep 1; cd /tmp/m9tools
L='%7B%22args%22%3A%20%5B%22--unsafely-treat-insecure-origin-as-secure%3Dhttp%3A%2F%2F172.19.0.16%3A18110%22%2C%20%22--use-gl%3Dangle%22%2C%20%22--use-angle%3Dswiftshader%22%2C%20%22--enable-unsafe-swiftshader%22%2C%20%22--ignore-gpu-blocklist%22%2C%20%22--autoplay-policy%3Dno-user-gesture-required%22%2C%20%22--user-data-dir%3D%2Ftmp%2Fvq-m10prof%22%5D%7D'
setsid nohup python3 playd.py 19710 "ws://10.0.0.100:3000/?launch=$L" "http://172.19.0.16:18110/" /tmp/m10prof >/tmp/m10web/playd.log 2>&1 &
for i in $(seq 60); do curl -s -o /dev/null -m2 -X POST -d 'print(1)' http://127.0.0.1:19710/ && break; sleep 2; done
T=300 PORT=19710 ./pc.sh < /tmp/m10web/boot.py
