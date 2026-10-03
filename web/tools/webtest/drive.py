# Drive one persistent browser profile through a list of steps, screenshotting as it goes.
# usage: drive.py <profile> <url> <steps...>   steps: key:<k>[*n] | wait:<sec> | shot:<name> | boot | cfg
import asyncio, os, sys, json, time
from playwright.async_api import async_playwright
S = os.environ.get("WEBTEST_DIR", os.path.expanduser("~/webtest-work"))
OUT = S + "/render"; os.makedirs(OUT, exist_ok=True)
CFG = {"CVars": {"gEnhancements": {"TimeSavers": {"SkipCutscene": {"Intro": 1}}}}}
prof, url, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
async def main():
    async with async_playwright() as p:
        ctx = await p.chromium.launch_persistent_context(S + "/prof-" + prof, headless=True, viewport={"width": 800, "height": 600},
            args=["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
        pg = ctx.pages[0] if ctx.pages else await ctx.new_page()
        logs = []
        pg.on("console", lambda m: logs.append(m.text)); pg.on("pageerror", lambda e: logs.append("PAGEERROR " + str(e)))
        pg.on("dialog", lambda d: asyncio.ensure_future(d.accept()))
        await pg.goto(url)
        await pg.wait_for_function("window.runtimeReady && window.sohInstalled", timeout=180000)
        for st in steps:
            kind, _, arg = st.partition(":")
            if kind == "o2r":
                if not await pg.evaluate("window.romReady"):
                    await pg.set_input_files("#rom-file", S + "/o2rpick/oot.o2r")
                    await pg.wait_for_function("window.romReady === true", timeout=120000)
            elif kind == "cfg":
                await pg.evaluate("cfg => { let c = {}; try { c = JSON.parse(new TextDecoder().decode(FS.readFile('/shipofharkinian.json'))); } catch (e) {} c.CVars = Object.assign(c.CVars || {}, cfg.CVars); FS.writeFile('/shipofharkinian.json', JSON.stringify(c)); }", CFG)
            elif kind == "solo":
                await pg.click("#btn-solo")
            elif kind == "join":
                await pg.fill("#f-name", arg or prof); await pg.fill("#f-room", "crashtest"); await pg.click("#btn-join")
            elif kind == "wait":
                await asyncio.sleep(float(arg))
            elif kind == "key":
                k, _, n = arg.partition("*"); k = {"space": " ", "enter": "Enter"}.get(k, k)
                for _ in range(int(n or 1)):
                    await pg.focus("#canvas"); await pg.keyboard.down(k); await asyncio.sleep(0.35); await pg.keyboard.up(k); await asyncio.sleep(1.2)
            elif kind == "shot":
                await pg.screenshot(path=f"{OUT}/{prof}-{arg}.png")
        bad = [l for l in logs if "PAGEERROR" in l or "Abort" in l or "RuntimeError" in l]
        print(prof, "errors:", bad[:3])
        await pg.evaluate("window.sohPersist && window.sohPersist()"); await asyncio.sleep(2)
        await ctx.close()
asyncio.run(main())
