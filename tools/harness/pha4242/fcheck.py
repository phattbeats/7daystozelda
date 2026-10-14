import asyncio
from playwright.async_api import async_playwright
KEY=open('/tmp/pha4242/.key').read().strip(); BODY=open('/tmp/pha4242/index.new.html','rb').read()
async def page(b, hashpart):
    ctx=await b.new_context(); pg=await ctx.new_page(); armed={"on":False}
    async def h(route):
        rq=route.request
        if armed["on"] and rq.resource_type=="document" and rq.url.startswith("https://zelda.phatt.vip/"):
            return await route.fulfill(status=200,body=BODY,headers={"content-type":"text/html"})
        await route.continue_()
    await pg.route("**/*",h)
    await pg.goto("https://zelda.phatt.vip/?key="+KEY,wait_until="commit"); armed["on"]=True
    await pg.goto("about:blank")
    errs=[]; pg.on("pageerror",lambda e: errs.append(str(e)[:150]))
    await pg.goto("https://zelda.phatt.vip/?key="+KEY+hashpart,wait_until="domcontentloaded"); await pg.wait_for_timeout(4000)
    return ctx,pg,errs
async def main():
  async with async_playwright() as p:
    b=await p.chromium.connect_over_cdp("ws://10.0.0.100:3000")
    for hp in ["#room=boys&name=Tester","#room=lb-ABCDE&name=Tester"]:
        ctx,pg,errs=await page(b,hp)
        print(hp,"f-room=",repr(await pg.eval_on_selector("#f-room","e=>e.value")),"overlay hidden=",await pg.evaluate("document.getElementById('overlay').classList.contains('hidden')"),"errs=",errs)
        await ctx.close()
    ctx,pg,errs=await page(b,"")
    await pg.fill("#f-name","PHA4242 test")
    await pg.click("#btn-make"); await pg.wait_for_timeout(2500)
    print("made hidden=",await pg.eval_on_selector("#mm-made","e=>e.hidden"),"code=",await pg.eval_on_selector("#mm-made-code","e=>e.textContent"),"vis=",await pg.eval_on_selector("#mm-made-vis","e=>e.textContent"),"note=",await pg.eval_on_selector("#mm-note","e=>e.textContent"))
    rooms=await pg.eval_on_selector("#mm-rooms","e=>e.innerText"); print("rooms:",rooms[:120]); print("errs",errs)
    await ctx.close(); await b.close()
asyncio.run(main())
