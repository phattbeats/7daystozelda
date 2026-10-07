#!/bin/bash
# joinE.sh: a fifth client joins the running room late and must receive the whole base
cd /tmp/z4062/t
. /tmp/vlibs4055/gpu-env.sh; export DEBUG=pw:browser
(env VW=640 VH=360 setsid python3 /tmp/z4062/t/playd-gpu.py 19465 local http://127.0.0.1:18471/index.html /tmp/z4062/t/prof-E > /tmp/z4062/t/playd-E.log 2>&1 < /dev/null &)
sleep 25
python3 x.py E "NAME='Pix'; TUNIC='9B59B6'
exec(open('/tmp/z4062/t/boot2.py').read())" 2>&1 | tail -2
python3 x.py E 'b=B(); print("E sees", len([p for p in b["placeables"] if p["scene"]==81]), "pieces, actors", b.get("placeableActors"), "scene", b.get("scene"), b.get("dyna"))'
