import asyncio, os, sys, json
from playwright.async_api import async_playwright
S = os.environ.get("WEBTEST_DIR", os.path.expanduser("~/webtest-work"))
BASE, TAG = sys.argv[1], sys.argv[2]
O2R = sys.argv[3] if len(sys.argv) > 3 else S + "/zelda/oot-desktop.o2r"
OUT = S + "/render"; os.makedirs(OUT, exist_ok=True)
CFG = {"CVars": {"gDeveloperTools": {"DebugEnabled": 1, "BootToDebugWarpScreen": 1},
                 "gGeneral": {"BetterDebugWarpScreenCurrentScene": int(os.environ.get("SCENE", "1"))}}}
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(args=["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
        pg = await b.new_page(viewport={"width": 960, "height": 720})
        logs = []
        pg.on("console", lambda m: logs.append(m.text)); pg.on("pageerror", lambda e: logs.append("PAGEERROR " + str(e)[:300]))
        await pg.goto(BASE)
        await pg.wait_for_function("window.runtimeReady && window.sohInstalled", timeout=180000)
        await pg.set_input_files("#rom-file", S + "/o2rpick/oot.o2r")
        await pg.wait_for_function("window.romReady === true", timeout=120000)
        await pg.evaluate("cfg => FS.writeFile('/shipofharkinian.json', JSON.stringify(cfg))", CFG)
        await pg.click("#btn-solo")
        await asyncio.sleep(25)
        await pg.screenshot(path=f"{OUT}/{TAG}-0-warp.png")
        await pg.focus("#canvas")
        for _ in range(int(os.environ.get("RIGHTS", "0"))):
            await pg.keyboard.down("ArrowRight"); await asyncio.sleep(0.4); await pg.keyboard.up("ArrowRight"); await asyncio.sleep(1.5)
        await pg.screenshot(path=f"{OUT}/{TAG}-0b-warp.png")
        await pg.keyboard.down("x"); await asyncio.sleep(0.4); await pg.keyboard.up("x"); await asyncio.sleep(18)
        await pg.screenshot(path=f"{OUT}/{TAG}-1.png")
        if os.environ.get("WALK", "1") == "1":
            await pg.keyboard.down("s"); await asyncio.sleep(3.5); await pg.keyboard.up("s"); await asyncio.sleep(3)
        await pg.screenshot(path=f"{OUT}/{TAG}-2.png")
        if os.environ.get("WALK", "1") == "1":
            await pg.keyboard.down("d"); await asyncio.sleep(2.5); await pg.keyboard.up("d")
        await pg.screenshot(path=f"{OUT}/{TAG}-3.png")
        for j in range(6):
            await asyncio.sleep(8)
            await pg.screenshot(path=f"{OUT}/{TAG}-kf{j}.png")
        n = await pg.evaluate("typeof GL !== 'undefined' ? GL.counter : -1")
        print(TAG, "GL object ids handed out so far:", n)
        for l in [l for l in logs if "PAGEERROR" in l or "Abort" in l][-5:]: print("  ", l)
        await b.close()
asyncio.run(main())
