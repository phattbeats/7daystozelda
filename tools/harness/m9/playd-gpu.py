# eval daemon: POST python code to :PORT, runs with helpers; returns stdout
import os, sys, io, json, time, traceback, http.server, socketserver, contextlib
from playwright.sync_api import sync_playwright
PORT=int(sys.argv[1]); WS=sys.argv[2]; URL=sys.argv[3]
pw=sync_playwright().start()
EXE=os.environ.get('CHROME_EXE') or os.path.expanduser('~/.cache/ms-playwright/chromium_headless_shell-1243/chrome-headless-shell-linux64/chrome-headless-shell')
ctx=pw.chromium.launch_persistent_context(sys.argv[4],executable_path=EXE,args=['--use-gl=angle','--use-angle=gl-egl','--enable-gpu','--ignore-gpu-blocklist','--autoplay-policy=no-user-gesture-required','--enable-features=SharedArrayBuffer'],viewport={'width':960,'height':540}) if WS=='local' else pw.chromium.connect_over_cdp(WS).contexts[0]
page=ctx.new_page(); page.set_viewport_size({'width':960,'height':540})
page.on('console', lambda m: print('[con]',m.text[:200],file=sys.stderr) if 'SevenDays' in m.text or 'error' in m.type else None)
page.goto(URL)
def ev(js): return page.evaluate(js)
def _k(k): return ' ' if k=='space' else k
def key(k,t=0.12): k=_k(k); page.keyboard.down(k); time.sleep(t); page.keyboard.up(k); time.sleep(0.1)
def hold(k,t): k=_k(k); page.keyboard.down(k); time.sleep(t); page.keyboard.up(k)
def shot(p): page.screenshot(path=p)
def cv(n,v): ev('Module.ccall("sevendays_test_cvar",null,["string","number"],["%s",%d])'%(n,v))
def rs(): return ev('Module.ccall("sevendays_test_raid_state","string",[],[])')
def raid(c): ev('Module.ccall("sevendays_test_raid",null,["string"],["%s"])'%c)
G=dict(page=page,ev=ev,key=key,hold=hold,shot=shot,time=time,cv=cv,rs=rs,raid=raid,_k=_k)
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        code=self.rfile.read(int(self.headers['Content-Length'])).decode(); out=io.StringIO()
        with contextlib.redirect_stdout(out):
            try: exec(code,G)
            except Exception: traceback.print_exc(file=out)
        b=out.getvalue().encode(); self.send_response(200); self.end_headers(); self.wfile.write(b)
    def log_message(self,*a): pass
socketserver.TCPServer.allow_reuse_address=True
socketserver.TCPServer(('127.0.0.1',PORT),H).serve_forever()
