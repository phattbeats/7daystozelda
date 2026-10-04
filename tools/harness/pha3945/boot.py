page.set_input_files('#rom-file','/tmp/mm/oot.o2r'); page.fill('#f-name','Tester')
for _ in range(180):
    if not page.is_disabled('#btn-solo'): break
    time.sleep(1)
page.click('#btn-solo'); time.sleep(25); shot('/tmp/vq/ev/boot.png'); print('solo booted')
