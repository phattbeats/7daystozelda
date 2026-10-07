for i in range(4):
    W(335, 104, 2530, 0); time.sleep(0.8); key('x'); time.sleep(1.5)
    l = B()['link']; print('try', i, l)
    if l[1] > 115: break
