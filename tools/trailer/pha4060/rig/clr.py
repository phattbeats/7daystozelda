page.mouse.click(640,360); time.sleep(0.3); zero=0
for i in range(60):
    if B().get('msg'): key('x'); time.sleep(0.6); zero=0
    else:
        zero+=1; time.sleep(0.4)
        if zero>=6: break
print(CLIENT, 'msg', B().get('msg'))
