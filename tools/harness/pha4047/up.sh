#!/bin/bash
D=/tmp/z4047/t
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(PORT=18422 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43447 PUBLIC_DIR=$D/serve setsid node /tmp/z4047/repo/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
export DEBUG=pw:browser __EGL_VENDOR_LIBRARY_FILENAMES=/tmp/vlibs/root/egl/10_nvidia.json LD_LIBRARY_PATH=/tmp/vlibs/root/usr/lib/x86_64-linux-gnu:/tmp/vlibs/root/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu FONTCONFIG_FILE=/tmp/vlibs/fonts.conf
(setsid python3 $D/playd-gpu.py 19847 local http://127.0.0.1:18422/index.html $D/prof-A > $D/playd-A.log 2>&1 < /dev/null &)
(setsid python3 $D/playd-gpu.py 19848 local http://127.0.0.1:18422/index.html $D/prof-B > $D/playd-B.log 2>&1 < /dev/null &)
