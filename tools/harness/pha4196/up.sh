#!/bin/bash
D=/tmp/z4196/t
V=/tmp/z4196/vlibs
cd $D/anchor && (setsid ./anchor > $D/anchor.log 2>&1 < /dev/null &)
(cd /paperclip/build/7dtz/repo/web && PORT=18496 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43496 PUBLIC_DIR=$D/serve setsid node /paperclip/build/7dtz/repo/web/server.js > $D/server.log 2>&1 < /dev/null &)
sleep 2
export TMPDIR=/tmp/pha4196/tmp DEBUG=pw:browser __EGL_VENDOR_LIBRARY_FILENAMES=$V/root/egl/10_nvidia.json LD_LIBRARY_PATH=$V/root/usr/lib/x86_64-linux-gnu:$V/root/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu FONTCONFIG_FILE=$V/fonts.conf
for c in ${@:-A B}; do
  P=$([ $c = B ] && echo 19897 || echo 19896)
  (setsid python3 $D/playd-gpu.py $P local http://127.0.0.1:18481/index.html $D/prof-$c > $D/playd-$c.log 2>&1 < /dev/null &)
done
