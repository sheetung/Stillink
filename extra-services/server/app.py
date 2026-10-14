"""Stillink extra services: HTTP card endpoints and UDP music receiver."""
import argparse
import json
import secrets
from service_security import load_key
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

import weather
import album
from music_receiver import Receiver

MUSIC = Receiver()
ALBUM = None
AUTH_KEY = ""

class Handler(BaseHTTPRequestHandler):
    def send(self, status, content, kind, data=None):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        if data:
            self.send_header("X-Weather-UTC-Offset", str(data["utc_offset"]))
            self.send_header("X-Weather-Updated", str(data["updated"]))
        self.end_headers()
        self.wfile.write(content)

    def authorized(self):
        if AUTH_KEY and not secrets.compare_digest(self.headers.get("Authorization", "").encode("utf-8"), ("Bearer " + AUTH_KEY).encode("ascii")):
            self.close_connection = True
            self.send(401, b'{"error":"Service key required"}', "application/json")
            return False
        return True

    def do_GET(self):
        if not self.authorized(): return
        url = urlsplit(self.path)
        if url.path.startswith("/v1/album/"):
            return self.album_request(url)
        if url.path in ("/v1/music.json", "/v1/music.png", "/v1/music.bin"):
            status, body, kind = MUSIC.response(url.path, parse_qs(url.query).get("v", [""])[0])
            return self.send(status, body, kind)
        if url.path == "/health":
            return self.send(200, json.dumps({"status":"ok", "services":{"weather":{"available":True}, "music":MUSIC.health(), "album":{"available":ALBUM is not None}}}).encode(), "application/json")
        weather.handle(self, url)

    def album_request(self, url):
        if ALBUM is None:
            return self.send(503, b'{"error":"Album storage unavailable"}', "application/json")
        return album.handle(self, ALBUM, url)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        if not self.authorized(): return
        self.album_request(urlsplit(self.path))

    def do_DELETE(self):
        if not self.authorized(): return
        self.album_request(urlsplit(self.path))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--font", default=weather.FONT_PATH)
    parser.add_argument("--music-port", type=int, default=8790, help="UDP music broadcast receiver port")
    parser.add_argument("--data-dir", default="data", help="Persistent album storage directory")
    parser.add_argument("--rotate-service-key", action="store_true", help="Generate a new service key without changing photos")
    args = parser.parse_args()
    AUTH_KEY = load_key(args.data_dir, rotate=args.rotate_service_key)
    print(f"Service key: {AUTH_KEY}", flush=True)
    if args.rotate_service_key: raise SystemExit(0)
    ALBUM = album.Store(args.data_dir)
    weather.FONT_PATH = args.font
    MUSIC.font_path = weather.find_font()
    MUSIC.start(port=args.music_port)
    print(f"Stillink services listening on {args.host}:{args.port}", flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
