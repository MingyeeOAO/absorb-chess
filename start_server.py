import http.server
import socketserver
import os
import mimetypes

class Handler(http.server.SimpleHTTPRequestHandler):
    def guess_type(self, path):
        # Force correct MIME for JS and WASM everywhere
        if path.endswith('.js'):
            return 'application/javascript'
        if path.endswith('.wasm'):
            return 'application/wasm'
        return super().guess_type(path)
    
    def end_headers(self):
        # Add cache headers for static assets
        if (self.path.endswith('.png') or self.path.endswith('.jpg') or 
            self.path.endswith('.css') or self.path.endswith('.js')):
            # Cache static assets for 1 hour
            self.send_header('Cache-Control', 'public, max-age=3600')
            self.send_header('ETag', f'"{hash(self.path)}"')
        super().end_headers()

PORT = 8080
os.chdir(os.path.dirname(os.path.abspath(__file__)))
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"Serving at http://localhost:{PORT}")
    httpd.serve_forever()