#!/bin/bash
# #4060 capture rig: relay :43460, server :18471 (origin of the copied saves), sink :19460, daemons A..D :19461-19464
D=/tmp/z4062/t
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(PORT=18471 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43460 PUBLIC_DIR=$D/serve setsid node /tmp/z4055/web/server.js > $D/server.log 2>&1 < /dev/null &)
(setsid python3 $D/sink.py > $D/sink.log 2>&1 < /dev/null &)
sleep 2
. /tmp/vlibs4055/gpu-env.sh; export DEBUG=pw:browser
start() { (env $3 setsid python3 $D/playd-gpu.py $2 local http://127.0.0.1:18471/index.html $D/prof-$1 > $D/playd-$1.log 2>&1 < /dev/null &); }
for c in ${CLIENTS:-A B C D}; do
  case $c in A) start A 19461 "TAP=1 VW=${AW:-1920} VH=${AH:-1080}";; B) start B 19462 "";; C) start C 19463 "";; D) start D 19464 "";; E) start E 19465 "";; esac
done
