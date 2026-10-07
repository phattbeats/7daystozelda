# assemble a recording: frames (variable timing) -> 30 fps CFR, audio aligned by wall clock
import sys, os, subprocess
name = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else '/tmp/z4060/clips/%s.mp4' % name
d = '/tmp/z4060/rec/' + name; os.makedirs(os.path.dirname(out), exist_ok=True)
ts = [l.split() for l in open(d + '/ts.txt') if l.strip()]
ts = [(int(n), float(t)) for n, t in ts]
with open(d + '/concat.txt', 'w') as f:
    for i, (n, t) in enumerate(ts):
        dur = (ts[i + 1][1] - t) if i + 1 < len(ts) else 1 / 30
        f.write("file 'f%06d.jpg'\nduration %.4f\n" % (n, dur))
    f.write("file 'f%06d.jpg'\n" % ts[-1][0])
a = '/tmp/z4060/rec/%s.audio.webm' % name; a0 = float(open(d + '/audio_t0.txt').read() or 0)
cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', d + '/concat.txt']
if os.path.exists(a) and a0 > 0:
    off = a0 - ts[0][1]  # >0: audio started after the first frame
    cmd += (['-itsoffset', '%.3f' % off] if off >= 0 else ['-ss', '%.3f' % -off]) + ['-i', a, '-map', '0:v', '-map', '1:a', '-c:a', 'aac', '-b:a', '192k']
cmd += ['-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '16', '-preset', 'medium', '-shortest', out]
subprocess.check_call(cmd); print(out, len(ts), 'frames', '%.2fs' % (ts[-1][1] - ts[0][1]))
