exec(open('/tmp/z4047/t/drv.py').read())
both("kit(); time.sleep(0.5)\n")
both("raid('warp:40B'); time.sleep(12)\nfor i in range(4):\n    key('x'); time.sleep(0.4)\n")
a=kd('A'); b=kd('B')
print('A', {k:a.get(k) for k in ('scene','own','present','clear','act','phase')}, 'B', {k:b.get(k) for k in ('scene','own','present','clear','act','phase')})
if a.get('clear'):
    kd('A',5); print('unset clear; re-warp'); both("raid('warp:40B'); time.sleep(12)\nfor i in range(4):\n    key('x'); time.sleep(0.4)\n")
    a=kd('A'); b=kd('B'); print('A present', a.get('present'), a.get('clear'), 'B present', b.get('present'), b.get('clear'))
