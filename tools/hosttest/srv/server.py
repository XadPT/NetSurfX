#!/usr/bin/env python3
"""Servidor de testes: HTTP e HTTPS com chunked, gzip, redireccionamentos, cookies e POST."""
import http.server, socketserver, ssl, gzip, sys, threading, urllib.parse, io

PAGE = """<!DOCTYPE html><html><head><meta charset="utf-8"><title>{title}</title>
<style>body{{font-family:sans-serif;background:#fff}} .box{{border:2px solid #36c;padding:6px;margin:6px 0}}</style></head>
<body><h1>{title}</h1><div class="box">{body}</div>
<p><a href="/page2">Pagina 2 (link)</a> | <a href="/redir">Redirect</a> | <a href="/gz">Gzip</a> | <a href="/chunked">Chunked</a></p>
<p><a href="https://localhost:8443/page2">HTTPS bom</a> | <a href="https://localhost:8444/">HTTPS auto-assinado</a></p>
<form action="/post" method="post"><input type="text" name="nome" value="Xbox"> <input type="submit" value="Enviar POST"></form>
<form action="/get" method="get"><input type="text" name="q"> <input type="submit" value="Pesquisar GET"></form>
<p><img src="/img.png" width="64" height="64"> imagem PNG</p>
{long}
</body></html>"""

LONG = "".join("<p>Linha de texto numero %d para testar o scroll da pagina. Acentos: ação, pão, coração.</p>" % i for i in range(60))

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    timeout = 4   # fecha ligacoes paradas (testa o keep-alive com ligacoes mortas)
    def log_message(self, *a): sys.stderr.write("[srv] %s %s porta=%d\n" % (self.command, self.path, self.client_address[1]))
    def send_page(self, title, body, extra_headers=(), mode="plain", status=200):
        data = PAGE.format(title=title, body=body, long=LONG).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        for k, v in extra_headers: self.send_header(k, v)
        if mode == "gzip":
            data = gzip.compress(data)
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers(); self.wfile.write(data)
        elif mode == "chunked":
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 1000):
                c = data[i:i+1000]
                self.wfile.write(b"%x\r\n" % len(c) + c + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif mode == "gzchunk":
            data = gzip.compress(data)
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 700):
                c = data[i:i+700]
                self.wfile.write(b"%x\r\n" % len(c) + c + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(data)))
            self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        ck = self.headers.get("Cookie", "(nenhum)")
        if u.path == "/":
            self.send_page("Teste NetSurfX", "Pagina inicial de teste. Cookie recebido: %s" % ck, [("Set-Cookie", "sessao=abc123; Path=/")])
        elif u.path == "/page2":
            self.send_page("Pagina 2", "Chegaste a pagina 2. Cookie: %s" % ck)
        elif u.path == "/redir":
            self.send_response(302); self.send_header("Location", "/page2?veio=redir"); self.send_header("Content-Length", "0"); self.end_headers()
        elif u.path == "/gz":
            self.send_page("Pagina gzip", "Esta pagina veio comprimida com gzip.", mode="gzip")
        elif u.path == "/chunked":
            self.send_page("Pagina chunked", "Esta pagina veio em blocos (chunked) e gzip.", mode="gzchunk")
        elif u.path == "/stress":
            parts = ["<!DOCTYPE html><html><head><meta charset='utf-8'><title>Stress</title>",
                     "<style>td{border:1px solid #888;padding:2px} .n{margin-left:1px;border-left:1px solid #ccc}</style></head><body>",
                     "<h1>Stress</h1>"]
            parts.append("<div class='n'>" * 400 + "fundo da arvore (400 niveis)" + "</div>" * 400)
            parts.append("<table>" + "".join("<tr>" + "".join("<td>c%d.%d</td>" % (r, c) for c in range(8)) + "</tr>" for r in range(200)) + "</table>")
            parts.append("".join("<img src='/img.png?i=%d' width='32' height='32'>" % i for i in range(120)))
            parts.append("<p>" + ("texto longo " * 20000) + "</p></body></html>")
            data = "".join(parts).encode()
            self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        elif u.path == "/get":
            q = urllib.parse.parse_qs(u.query).get("q", [""])[0]
            self.send_page("Resultado GET", "Pesquisaste: <b>%s</b>" % q)
        elif u.path == "/ua":
            d = ("<html><body><p>UA: " + self.headers.get("User-Agent", "") + "</p><p>AE: " + self.headers.get("Accept-Encoding", "") + "</p></body></html>").encode()
            self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Length", str(len(d))); self.end_headers(); self.wfile.write(d)
        elif u.path == "/dl/redir":
            self.send_response(302); self.send_header("Location", "/dl/ficheiro.zip"); self.send_header("Content-Length", "0"); self.end_headers()
        elif u.path == "/dl/ficheiro.zip":
            import os, time
            if not os.path.exists("dl.bin"):
                open("dl.bin", "wb").write(os.urandom(3 * 1024 * 1024 + 123))
            d = open("dl.bin", "rb").read()
            self.send_response(200); self.send_header("Content-Type", "application/zip")
            self.send_header("Content-Disposition", 'attachment; filename="Homebrew Ação: teste/v1?.zip"')
            self.send_header("Content-Length", str(len(d))); self.end_headers()
            for i in range(0, len(d), 65536):
                self.wfile.write(d[i:i+65536]); time.sleep(0.3 if "slow" in (u.query or "") else 0.02)
        elif u.path == "/dropimg":
            # responde e depois fecha a ligacao SEM avisar (sem Connection: close)
            import time
            png = open("static/t.webp", "rb").read()
            self.send_response(200); self.send_header("Content-Type", "image/webp"); self.send_header("Content-Length", str(len(png))); self.end_headers(); self.wfile.write(png); self.wfile.flush()
            time.sleep(0.4)
            self.close_connection = True
        elif u.path.startswith("/static/"):
            import os, mimetypes
            fn = os.path.join("static", os.path.basename(u.path))
            if not os.path.exists(fn):
                self.send_page("404", "Nao existe", status=404); return
            d = open(fn, "rb").read()
            ct = {".svg": "image/svg+xml", ".html": "text/html; charset=utf-8", ".css": "text/css", ".ttf": "font/ttf", ".webp": "image/webp", ".woff": "font/woff"}.get(os.path.splitext(fn)[1], "application/octet-stream")
            self.send_response(200); self.send_header("Content-Type", ct); self.send_header("Content-Length", str(len(d))); self.send_header("Cache-Control", "max-age=3600"); self.send_header("Last-Modified", "Thu, 01 Oct 2026 10:00:00 GMT"); self.end_headers(); self.wfile.write(d)
        elif u.path == "/img.png":
            png = open("img.png", "rb").read()
            self.send_response(200); self.send_header("Content-Type", "image/png"); self.send_header("Content-Length", str(len(png))); self.end_headers(); self.wfile.write(png)
        else:
            self.send_page("404", "Nao existe", status=404)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n).decode("utf-8", "replace")
        self.send_page("Resultado POST", "Recebido por POST: <b>%s</b>" % body)

class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

def serve(port, ctx=None):
    s = S(("127.0.0.1", port), H)
    if ctx: s.socket = ctx.wrap_socket(s.socket, server_side=True)
    s.serve_forever()

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain("good.crt", "good.key")
ctx2 = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx2.load_cert_chain("self.crt", "self.key")
threading.Thread(target=serve, args=(8443, ctx), daemon=True).start()
threading.Thread(target=serve, args=(8444, ctx2), daemon=True).start()
serve(8080)
