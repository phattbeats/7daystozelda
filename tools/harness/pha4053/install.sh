#!/bin/bash
B=/tmp/wOOT-True-Co-op/build-web/soh
cd /tmp/z4053/t/serve
cp $B/soh.js soh.js; cp $B/soh.wasm soh.wasm
V=$(md5sum soh.js | cut -c1-8)
sed -i "s/soh\.js?v=[0-9a-f]*/soh.js?v=$V/" index.html
echo v=$V
