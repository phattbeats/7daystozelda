#!/bin/bash
D=/tmp/z4155/rig
mkdir -p $D/tmp; export TMPDIR=$D/tmp
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(PORT=18455 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43455 PUBLIC_DIR=$D/serve setsid node /tmp/z4155/repo/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
export __EGL_VENDOR_LIBRARY_FILENAMES=/tmp/vlibs/egl/10_nvidia.json LD_LIBRARY_PATH=/tmp/vlibs/root/usr/lib/x86_64-linux-gnu:/tmp/vlibs/root/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu FONTCONFIG_FILE=/tmp/vlibs/fonts.conf
(setsid nice -n 19 python3 $D/playd-gpu.py 19855 local http://127.0.0.1:18452/index.html $D/prof-A > $D/playd-A.log 2>&1 < /dev/null &)
(setsid nice -n 19 python3 $D/playd-gpu.py 19866 local http://127.0.0.1:18452/index.html $D/prof-B > $D/playd-B.log 2>&1 < /dev/null &)
