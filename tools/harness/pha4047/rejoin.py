page.goto('http://127.0.0.1:18422/index.html?r=%d'%int(time.time())); time.sleep(5)
exec(open('/tmp/z4047/t/join.py').read())
print(ev("[...document.scripts].map(s=>s.src).join(' ')"))
