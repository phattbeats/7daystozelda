#!/bin/bash
# copy the current wasm build into serve/ with a fresh soh.js?v=  (re-run after every rebuild, then reload both clients: ./reload.sh)
B=${B:-/tmp/xOOT-True-Co-op/build-web/soh}
cd /tmp/z4054/t/serve
cp $B/soh.js soh.js; cp $B/soh.wasm soh.wasm; cmp -s $B/soh.data soh.data || cp $B/soh.data soh.data
V=$(md5sum soh.js | cut -c1-8)
sed -i "s/soh\.js?v=[0-9a-f]*/soh.js?v=$V/" index.html
echo v=$V
