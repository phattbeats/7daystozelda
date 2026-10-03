import asyncio, os, sys, time
from playwright.async_api import async_playwright
S = os.environ.get("WEBTEST_DIR", os.path.expanduser("~/webtest-work"))
URL = sys.argv[1]; TAG = sys.argv[2]; ROOM = sys.argv[3] if len(sys.argv) > 3 else "crashtest"
OUT = S + "/render"
async def player(p, prof, delay, results):
    await asyncio.sleep(delay)
    ctx = await p.chromium.launch_persistent_context(S + "/prof-" + prof, headless=True, viewport={"width": 800, "height": 600},
        args=["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
    pg = ctx.pages[0] if ctx.pages else await ctx.new_page()
    logs = []
    pg.on("console", lambda m: logs.append(f"{time.strftime('%H:%M:%S')} {m.text}"))
    pg.on("pageerror", lambda e: logs.append(f"{time.strftime('%H:%M:%S')} PAGEERROR {e}\n{getattr(e, 'stack', '')}"))
    pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
    await pg.goto(URL)
    await pg.wait_for_function("window.runtimeReady && window.sohInstalled && window.romReady", timeout=180000)
    await pg.fill("#f-name", prof); await pg.fill("#f-room", ROOM); await pg.click("#btn-join")
    await asyncio.sleep(35)
    async def key(k):
        await pg.focus("#canvas"); await pg.keyboard.down(k); await asyncio.sleep(0.35); await pg.keyboard.up(k)
    await key("x"); await asyncio.sleep(5); await key("x")
    print(prof, "loading save", time.strftime('%H:%M:%S'), flush=True)
    await asyncio.sleep(25)
    # walk out the front door (behind the camera) into Kokiri Forest
    await pg.focus("#canvas"); await pg.keyboard.down("s"); await asyncio.sleep(9); await pg.keyboard.up("s")
    print(prof, "walked out", time.strftime('%H:%M:%S'), flush=True)
    for i in range(10):
        await asyncio.sleep(10)
        try:
            await pg.screenshot(path=f"{OUT}/{TAG}-{prof}-{i}.png", timeout=15000)
        except Exception as e:
            pass
        if any("PAGEERROR" in l or "Aborted" in l for l in logs):
            break
    results[prof] = logs
    open(f"{OUT}/{TAG}-{prof}.log", "w").write("\n".join(logs))
    await ctx.close()
async def main():
    res = {}
    async with async_playwright() as p:
        await asyncio.gather(player(p, "A", 0, res), player(p, "B", 110, res))
    for prof, logs in res.items():
        crit = [l for l in logs if any(k in l for k in ("PAGEERROR", "Abort", "RuntimeError", "out of bounds", "[Anchor]", "Anchor", "Connected", "crash"))]
        print(f"=== {prof}: {len(logs)} console lines")
        for l in crit[-20:]: print("  ", l[:1500])
asyncio.run(main())
