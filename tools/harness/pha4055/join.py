for _ in range(120):
    if not page.is_disabled('#btn-join'): break
    time.sleep(1)
page.click('#btn-join'); time.sleep(25); page.reload(); time.sleep(35); page.mouse.click(480,270); time.sleep(1)
exec(open('/tmp/z4055/t/load.py').read())
