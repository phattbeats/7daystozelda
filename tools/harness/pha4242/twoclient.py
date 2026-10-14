"""PHA-4242 regression + two-client test on the live origin with the new page swapped in.

A creates a private lobby and enters it; B joins with the code. Both must end up in the same
lb-CODE room (lobby info lists both players). Then B leaves (opens the site without the hash) and
must land back on the lobby page; old #room=<name> links must not join. Local GPU Chrome.
usage: python3 twoclient.py OUTDIR
"""
import os, sys, time, json, re
from playwright.sync_api import sync_playwright

OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
KEY = open("/tmp/pha4242/.key").read().strip()
BASE = "https://zelda.phatt.vip/"
BUILD = "/tmp/pha4242/build/public"
O2R = "/tmp/z4177/t/oot.o2r"
EXE = os.environ["CHROME_EXE"]
LOG = open(OUT + "/log.txt", "a")


def log(*a):
    s = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + "\n"); LOG.flush()


def override(route):
    rq = route.request; u = rq.url.split("#")[0]
    path = u.split("zelda.phatt.vip", 1)[1].split("?")[0] if "zelda.phatt.vip" in u else None
    if path is not None and rq.resource_type == "document" and path in ("/", "/index.html"):
        return route.fulfill(status=200, body=open(BUILD + "/index.html", "rb").read(), headers={"content-type": "text/html; charset=utf-8", "cache-control": "no-store"})
    if path == "/hdpack.js":
        return route.fulfill(status=200, body=open(BUILD + "/hdpack.js", "rb").read(), headers={"content-type": "text/javascript"})
    if path and path.startswith("/fonts/"):
        return route.fulfill(status=200, body=open(BUILD + path, "rb").read(), headers={"content-type": "font/woff2"})
    route.continue_()


def client(pw, name, prof):
    ctx = pw.chromium.launch_persistent_context(prof, executable_path=EXE, viewport={"width": 960, "height": 720},
        args=["--use-gl=angle", "--use-angle=gl-egl", "--enable-gpu", "--no-sandbox", "--ignore-gpu-blocklist",
              "--autoplay-policy=no-user-gesture-required", "--enable-features=SharedArrayBuffer"])
    pg = ctx.pages[0] if ctx.pages else ctx.new_page()
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)[:200]))
    pg.goto(BASE + "?key=" + KEY, wait_until="commit")   # real visit: invite cookie
    pg.route("**/*", override)
    pg.goto(BASE + "?key=" + KEY, wait_until="domcontentloaded"); time.sleep(4)
    if not pg.is_visible("#rom-ready"):
        for _ in range(60):
            if pg.query_selector("#rom-need:not([hidden])"): break
            time.sleep(1)
        pg.set_input_files("#rom-file", O2R); log(name, "rom fed")
        for _ in range(180):
            time.sleep(2)
            try:
                if pg.is_visible("#rom-ready"): break
            except Exception: pass
    log(name, "rom ready:", pg.is_visible("#rom-ready"), "solo enabled:", not pg.is_disabled("#btn-solo"))
    for _ in range(120):
        if not pg.is_disabled("#btn-solo"): break
        time.sleep(1)
    pg.fill("#f-name", name)
    return ctx, pg, errs


def lobby_info(pg, code):
    return pg.evaluate("c=>fetch('api/lobby/info?code='+c).then(r=>r.json())", code)


with sync_playwright() as pw:
    A, pa, ea = client(pw, "Alpha", "/tmp/pha4242/prof-A")
    B, pb, eb = client(pw, "Bravo", "/tmp/pha4242/prof-B")
    pa.screenshot(path=OUT + "/A-lobby.png"); pb.screenshot(path=OUT + "/B-lobby.png")

    # A: private lobby, enter it
    pa.click("#btn-make"); time.sleep(3)
    code = pa.text_content("#mm-made-code").strip(); log("A made lobby", code, pa.text_content("#mm-made-vis"))
    pa.screenshot(path=OUT + "/A-made.png")
    pa.click("#btn-enter"); time.sleep(4)
    log("A hash", pa.evaluate("location.hash"), "overlay hidden", pa.evaluate("document.getElementById('overlay').classList.contains('hidden')"),
        "anchor room", pa.evaluate("window._anchorConfig&&window._anchorConfig.room"))

    # B: join by code
    pb.fill("#mm-code", code); time.sleep(0.5); pb.click("#btn-code"); time.sleep(4)
    log("B hash", pb.evaluate("location.hash"), "anchor room", pb.evaluate("window._anchorConfig&&window._anchorConfig.room"), "note", pb.text_content("#mm-note"))

    # first join can sit on a black canvas: reload both (auto-join from the hash), then wait for the relay
    time.sleep(20)
    for pg in (pa, pb): pg.reload(wait_until="domcontentloaded")
    seen = None
    for i in range(60):
        time.sleep(5)
        try:
            info = lobby_info(pa, code)
            players = info.get("players") or []
            ra = pa.evaluate("localStorage.getItem('soh.roster.'+(window._anchorConfig||{}).room)||localStorage.getItem('roster.'+(window._anchorConfig||{}).room)")
            if i % 4 == 0: log("lobby", code, "players", players, "A roster", (ra or "")[:160])
            if "Alpha" in json.dumps(players) and "Bravo" in json.dumps(players):
                seen = players; break
        except Exception as e:
            log("poll err", str(e)[:120])
    log("BOTH IN LOBBY:", bool(seen), seen)
    pa.screenshot(path=OUT + "/A-ingame.png"); pb.screenshot(path=OUT + "/B-ingame.png")
    pills = [pg.evaluate("(document.getElementById('net-text')||{}).textContent") for pg in (pa, pb)]
    log("net pills", pills)

    # leave: B opens the site without the hash -> back on the lobby page
    pb.goto("about:blank"); pb.goto(BASE, wait_until="domcontentloaded"); time.sleep(5)
    log("B after leave: overlay visible", pb.is_visible("#overlay"), "hidden-class", pb.evaluate("document.getElementById('overlay').classList.contains('hidden')"),
        "anchor", pb.evaluate("!!window._anchorConfig"))
    pb.screenshot(path=OUT + "/B-left.png")
    # old named-room link must not join
    pb.goto("about:blank"); pb.goto(BASE + "#room=boys&name=Bravo", wait_until="domcontentloaded"); time.sleep(5)
    log("B named-room link: overlay hidden", pb.evaluate("document.getElementById('overlay').classList.contains('hidden')"),
        "f-room", repr(pb.eval_on_selector("#f-room", "e=>e.value")), "anchor", pb.evaluate("!!window._anchorConfig"))
    # arcade popup opens
    pb.evaluate("document.querySelector('[data-phatt-arcade]').shadowRoot.querySelector('button.open').click()"); time.sleep(1)
    log("arcade dialog open", pb.evaluate("document.querySelector('[data-phatt-arcade]').shadowRoot.querySelector('dialog').open"))
    pb.screenshot(path=OUT + "/B-arcade.png")
    log("page errors A", ea, "B", eb)
    A.close(); B.close()
