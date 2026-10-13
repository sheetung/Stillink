"""Stillink extra services: HTTP card endpoints and UDP music receiver."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

import weather
from music_receiver import Receiver

MUSIC = Receiver()

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

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ("/v1/music.json", "/v1/music.png", "/v1/music.bin"):
            status, body, kind = MUSIC.response(url.path, parse_qs(url.query).get("v", [""])[0])
            return self.send(status, body, kind)
        if url.path == "/health":
            return self.send(200, json.dumps({"status":"ok", "services":{"weather":{"available":True}, "music":{"available":MUSIC.listening}}}).encode(), "application/json")
        weather.handle(self, url)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--font", default=weather.FONT_PATH)
    parser.add_argument("--music-port", type=int, default=8790, help="UDP music broadcast receiver port")
    args = parser.parse_args()
    weather.FONT_PATH = args.font
    MUSIC.font_path = weather.find_font()
    MUSIC.start(port=args.music_port)
    print(f"Stillink services listening on {args.host}:{args.port}", flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()
