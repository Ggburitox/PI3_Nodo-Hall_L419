#!/usr/bin/env python3
"""Panel del nodo 6. Python 3, sin dependencias externas."""
import argparse, json, time, urllib.request, urllib.error, webbrowser, threading
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs, urlencode

parser = argparse.ArgumentParser()
parser.add_argument('--gateway', default='http://192.168.4.1', help='URL base del gateway')
parser.add_argument('--demo', action='store_true', help='Usar datos simulados, no hardware')
parser.add_argument('--port', type=int, default=8765)
args = parser.parse_args()
base = args.gateway.rstrip('/')
start = time.monotonic()
epoch = int(time.time()*1000)
page = Path(__file__).with_name('panel.html')


def sample(i):
    phase = i % 12
    opened = phase >= 7
    return dict(id=i+1, nodo=6, tipo='magnetico', ts=epoch+i*2000,
                puerta_abierta=int(opened), aperturas=int(phase==7),
                seg_abierta=2.0 if i>0 and (i-1)%12>=7 else 0.0,
                campo_raw=2048 if opened else 2290, anomalias='NINGUNA')


class Handler(BaseHTTPRequestHandler):
    def send_data(self, body, mime='application/json; charset=utf-8', status=200):
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlsplit(self.path)
        if u.path == '/':
            return self.send_data(page.read_bytes(), 'text/html; charset=utf-8')
        if u.path == '/config':
            return self.send_data(json.dumps({'demo':args.demo,'gateway':base}).encode())
        if u.path not in ('/api/ultimos','/api/datos','/api/hora'):
            return self.send_data(b'{}', status=404)
        try:
            if args.demo:
                i=int((time.monotonic()-start)//2)
                if u.path == '/api/ultimos':
                    d=sample(i)
                    return self.send_data(json.dumps([dict(nodo=6,tipo='magnetico',hace_s=0,anomalias=d['anomalias'],msg=d)]).encode())
                if u.path == '/api/datos':
                    cursor=int(parse_qs(u.query).get('desde',['0'])[0])
                    data='\n'.join(json.dumps(sample(j)) for j in range(max(0,cursor,i-500),i+1))
                    return self.send_data(data.encode(), 'application/x-ndjson; charset=utf-8')
                return self.send_data(b'{"ok":true}')
            target=base+u.path+('?' + u.query if u.query else '')
            with urllib.request.urlopen(target, timeout=4) as upstream:
                return self.send_data(upstream.read(), upstream.headers.get('Content-Type','application/json'), upstream.status)
        except Exception as exc:
            return self.send_data(json.dumps({'error':str(exc)}).encode(), status=502)


if __name__ == '__main__':
    url=f'http://127.0.0.1:{args.port}'
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    print(f'Panel: {url} | '+('SIMULACION' if args.demo else f'Gateway: {base}'),flush=True)
    threading.Timer(0.5,lambda:webbrowser.open(url)).start()
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()
