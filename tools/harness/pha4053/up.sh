#!/bin/bash
D=/tmp/z4053/t
V=/tmp/z4053/vlibs
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(cd /tmp/z4053/repo/web && PORT=18453 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43453 PUBLIC_DIR=$D/serve setsid node /tmp/z4053/repo/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
export DEBUG=pw:browser __EGL_VENDOR_LIBRARY_FILENAMES=$V/root/egl/10_nvidia.json LD_LIBRARY_PATH=$V/root/usr/lib/x86_64-linux-gnu:$V/root/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu FONTCONFIG_FILE=$V/fonts.conf
for c in ${@:-A B}; do
  P=$([ $c = B ] && echo 19863 || echo 19853)
  (setsid python3 $D/playd-gpu.py $P local http://127.0.0.1:18453/index.html $D/prof-$c > $D/playd-$c.log 2>&1 < /dev/null &)
done
