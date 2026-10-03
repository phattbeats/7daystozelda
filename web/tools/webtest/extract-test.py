import asyncio, os, sys, time
from playwright.async_api import async_playwright
ROM = "" + os.environ.get("WEBTEST_DIR", os.path.expanduser("~/webtest-work")) + "/zelda/oot-rev2.z64"
BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:18443/?key=testkey#room=boys"
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(args=["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
        pg = await b.new_page()
        logs = []
        pg.on("console", lambda m: logs.append(m.text)); pg.on("pageerror", lambda e: logs.append("PAGEERROR " + str(e)[:400]))
        await pg.goto(BASE)
        await pg.wait_for_function("window.runtimeReady && window.sohInstalled", timeout=180000)
        t0 = time.time()
        await pg.set_input_files("#rom-file", ROM)
        # success = page reloads into "ready"; failure = error text
        while time.time() - t0 < 900:
            await asyncio.sleep(3)
            try:
                st = await asyncio.wait_for(pg.evaluate("JSON.stringify({e: (document.getElementById('error-msg')||{}).textContent, ready: !!document.getElementById('rom-ready') && !document.getElementById('rom-ready').hidden, s: (document.getElementById('status-line')||{}).textContent})"), 10)
            except Exception as ex:
                continue
            if '"e":""' not in st or '"ready":true' in st:
                break
        print(f"after {time.time()-t0:.0f}s:", st)
        keys = await pg.evaluate("listCachedFiles()")
        print("cached:", keys)
        for l in [l for l in logs if any(k in l for k in ("Extract", "Abort", "PAGEERROR", "ROM", "o2r", "RuntimeError", "at "))][-25:]:
            print("  ", l[:300])
        await b.close()
asyncio.run(main())
