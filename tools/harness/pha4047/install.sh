#!/bin/bash
# copy the current build into serve/ with a fresh soh.js?v=
B=/tmp/mOOT-True-Co-op/build-web/soh
cd /tmp/z4047/t/serve
cp $B/soh.js soh.js; cp $B/soh.wasm soh.wasm
V=$(md5sum soh.js | cut -c1-8)
sed -i "s/soh\.js?v=[0-9a-f]*/soh.js?v=$V/" index.html
echo v=$V
