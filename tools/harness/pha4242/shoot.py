"""Capture the 7DtZ lobby page at phone / tablet / desktop through browserless.

usage: python3 shoot.py <url-without-key> <outdir> [prefix]
Each size gets: <prefix>-<size>-top.png (first screen) and <prefix>-<size>-full.png (whole
overlay, by growing the viewport to the overlay's scroll height, since #overlay is position:fixed).
"""
import sys, os, asyncio
from playwright.async_api import async_playwright

URL, OUT = sys.argv[1], sys.argv[2]
PREFIX = sys.argv[3] if len(sys.argv) > 3 else "shot"
KEY = open("/tmp/pha4242/.key").read().strip()
SIZES = {
    "phone": dict(viewport={"width": 390, "height": 844}, device_scale_factor=2, is_mobile=True, has_touch=True,
                  user_agent="Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"),
    "tablet": dict(viewport={"width": 820, "height": 1180}, device_scale_factor=2, is_mobile=True, has_touch=True,
                   user_agent="Mozilla/5.0 (iPad; CPU OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"),
    "desktop": dict(viewport={"width": 1440, "height": 900}, device_scale_factor=1),
}
STATE = os.environ.get("STATE", "")  # "ready" fakes a loaded ROM so the enabled buttons show


async def main():
    os.makedirs(OUT, exist_ok=True)
    async with async_playwright() as p:
        b = await p.chromium.connect_over_cdp("ws://10.0.0.100:3000")
        for name, opts in SIZES.items():
            if os.environ.get("ONLY") and name not in os.environ["ONLY"].split(","):
                continue
            ctx = await b.new_context(**opts)
            pg = await ctx.new_page()
            if os.environ.get("OVERRIDE"):
                body = open(os.environ["OVERRIDE"], "rb").read()
                armed = {"on": False}
                async def handler(route):
                    rq = route.request
                    if armed["on"] and rq.resource_type == "document" and rq.url.startswith("https://zelda.phatt.vip/"):
                        return await route.fulfill(status=200, body=body, headers={"content-type": "text/html; charset=utf-8", "cache-control": "no-store"})
                    if "/fonts/" in rq.url and rq.url.split("?")[0].endswith(".woff2"):
                        f = "/tmp/pha4242/design/" + rq.url.split("/fonts/")[1].split("?")[0]
                        return await route.fulfill(status=200, body=open(f, "rb").read(), headers={"content-type": "font/woff2"})
                    await route.continue_()
                await pg.route("**/*", handler)
                # first a real visit so the invite cookie is set, then the override
                await pg.goto(URL + ("&" if "?" in URL else "?") + "key=" + KEY, wait_until="commit", timeout=60000)
                armed["on"] = True
            sep = "&" if "?" in URL else "?"
            await pg.goto(URL + sep + "key=" + KEY, wait_until="domcontentloaded", timeout=60000)
            await pg.wait_for_timeout(int(os.environ.get("WAIT", "6000")))
            if STATE == "ready":
                await pg.evaluate("""()=>{const q=s=>document.getElementById(s);
                  q('rom-loading')&&(q('rom-loading').hidden=true);q('rom-need')&&(q('rom-need').hidden=true);
                  q('rom-ready')&&(q('rom-ready').hidden=false);
                  ['btn-join','btn-solo','btn-quick','btn-code'].forEach(i=>q(i)&&(q(i).disabled=false));}""")
            r = await pg.evaluate("()=>{const r=document.getElementById('rotate');r&&(r.hidden=true);return 1}")
            await pg.wait_for_timeout(400)
            await pg.screenshot(path=f"{OUT}/{PREFIX}-{name}-top.png")
            h = await pg.evaluate("()=>{const o=document.getElementById('overlay');return o?o.scrollHeight:document.body.scrollHeight}")
            await pg.set_viewport_size({"width": opts["viewport"]["width"], "height": min(int(h), 6000)})
            await pg.wait_for_timeout(600)
            await pg.screenshot(path=f"{OUT}/{PREFIX}-{name}-full.png")
            print(name, "full height", h)
            await ctx.close()
        await b.close()

asyncio.run(main())
