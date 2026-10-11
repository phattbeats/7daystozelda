time.sleep(25); page.reload(); time.sleep(35); page.mouse.click(480,270); time.sleep(1)
exec(open('/tmp/z4048/t/load.py').read())
ev('Module.ccall("sevendays_test_save",null,[],[])'); time.sleep(3)
print(ev('location.hash'))
