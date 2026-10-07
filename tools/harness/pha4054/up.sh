#!/bin/bash
D=/tmp/z4054/t
V=/tmp/vlibs4055
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(cd /tmp/z4054/repo/web && PORT=28454 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43454 PUBLIC_DIR=$D/serve setsid node /tmp/z4054/repo/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
export DEBUG=pw:browser __EGL_VENDOR_LIBRARY_FILENAMES=$V/root/egl/10_nvidia.json LD_LIBRARY_PATH=$V/root/usr/lib/x86_64-linux-gnu:$V/root/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu FONTCONFIG_FILE=$V/fonts.conf
for c in ${@:-A B}; do
  P=$([ $c = B ] && echo 29858 || echo 29857)
  (setsid python3 $D/playd-gpu.py $P local http://127.0.0.1:28454/index.html $D/prof-$c > $D/playd-$c.log 2>&1 < /dev/null &)
done
