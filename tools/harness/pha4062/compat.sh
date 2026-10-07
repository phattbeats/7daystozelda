#!/bin/bash
# compat.sh: a base saved by the STOCK build (main 42e8dc0) must load on the fix build; then a vanilla pass (7 Days off) in the same co-op room
cd /tmp/z4062/t
echo "##### stock build: 94-piece base, heap size, save"
./restart.sh serve-base > restart.out 2>&1
python3 x.py A -f fortbuild.py 2>&1 | tail -1 | cut -c1-200
for c in A B; do python3 x.py $c 'print(CLIENT, "stock wasmHeapMB", round(ev("Module.HEAP8.length")/1048576,1), "jsHeapMB", ev("Math.round(performance.memory.usedJSHeapSize/1048576)"))' | grep stock; done
python3 x.py A 'ev("Module.ccall(\"sevendays_test_save\",null,[],[])"); time.sleep(1.5); ev("sohPersist()"); time.sleep(4); print("saved", len([p for p in B()["placeables"] if p["scene"]==81]))' 2>&1 | grep "saved\|Error"
echo "##### fix build loads the stock save"
./restart.sh serve-final > restart.out 2>&1
sleep 8
python3 x.py A 'b=B(); print("fix build sees", len([p for p in b["placeables"] if p["scene"]==81]), "pieces; scene", b.get("scene"), "actors", b.get("placeableActors"), b["dyna"])' 2>&1 | grep "fix build"
for c in A B; do python3 x.py $c 'print(CLIENT, "fix wasmHeapMB", round(ev("Module.HEAP8.length")/1048576,1), "jsHeapMB", ev("Math.round(performance.memory.usedJSHeapSize/1048576)"))' | grep fix; done
echo "##### vanilla pass: 7 Days to Zelda off in the same co-op room"
for c in A B C D; do python3 x.py $c 'exec(open("/tmp/z4062/t/gfx.py").read())
cv("gSevenDays.Enabled",0); cv("gSevenDays.Base",0); time.sleep(5); GFX("reset"); f=FPS(5); time.sleep(10); g=GFX()
print(CLIENT, "vanilla fps", round(f,1), "frames", g["frames"], "overflow", g["overflowFrames"], "skips", g["actorSkips"], g["placeableSkips"], "peakOpa", g["opa"]["peakUsed"], "net", ev("document.querySelector(\"#net-text\").textContent"))' 2>&1 | grep vanilla & done; wait
grep -h "Unhandled OP\|PAGE CRASH\|SEGV\|PAGEERROR" playd-?.log | cut -c1-200 | head -5
echo COMPATDONE
