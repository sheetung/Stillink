"""Receive Windows UDP metadata/artwork and expose server-rendered music cards."""
import base64
import binascii
import hashlib
import io
import json
import socket
import struct
import threading
import time

from music_card import Track, pack_frame, render_card

HEADER = struct.Struct(">4s32sHHI")
CHUNK = 1200
MAX_MESSAGE = 6 * 1024 * 1024


class Receiver:
    def __init__(self):
        self.lock = threading.Lock()
        self.pending = {}
        self.last_digest = None
        self.received = 0
        self.card_key = None
        self.data = dict(playing=False, version="", title="", artist="", album="")
        self.png = b""; self.frame = b""

    def feed(self, packet, source):
        if len(packet) < HEADER.size: return
        magic, digest, part, count, length = HEADER.unpack(packet[:HEADER.size])
        if magic != b"SKM1" or not 0 < length <= MAX_MESSAGE or count != (length+CHUNK-1)//CHUNK or part >= count: return
        chunk = packet[HEADER.size:]
        if len(chunk) != min(CHUNK, length-part*CHUNK): return
        now = time.monotonic()
        # Prune incomplete bursts; retransmissions of a completed announcement are cheap.
        self.pending = {k:v for k,v in self.pending.items() if now-v[0] < 10}
        key = source, digest
        if self.last_digest == key:
            with self.lock: self.received = now
            return
        if key not in self.pending:
            if len(self.pending) >= 8: self.pending.pop(next(iter(self.pending)))
            self.pending[key] = now, count, length, {}
        _, expected_count, expected_length, pieces = self.pending[key]
        if (count,length) != (expected_count,expected_length): return
        pieces[part] = chunk
        if len(pieces) != count: return
        body = b"".join(pieces[i] for i in range(count)); del self.pending[key]
        if hashlib.sha256(body).digest() != digest: return
        try:
            message = json.loads(body)
            if not isinstance(message.get("playing"),bool): return
            if any(not isinstance(message.get(k),str) or len(message[k])>1024 for k in ("title","artist","album","player")): return
            cover = base64.b64decode(message.get("cover",""),validate=True)
            if len(cover)>4*1024*1024: return
            card_key = message["title"], message["artist"], message["album"], hashlib.sha256(cover).digest()
            if message["title"] and card_key != self.card_key:
                image = render_card(Track(message["title"],message["artist"],message["album"],message["player"],cover))
                frame = pack_frame(image)
                out = io.BytesIO(); image.save(out,"PNG")
                with self.lock:
                    self.png, self.frame = out.getvalue(), frame
                    self.data.update(version=hashlib.sha256(frame).hexdigest(),title=message["title"],artist=message["artist"],album=message["album"])
                    self.card_key = card_key
            with self.lock:
                self.data["playing"] = message["playing"] and bool(message["title"])
                self.received = now
                self.last_digest = key
        except (ValueError, TypeError, KeyError, binascii.Error):
            return

    def response(self, path, expected=""):
        with self.lock:
            if path == "/v1/music.json":
                data = {**self.data,"playing":self.data["playing"] and time.monotonic()-self.received<35}
                return 200,json.dumps(data,ensure_ascii=False).encode(),"application/json"
            if expected and expected != self.data["version"]:
                return 409,b'{"error":"Track changed"}',"application/json"
            body = self.png if path.endswith("png") else self.frame
            return (200 if body else 204),body,("image/png" if path.endswith("png") else "application/octet-stream")

    def start(self, host="0.0.0.0", port=8790):
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock.bind((host,port))
        def listen():
            while True:
                packet, address = sock.recvfrom(2048)
                try: self.feed(packet,address[0])
                except Exception as exc: print(f"music broadcast error: {exc}",flush=True)
        threading.Thread(target=listen,daemon=True).start()
        return sock
