import subprocess, json
D = json.load(open('trailer.json'))
segs = ['seg/tr_%02d.mp4' % i for i in range(len(D['segs']))]
dur = [s['dur'] for s in D['segs']]
XF = 0.5                      # crossfade temple -> nightfall
g1 = sum(dur[:5]); L = sum(dur) - XF
T = L - 3.9                   # title card start
inp = []
for s in segs: inp += ['-i', s]
inp += ['-loop', '1', '-t', '%.2f' % L, '-i', 'logo.png', '-loop', '1', '-t', '%.2f' % L, '-i', 'tag16x9.png', '-loop', '1', '-t', '%.2f' % L, '-i', 'url16x9.png']
inp += ['-ss', '3.0', '-i', '../clips/build2.mp4', '-ss', '2.0', '-i', '../clips/r1_nightfall.mp4',
        '-ss', '1.0', '-i', '../clips/r5_chair_gate.mp4', '-ss', '13.1', '-i', '../t/serve/title-theme.mp3']
n = len(segs); LOGO = n; TAG = n + 1; URL = n + 2; AA, AB, AC, AD = n + 3, n + 4, n + 5, n + 6
fc = []
fc.append(''.join('[%d:v]' % i for i in range(5)) + 'concat=n=5:v=1:a=0[g1]')
fc.append(''.join('[%d:v]' % i for i in range(5, n)) + 'concat=n=%d:v=1:a=0[g2]' % (n - 5))
fc.append('[g1][g2]xfade=transition=fade:duration=%.2f:offset=%.3f[v0]' % (XF, g1 - XF))
fc.append('[v0]fade=in:st=0:d=0.5,drawbox=c=black@0.5:t=fill:enable=\'gte(t,%.2f)\'[v1]' % (T + 0.4))
fc.append('[%d:v]scale=1150:-1,format=rgba,fade=in:st=%.2f:d=0.7:alpha=1[logo]' % (LOGO, T))
fc.append('[v1][logo]overlay=x=(W-w)/2:y=170:shortest=1[v2]')
fc.append('[%d:v]format=rgba,fade=in:st=%.2f:d=0.6:alpha=1[tag]' % (TAG, T + 0.8))
fc.append('[%d:v]format=rgba,fade=in:st=%.2f:d=0.6:alpha=1[url]' % (URL, T + 1.4))
fc.append('[v2][tag]overlay=0:0:shortest=1[v3];[v3][url]overlay=0:0:shortest=1,fade=out:st=%.2f:d=0.6[vout]' % (L - 0.6))
a_len = [g1 - XF / 2, dur[5], sum(dur[6:11]), dur[11]]
fc.append('[%d:a]atrim=0:%.3f,afade=in:d=0.4,volume=1.0[a0]' % (AA, a_len[0] + 0.5))
fc.append('[%d:a]atrim=0:%.3f,volume=1.15[a1]' % (AB, a_len[1] + 0.5))
fc.append('[%d:a]atrim=0:%.3f,volume=1.05[a2]' % (AC, a_len[2] + 0.5))
fc.append('[%d:a]atrim=0:%.3f,volume=2.4,afade=in:d=0.8[a3]' % (AD, a_len[3] + 1.0))
fc.append('[a0][a1]acrossfade=d=0.5[x1];[x1][a2]acrossfade=d=0.4[x2];[x2][a3]acrossfade=d=0.8[x3]')
fc.append('[x3]atrim=0:%.3f,afade=out:st=%.3f:d=0.8,loudnorm=I=-14:TP=-1.5:LRA=11[aout]' % (L, L - 0.8))
cmd = ['ffmpeg', '-v', 'error', '-y'] + inp + ['-filter_complex', ';'.join(fc), '-map', '[vout]', '-map', '[aout]',
       '-c:v', 'libx264', '-crf', '17', '-preset', 'slow', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
       '-c:a', 'aac', '-b:a', '192k', '-ar', '48000', '-t', '%.3f' % L, 'out/7dtz_trailer_16x9.mp4']
import os; os.makedirs('out', exist_ok=True)
subprocess.check_call(cmd); print('L=%.2f title at %.2f' % (L, T))
