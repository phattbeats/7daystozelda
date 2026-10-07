#!/bin/bash
D=/tmp/z4055/t
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(PORT=18471 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43471 PUBLIC_DIR=$D/serve setsid node /tmp/z4055/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
. /tmp/vlibs4055/gpu-env.sh; export DEBUG=pw:browser
(setsid python3 $D/playd-gpu.py 19571 local http://127.0.0.1:18471/index.html $D/prof-A > $D/playd-A.log 2>&1 < /dev/null &)
(setsid python3 $D/playd-gpu.py 19572 local http://127.0.0.1:18471/index.html $D/prof-B > $D/playd-B.log 2>&1 < /dev/null &)
