"""Preview the embedded UI with real read-only data. Never forward control requests.

Run from the repository root: python test/preview_server.py
Open http://127.0.0.1:8765 in the shared browser.
"""
import json
import re
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE = 'http://10.1.40.117'


def embedded_html():
    source = (ROOT / 'components/api_server/api_server.c').read_text()
    def constant(name):
        match = re.search(r'static const char \*' + name + r' =\n(.*?);\n', source, re.S)
        return ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))
    return constant('HTML3').replace('</body>', constant('GRAPH_PATCH') + '</body>').encode()


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, content, kind='application/json'):
        self.send_response(code)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(content)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(content)

    def do_GET(self):
        if self.path == '/' or self.path.startswith('/?'):
            return self.reply(200, embedded_html(), 'text/html; charset=utf-8')
        allowed = {'status', 'settings', 'moisture', 'moisture/latest', 'diagnostics', 'history'}
        if self.path.startswith('/api/v1/') and self.path[8:].split('?')[0] in allowed:
            try:
                with urllib.request.urlopen(DEVICE + self.path, timeout=10) as response:
                    return self.reply(200, response.read())
            except (urllib.error.URLError, TimeoutError):
                return self.reply(502, b'{"error":"controller unavailable"}')
        self.reply(404, b'{"error":"not found"}')

    def do_POST(self):
        self.reply(405, b'{"error":"Read-only preview: no commands reach the controller"}')

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    print('Read-only UI preview at http://127.0.0.1:8765', flush=True)
    ThreadingHTTPServer(('127.0.0.1', 8765), Handler).serve_forever()
