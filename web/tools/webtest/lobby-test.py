import asyncio, struct, os
from playwright.async_api import async_playwright
T = os.path.dirname(os.path.abspath(__file__))
BASE = "http://127.0.0.1:18443/"
fails = 0
def check(ok, what):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    if not ok: fails += 1

def fake_rom(title=b"THE LEGEND OF ZELDA ", size=8 * 1024 * 1024):
    b = bytearray(size)
    b[0:4] = bytes.fromhex("80371240")
    b[0x20:0x20 + len(title)] = title
    for i in range(0x1000, size, 4097): b[i] = (i * 7) & 0xFF
    return bytes(b)

def to_v64(b):
    out = bytearray(len(b)); out[0::2] = b[1::2]; out[1::2] = b[0::2]; return bytes(out)

async def wait_engine(page):
    await page.wait_for_function("window.runtimeReady === true && window.sohInstalled === true", timeout=120000)

async def main():
    rom_v64 = os.path.join(T, "fake.v64"); open(rom_v64, "wb").write(to_v64(fake_rom()))
    junk = os.path.join(T, "notes.z64"); open(junk, "wb").write(b"hello" * 2000)
    async with async_playwright() as p:
        browser = await p.chromium.launch(args=["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"])
        ctx = await browser.new_context(viewport={"width": 1280, "height": 860})
        page = await ctx.new_page()
        errors, logs = [], []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.on("console", lambda m: logs.append(m.text))
        dialogs = []
        async def on_dialog(d):
            dialogs.append(d.default_value or d.message); await d.dismiss()
        page.on("dialog", lambda d: asyncio.ensure_future(on_dialog(d)))

        await page.goto(BASE + "?key=testkey#room=boys&horde=1")
        # Seed a stale soh.o2r cache entry like the old build left behind.
        await page.evaluate("""() => new Promise(r => { const q = indexedDB.open('soh_otr_files', 1);
            q.onupgradeneeded = e => e.target.result.createObjectStore('otr');
            q.onsuccess = e => { const tx = e.target.result.transaction('otr','readwrite'); tx.objectStore('otr').put(new ArrayBuffer(8), 'soh.o2r'); tx.oncomplete = r; }; })""")
        await page.reload()
        await wait_engine(page)
        keys = await page.evaluate("listCachedFiles()")
        check("soh.o2r" not in keys, f"stale soh.o2r cache entry removed (cache now {keys})")
        check(await page.is_visible("#drop"), "no ROM yet: drop zone shown")
        check(await page.input_value("#f-room") == "boys" and await page.is_checked("#f-horde"), "room and horde prefilled from the invite link")
        check(await page.is_disabled("#btn-join") and await page.is_disabled("#btn-solo"), "Join/Solo disabled without a ROM")
        await page.screenshot(path=os.path.join(T, "lobby-desktop.png"), full_page=True)

        await page.fill("#f-name", "Brandon")
        check(await page.is_disabled("#btn-join"), "Join stays disabled until a ROM is ready")
        await page.click("#btn-invite"); await asyncio.sleep(0.5)
        invite = dialogs[-1] if dialogs else ""
        check(invite.endswith("/?key=testkey#room=boys&horde=1"), f"invite link: {invite}")

        await page.set_input_files("#rom-file", junk); await asyncio.sleep(0.5)
        check("isn't an N64 ROM" in await page.text_content("#error-msg"), "junk file rejected with a clear message")

        await page.set_input_files("#rom-file", rom_v64)
        await page.wait_for_function("document.getElementById('error-msg').textContent.length > 0 || !document.getElementById('rom-ready').hidden", timeout=120000)
        err = await page.text_content("#error-msg")
        check("version Ship of Harkinian supports" in err, f"byte-swapped .v64 normalized and handed to the extractor; unknown dump refused: '{err[:60]}...'")
        check(await page.is_hidden("#crash"), "no crash card after extractor refusal")
        check(any("ROM" in l or "Extract" in l or "rom" in l for l in logs), "extractor ran")

        # Settings persistence across reloads.
        await page.evaluate("""() => { FS.writeFile('/shipofharkinian.json', JSON.stringify({Window:{Fullscreen:{Enabled:true}, Width: 640}, CVars:{gTest:{Value:7}}})); window.sohPersist(); }""")
        await asyncio.sleep(1.5)
        await page.reload()
        await page.wait_for_function("window._sohIdbReady === true", timeout=60000)
        cfg = await page.evaluate("() => { try { return JSON.parse(new TextDecoder().decode(FS.readFile('/shipofharkinian.json'))); } catch (e) { return null; } }")
        check(cfg is not None and cfg.get("CVars", {}).get("gTest", {}).get("Value") == 7, "settings file restored after reload")
        check(cfg is not None and "Fullscreen" not in cfg.get("Window", {}), "fullscreen flag stripped on restore")
        check(await page.input_value("#f-name") == "", "name not in link, so not prefilled unless remembered (fine)")

        await wait_engine(page)
        ok = await page.evaluate("() => { try { Module._web_set_hidden(1); Module._web_set_hidden(0); return true; } catch (e) { return String(e); } }")
        check(ok is True, f"background-tab loop switch callable: {ok}")

        await page.evaluate("() => { NetPill.start('boys'); window.SohNet.state(1); }")
        t1 = await page.text_content("#net-text")
        await page.evaluate("() => window.SohNet.state(2)")
        t2 = await page.text_content("#net-text")
        await page.evaluate("() => window.SohNet.state(0)")
        t3 = await page.text_content("#net-text")
        check(t1.startswith("Connecting") and t2 == "boys" and t3.startswith("Disconnected"), f"status pill: '{t1}' -> '{t2}' -> '{t3}'")

        m = await browser.new_page(viewport={"width": 390, "height": 844}, is_mobile=True, has_touch=True)
        await m.goto(BASE + "?key=testkey#room=boys&horde=1")
        await asyncio.sleep(2)
        await m.screenshot(path=os.path.join(T, "lobby-mobile.png"), full_page=True)
        noscroll = await m.evaluate("document.documentElement.scrollWidth <= window.innerWidth")
        check(noscroll, "mobile: no horizontal overflow")
        await browser.close()
    check(not errors, f"no page errors {errors[:3]}")
    print("LOBBY:", "ALL PASS" if not fails else f"{fails} FAILED")

asyncio.run(main())
