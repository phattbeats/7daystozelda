# render normalized 1920x1080@30 silent segments: python3 segs.py <plan.json>
import json, subprocess, sys, os
C = '/tmp/z4060/clips/'
plan = json.load(open(sys.argv[1]))
W, H = plan.get('w', 1920), plan.get('h', 1080)
for i, s in enumerate(plan['segs']):
    out = 'seg/%s_%02d.mp4' % (plan['name'], i)
    sp = s.get('speed', 1.0); vf = []
    if s.get('crop'): vf.append('crop=%s' % s['crop'])
    vf.append('setpts=PTS/%f' % sp)
    if s.get('vertical'):
        vf = vf  # handled by caller
    vf.append('scale=%d:%d:flags=lanczos' % (W, H))
    vf.append('unsharp=5:5:0.6')
    if s.get('grade') == 'day': vf.append('eq=contrast=1.06:saturation=1.18:gamma=0.98')
    if s.get('grade') == 'night': vf.append('eq=contrast=1.08:saturation=1.1')
    vf.append('fps=30,format=yuv420p,setsar=1')
    cmd = ['ffmpeg', '-v', 'error', '-y', '-ss', str(s['ss']), '-t', '%.3f' % (s['dur'] * sp), '-i', C + s['src'] + '.mp4',
           '-vf', ','.join(vf), '-an', '-c:v', 'libx264', '-crf', '14', '-preset', 'fast', out]
    subprocess.check_call(cmd); print(out, s['src'], s['dur'])
