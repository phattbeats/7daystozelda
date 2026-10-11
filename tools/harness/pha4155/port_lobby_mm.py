import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
d = '/tmp/z4155/src/'
html, css, js = open(d+'mm.html').read().strip(), open(d+'mm.css').read().strip(), open(d+'mm.js').read()
def rep(a, b):
    global s
    assert s.count(a) == 1, (a, s.count(a))
    s = s.replace(a, b)
assert 'id=mm ' not in s
rep('.guide-btn{display:inline-block;min-height:44px;line-height:44px;padding:0 1.1rem;text-decoration:none;margin-bottom:.35rem}</style>', '.guide-btn{display:inline-block;min-height:44px;line-height:44px;padding:0 1.1rem;text-decoration:none;margin-bottom:.35rem}' + css + '</style>')
rep('placeholder=boys></label></div>', 'placeholder=boys></label></div>' + html)
rep('</body>', '<script>' + js + '</script></body>')
open(dst, 'w').write(s)
