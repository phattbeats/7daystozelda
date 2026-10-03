import http.server, socketserver, sys, os
os.chdir(sys.argv[2]); 
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy','same-origin'); self.send_header('Cross-Origin-Embedder-Policy','require-corp'); self.send_header('Cache-Control','no-store'); super().end_headers()
H.extensions_map['.wasm']='application/wasm'
socketserver.ThreadingTCPServer.allow_reuse_address=True
socketserver.ThreadingTCPServer(('0.0.0.0',int(sys.argv[1])),H).serve_forever()
