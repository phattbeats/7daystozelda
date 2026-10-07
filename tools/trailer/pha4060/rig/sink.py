# receives MediaRecorder chunks from the capture page: POST /<name>.webm appends
import http.server, socketserver, os
D='/tmp/z4060/rec'; os.makedirs(D, exist_ok=True)
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        b=self.rfile.read(int(self.headers['Content-Length']))
        with open(os.path.join(D, os.path.basename(self.path)), 'ab') as f: f.write(b)
        self.send_response(204); self.send_header('Access-Control-Allow-Origin','*'); self.end_headers()
    def do_OPTIONS(self):
        self.send_response(204); self.send_header('Access-Control-Allow-Origin','*'); self.send_header('Access-Control-Allow-Headers','*'); self.end_headers()
    def log_message(self,*a): pass
socketserver.ThreadingTCPServer.allow_reuse_address=True
socketserver.ThreadingTCPServer(('127.0.0.1',19460),H).serve_forever()
