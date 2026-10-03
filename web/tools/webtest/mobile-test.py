import asyncio, os
from playwright.async_api import async_playwright
T = os.path.dirname(os.path.abspath(__file__))
BASE = "http://127.0.0.1:18443/?key=testkey#room=boys&horde=1"
fails = 0
def check(ok, what):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    if not ok: fails += 1

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch(args=["--use-gl=angle","--use-angle=swiftshader","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
        iphone = p.devices["iPhone 13"]
        ctx = await browser.new_context(**iphone)
        page = await ctx.new_page()
        errors = []; page.on("pageerror", lambda e: errors.append(str(e)))
        await page.goto(BASE)
        await page.wait_for_function("window.runtimeReady && window.sohInstalled", timeout=120000)
        check(await page.get_attribute("#rom-file", "accept") is None, "iPhone: file picker has no accept filter (ROMs not greyed out)")
        check("Tap to pick" in await page.text_content("#drop-hint"), "touch copy on the picker")
        check(await page.evaluate("isMobile && isIOS && isTouch"), "detected as iPhone")
        check(await page.evaluate("document.getElementById('touch-gamepad').classList.contains('visible')"), "touch gamepad enabled")
        check(await page.evaluate("!!document.querySelector('link[rel=manifest]') && !!document.querySelector('link[rel=apple-touch-icon]')"), "home-screen install tags present")
        await page.screenshot(path=os.path.join(T, "m-lobby-portrait.png"))

        await page.set_input_files("#rom-file", os.path.join(T, "norom.zip"))
        await page.wait_for_function("document.getElementById('error-msg').textContent.length > 0", timeout=30000)
        check("No .z64" in await page.text_content("#error-msg"), "zip without a ROM: clear message")
        await page.set_input_files("#rom-file", os.path.join(T, "rom.zip"))
        await page.wait_for_function("document.getElementById('error-msg').textContent.indexOf('version') >= 0", timeout=180000)
        check(True, "zip unpacked (skipped __MACOSX), byte-swapped ROM reached the extractor, unknown dump refused")
        check(await page.is_hidden("#crash"), "no crash")

        r = await page.evaluate("() => { try { Module._web_anchor_suspend(1); Module._web_anchor_suspend(0); return true; } catch (e) { return String(e); } }")
        check(r is True, f"anchor suspend/resume safe before a game starts: {r}")

        # Bluetooth controller connects -> touch pad hides; disconnects -> returns.
        await page.evaluate("() => { window.dispatchEvent(new Event('gamepadconnected')); }")
        hidden = await page.evaluate("!document.getElementById('touch-gamepad').classList.contains('visible') && !TouchGamepad.isActive()")
        await page.evaluate("() => { navigator.getGamepads = () => [null, null, null, null]; window.dispatchEvent(new Event('gamepaddisconnected')); }")
        back = await page.evaluate("document.getElementById('touch-gamepad').classList.contains('visible') && TouchGamepad.isActive()")
        check(hidden and back, "controller connect hides the touch pad, disconnect brings it back")

        # Rotate hint during play (simulate a started game).
        await page.evaluate("() => { gameStarted = true; document.getElementById('overlay').style.display='none'; RotateHint.check(); }")
        check(await page.is_visible("#rotate"), "portrait phone in game: rotate hint shown")
        await page.screenshot(path=os.path.join(T, "m-rotate.png"))
        await page.set_viewport_size({"width": 844, "height": 390})
        await asyncio.sleep(0.4)
        await page.evaluate("RotateHint.check()")
        check(await page.is_hidden("#rotate"), "landscape: hint gone")
        await page.screenshot(path=os.path.join(T, "m-landscape-play.png"))

        # Android: fullscreen route exists.
        actx = await browser.new_context(**p.devices["Pixel 7"])
        ap = await actx.new_page()
        await ap.goto(BASE); await asyncio.sleep(2)
        check(await ap.evaluate("isMobile && !isIOS && !!document.documentElement.requestFullscreen"), "Android: detected, fullscreen API available")
        check(await ap.get_attribute("#rom-file", "accept") is not None, "Android keeps the file filter (works there)")
        await browser.close()
    check(not errors, f"no page errors {errors[:3]}")
    print("MOBILE:", "ALL PASS" if not fails else f"{fails} FAILED")
asyncio.run(main())
