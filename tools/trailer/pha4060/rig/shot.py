# shot.py '<python dict literal for SHOT>' -> runs act.py on A B C D concurrently
import sys, subprocess, tempfile, os
shot = sys.argv[1]; files = []
for c in 'ABCD':
    f = '/tmp/z4060/t/_shot_%s.py' % c
    open(f, 'w').write('SHOT = %s\n' % shot + open('/tmp/z4060/t/act.py').read()); files.append('%s:%s' % (c, f))
print(subprocess.run(['python3', '/tmp/z4060/t/drv.py'] + files, capture_output=True, text=True).stdout)
