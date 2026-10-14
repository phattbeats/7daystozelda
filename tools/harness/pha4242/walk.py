"""Walkthrough video of the lobby page via browserless CDP screencast.
usage: OVERRIDE=index.html python3 walk.py <phone|desktop> <out.mp4>"""
import sys, os, asyncio, base64, subprocess, time, shutil
from playwright.async_api import async_playwright

SIZE, OUT = sys.argv[1], sys.argv[2]
KEY = open("/tmp/pha4242/.key").read().strip()
URL = "https://zelda.phatt.vip/?key=" + KEY
OPTS = {
    "phone": dict(viewport={"width": 390, "height": 844}, device_scale_factor=2, is_mobile=True, has_touch=True,
                  user_agent="Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"),
    "desktop": dict(viewport={"width": 1440, "height": 900}),
}[SIZE]
FR = "/tmp/pha4242/frames-" + SIZE


async def main():
    shutil.rmtree(FR, ignore_errors=True); os.makedirs(FR)
    async with async_playwright() as p:
        b = await p.chromium.connect_over_cdp("ws://10.0.0.100:3000")
        ctx = await b.new_context(**OPTS)
        pg = await ctx.new_page()
        body = open(os.environ["OVERRIDE"], "rb").read() if os.environ.get("OVERRIDE") else None
        armed = {"on": False}

        async def handler(route):
            rq = route.request
            if body and armed["on"] and rq.resource_type == "document" and rq.url.startswith("https://zelda.phatt.vip/"):
                return await route.fulfill(status=200, body=body, headers={"content-type": "text/html; charset=utf-8"})
            if "/fonts/" in rq.url and rq.url.split("?")[0].endswith(".woff2"):
                f = "/tmp/pha4242/design/" + rq.url.split("/fonts/")[1].split("?")[0]
                return await route.fulfill(status=200, body=open(f, "rb").read(), headers={"content-type": "font/woff2"})
            await route.continue_()
        await pg.route("**/*", handler)
        await pg.goto(URL, wait_until="commit", timeout=60000)
        armed["on"] = True
        await pg.goto(URL, wait_until="domcontentloaded", timeout=60000)
        await pg.wait_for_timeout(5000)
        await pg.evaluate("()=>{const r=document.getElementById('rotate');r&&(r.hidden=true)}")

        cdp = await ctx.new_cdp_session(pg)
        frames = []

        rec = {"on": True}

        async def grab():
            while rec["on"]:
                try:
                    r = await cdp.send("Page.captureScreenshot", {"format": "jpeg", "quality": 82})
                    frames.append((time.time(), r["data"]))
                except Exception as e:
                    print("grab", e); await asyncio.sleep(0.1)
        w, h = OPTS["viewport"]["width"], OPTS["viewport"]["height"]
        task = asyncio.ensure_future(grab())

        async def scroll_to(sel, dur=1200):
            await pg.evaluate("""([s,d])=>new Promise(r=>{const o=document.getElementById('overlay');const el=document.querySelector(s);
              const t0=performance.now(),y0=o.scrollTop,y1=Math.max(0,Math.min(o.scrollHeight-o.clientHeight,el.getBoundingClientRect().top+o.scrollTop-16));
              (function f(t){const k=Math.min(1,(t-t0)/d),e=k<.5?2*k*k:1-Math.pow(-2*k+2,2)/2;o.scrollTop=y0+(y1-y0)*e;k<1?requestAnimationFrame(f):r()})(t0)})""", [sel, dur])
            await pg.wait_for_timeout(700)

        await pg.wait_for_timeout(1500)
        # step through the page: ROM, name, colours, play, lobby code, controls tabs, guides, arcade
        await scroll_to("#rom-h"); await pg.wait_for_timeout(800)
        await scroll_to("#link-h")
        await pg.click("#f-name"); await pg.keyboard.type("Saria", delay=90); await pg.wait_for_timeout(400)
        await scroll_to(".colors")
        await pg.click(".cpick[data-c=tunic] .swatches button:nth-child(3)"); await pg.wait_for_timeout(900)
        await scroll_to("#play-h")
        await pg.wait_for_timeout(1200)
        await scroll_to(".or")
        await pg.click("#btn-make"); await pg.wait_for_timeout(2200)
        await scroll_to(".code-h")
        await pg.click("#mm-code"); await pg.keyboard.type("ABCDE", delay=110); await pg.wait_for_timeout(700)
        await scroll_to("#btn-solo"); await pg.wait_for_timeout(900)
        await scroll_to("#ctl-h")
        for t in ("#ctl-tab-pad", "#ctl-tab-kb", "#ctl-tab-touch"):
            await pg.click(t); await pg.wait_for_timeout(1100)
        await scroll_to(".guides"); await pg.wait_for_timeout(800)
        await scroll_to("#topbar", 1500)
        await pg.evaluate("()=>document.querySelector('[data-phatt-arcade]').shadowRoot.querySelector('button.open').click()")
        await pg.wait_for_timeout(2200)
        await pg.keyboard.press("Escape"); await pg.wait_for_timeout(1200)
        rec["on"] = False; await task
        await ctx.close(); await b.close()

    # frames arrive irregularly; write a concat list with real durations
    lst = []
    for i, (t, d) in enumerate(frames):
        fn = f"{FR}/{i:05d}.jpg"; open(fn, "wb").write(base64.b64decode(d))
        dur = (frames[i + 1][0] - t) if i + 1 < len(frames) else 0.5
        lst.append(f"file '{fn}'\nduration {max(dur, 0.01):.3f}")
    lst.append(f"file '{FR}/{len(frames) - 1:05d}.jpg'")
    open(FR + "/list.txt", "w").write("\n".join(lst))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", FR + "/list.txt",
                    "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2,fps=30", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-movflags", "+faststart", OUT], check=True)
    print(OUT, len(frames), "frames")

asyncio.run(main())
