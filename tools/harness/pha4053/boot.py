page.set_input_files('#rom-file','/tmp/z4053/t/oot.o2r'); page.fill('#f-name','Tester')
for _ in range(180):
    if not page.is_disabled('#btn-solo'): break
    time.sleep(1)
page.click('#btn-solo'); time.sleep(25); shot('/tmp/z4053/t/ev/boot.png'); print('solo booted')
