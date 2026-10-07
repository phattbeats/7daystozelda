# vertical-short segments: 4:3 center crops scaled to 1080x810 (silent)
import json, subprocess, sys
C = '/tmp/z4060/clips/'
plan = json.load(open(sys.argv[1]))
for i, s in enumerate(plan['segs']):
    out = 'seg/%s_%02d.mp4' % (plan['name'], i)
    sp = s.get('speed', 1.0)
    crop = s.get('crop', '960:720:160:0')
    vf = ['crop=' + crop, 'setpts=PTS/%f' % sp, 'scale=1080:810:flags=lanczos', 'unsharp=5:5:0.6']
    if s.get('grade') == 'day': vf.append('eq=contrast=1.06:saturation=1.18:gamma=0.98')
    if s.get('grade') == 'night': vf.append('eq=contrast=1.08:saturation=1.1')
    vf.append('fps=30,format=yuv420p,setsar=1')
    subprocess.check_call(['ffmpeg', '-v', 'error', '-y', '-ss', str(s['ss']), '-t', '%.3f' % (s['dur'] * sp), '-i', C + s['src'] + '.mp4',
                           '-vf', ','.join(vf), '-an', '-c:v', 'libx264', '-crf', '14', '-preset', 'fast', out])
    print(out)
