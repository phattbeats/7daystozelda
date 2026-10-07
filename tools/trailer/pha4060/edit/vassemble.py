import json, subprocess, sys
plan = json.load(open(sys.argv[1])); out = sys.argv[2]
segs = ['seg/%s_%02d.mp4' % (plan['name'], i) for i in range(len(plan['segs']))]
dur = [s['dur'] for s in plan['segs']]; L = min(sum(dur), float(plan.get('max', 15.0)))
T = L - 3.2
aud = plan['audio'] if 'audio' in plan else [['../clips/r5_chair_gate.mp4', 1.0, sum(dur[:4]), 1.0], ['../clips/build2.mp4', 3.0, sum(dur[4:6]), 1.0], ['../t/serve/title-theme.mp3', 16.0, dur[6], 2.4]]
inp = []
for s in segs: inp += ['-i', s]
n = len(segs)
inp += ['-loop', '1', '-t', '%.2f' % L, '-i', 'logo.png', '-loop', '1', '-t', '%.2f' % L, '-i', plan.get('tag', 'v_tag.png'), '-loop', '1', '-t', '%.2f' % L, '-i', 'v_url.png']
LOGO, TAG, URL = n, n + 1, n + 2
for f, ss, d, v in aud: inp += ['-ss', str(ss), '-i', f]
fc = [''.join('[%d:v]' % i for i in range(n)) + 'concat=n=%d:v=1:a=0,trim=0:%.3f,split[fg][fgb]' % (n, L)]
fc.append('[fgb]scale=2560:1920,crop=1080:1920,boxblur=28:2,eq=brightness=-0.18:saturation=0.8[bg]')
fc.append('[bg][fg]overlay=0:555,fade=in:st=0:d=0.3,drawbox=c=black@0.55:t=fill:enable=\'gte(t,%.2f)\'[v1]' % (T + 0.3))
fc.append('[%d:v]format=rgba,fade=in:st=0.2:d=0.4:alpha=1[tag]' % TAG)
fc.append('[%d:v]scale=980:-1,format=rgba,fade=in:st=%.2f:d=0.6:alpha=1[logo]' % (LOGO, T))
fc.append('[%d:v]format=rgba,fade=in:st=%.2f:d=0.5:alpha=1[url]' % (URL, T + 0.8))
fc.append('[v1][tag]overlay=0:0:shortest=1[v2];[v2][logo]overlay=(W-w)/2:820:shortest=1[v3];[v3][url]overlay=0:0:shortest=1,fade=out:st=%.2f:d=0.4[vout]' % (L - 0.4))
labels = []
for k, (f, ss, d, v) in enumerate(aud):
    fc.append('[%d:a]atrim=0:%.3f,volume=%.2f%s[a%d]' % (n + 3 + k, d + 0.5, v, ',afade=in:d=0.5' if k == len(aud) - 1 else '', k)); labels.append('a%d' % k)
cur = labels[0]
for k in range(1, len(labels)):
    fc.append('[%s][%s]acrossfade=d=0.4[x%d]' % (cur, labels[k], k)); cur = 'x%d' % k
fc.append('[%s]atrim=0:%.3f,afade=out:st=%.3f:d=0.5,loudnorm=I=-14:TP=-1.5:LRA=11[aout]' % (cur, L, L - 0.5))
subprocess.check_call(['ffmpeg', '-v', 'error', '-y'] + inp + ['-filter_complex', ';'.join(fc), '-map', '[vout]', '-map', '[aout]',
    '-c:v', 'libx264', '-crf', '18', '-preset', 'slow', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', '-c:a', 'aac', '-b:a', '192k',
    '-ar', '48000', '-t', '%.3f' % L, out])
print(out, 'L=%.2f' % L)
