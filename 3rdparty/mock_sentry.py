#!/usr/bin/env python3
"""mock_sentry.py - 极简 Sentry envelope 接收服务(仅用于本地验证上传链路)

模拟 Sentry 服务端的 /api/<project>/envelope/ 接口:
  - 接收 sentry-native 发来的 envelope POST
  - 按时间戳存到 ./received/ 目录,便于检查 minidump 是否随上报发出

用法:
  python3 mock_sentry.py [port]        # 默认 9000
  DSN 示例: http://testkey@127.0.0.1:9000/1

注意:这只是验证上传链路通断的 mock,生产请用官方 self-hosted Sentry,
见 docker/sentry/README.md。
"""
import http.server
import os
import sys
import time
from urllib.parse import urlparse

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
OUTDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "received")
os.makedirs(OUTDIR, exist_ok=True)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        parsed = urlparse(self.path)
        # 处理 Expect: 100-continue (crashpad 的 libcurl 上传会用)
        if self.headers.get("Expect", "").lower() == "100-continue":
            self.send_response_only(100)
            self.end_headers()
        length = int(self.headers.get("Content-Length", 0))
        ctype = self.headers.get("Content-Type", "")
        te = self.headers.get("Transfer-Encoding", "")
        print(f"[mock_sentry] headers: Expect={self.headers.get('Expect')} "
              f"Content-Length={self.headers.get('Content-Length')} "
              f"Transfer-Encoding={te} Content-Type={ctype[:60]}", flush=True)
        body = b""
        if te.lower() == "chunked":
            # 手动解 chunked
            while True:
                line = self.rfile.readline().strip()
                if not line:
                    break
                size = int(line.split(b";")[0], 16)
                if size == 0:
                    self.rfile.readline()
                    break
                body += self.rfile.read(size)
                self.rfile.readline()
        elif length:
            body = self.rfile.read(length)
        ts = time.strftime("%Y%m%d_%H%M%S")
        # 从路径提取 project id
        proj = parsed.path.strip("/").split("/")[-2] if "envelope" in parsed.path else "x"
        fname = os.path.join(OUTDIR, f"envelope_{proj}_{ts}_{len(body)}b.bin")
        with open(fname, "wb") as f:
            f.write(b"### PATH: " + self.path.encode() + b"\n")
            auth = self.headers.get("X-Sentry-Auth", "")
            f.write(b"### AUTH: " + auth.encode() + b"\n\n")
            f.write(body)
        # 简单判断 envelope 里是否带了 minidump 附件(可能 gzip 压缩)
        has_dump = b"upload_file_minidump" in body
        if not has_dump and body[:2] == b"\x1f\x8b":
            try:
                import gzip as _gzip
                has_dump = b"upload_file_minidump" in _gzip.decompress(body)
            except Exception:
                pass
        print(f"[mock_sentry] {self.path} {len(body)} bytes -> {fname} "
              f"{'HAS_MINIDUMP' if has_dump else 'no dump'}", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"id":"mock-id"}')


if __name__ == "__main__":
    srv = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    print(f"[mock_sentry] listening on 127.0.0.1:{PORT}, saving to {OUTDIR}", flush=True)
    srv.serve_forever()
