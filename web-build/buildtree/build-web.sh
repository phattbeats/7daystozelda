#!/bin/bash
# Incremental web build. Edits under /root/OOT-True-Co-op (soh/, libultraship/,
# ...) recompile only what changed. Output lands in /out/public-update/: copy
# those files over the bundle's public/ and redeploy.
set -euo pipefail
source /root/emsdk/emsdk_env.sh >/dev/null 2>&1
cd /root/OOT-True-Co-op/build-web
# A CMake re-run re-downloads stb_image.h; restore the known-good copy if it
# came back empty, or libultraship fails to compile.
ninja build.ninja >/dev/null 2>&1 || true
if [ ! -s _deps/stb/stb_image.h ]; then cp -p /root/stb_good.h _deps/stb/stb_image.h; fi
# shell.html isn't a link dependency: force the relink so page edits ship.
rm -f soh/soh.html
ninja -j"${JOBS:-4}" soh
mkdir -p /out/public-update
cp soh/soh.html /out/public-update/index.html
cp soh/soh.js soh/soh.wasm soh/soh.data /out/public-update/
cd /out/public-update && for f in index.html soh.js soh.wasm soh.data; do gzip -9 -k -n -f "$f"; done
echo "Built. Copy /out/public-update/* over the bundle's public/ (soh.o2r and icons stay as they are)."
