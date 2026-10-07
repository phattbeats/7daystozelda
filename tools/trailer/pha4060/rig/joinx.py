import json
ev('''(()=>{localStorage.setItem("soh.name",%s);localStorage.setItem("soh.room","trailer");localStorage.setItem("soh.tunic",%s);localStorage.setItem("soh.color",%s);localStorage.setItem("soh.fairy","FFFFFF-"+%s);Object.keys(localStorage).filter(k=>k.startsWith("soh.roster")).forEach(k=>localStorage.removeItem(k));})()'''%(json.dumps(NAME),json.dumps(TUNIC),json.dumps(TUNIC),json.dumps(TUNIC)))
page.reload(); time.sleep(5)
for i in range(120):
    if not page.is_disabled('#btn-join'): break
    time.sleep(1)
page.fill('#f-name',NAME); page.fill('#f-room','trailer')
page.click('#btn-join'); time.sleep(25); page.reload(); time.sleep(35); page.mouse.click(480,270); time.sleep(1)
print(NAME, page.url)
