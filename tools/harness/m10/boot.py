# PHA-3923: boot playd's page in Solo. The #name= hash auto-joins a co-op room, and
# the relay's /anchor is a stub (/tmp/relay/stub.js) that never answers, so SoH ran
# its main loop but drew nothing (black canvas). Solo skips Anchor entirely.
page.set_input_files('#rom-file','/tmp/m9web/serve/test-oot.o2r'); page.fill('#f-name','Tester')
for _ in range(120):
    if not page.is_disabled('#btn-solo'): break
    time.sleep(1)
page.click('#btn-solo'); time.sleep(30); shot('/tmp/m10ev/boot.png'); print('solo booted')
