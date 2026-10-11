# PHA-4155 follow-up: swap the quick-match lobby block in web/public/index.html for the lobby-codes one.
# usage: port_lobby_codes.py OLD_LOBBY_SRC_DIR INDEX_IN INDEX_OUT   (new block = ./lobby-src)
import sys, os
old, src, dst = sys.argv[1] + '/', sys.argv[2], sys.argv[3]
new = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lobby-src') + '/'
s = open(src).read()
for f in ('mm.html', 'mm.css', 'mm.js'):
    o, n = open(old + f).read(), open(new + f).read()
    if f != 'mm.js': o, n = o.strip(), n.strip()
    assert s.count(o) == 1, (f, s.count(o))
    s = s.replace(o, n)
open(dst, 'w').write(s)
