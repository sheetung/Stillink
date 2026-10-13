"""Persistent photo frames and device-independent slideshow selections."""
import binascii
import io
import hashlib
import json
import random
import sqlite3
import struct
import time
import uuid
from contextlib import contextmanager
from pathlib import Path

from PIL import Image
from music_card import PALETTE


def preview(frame):
    if len(frame) < 16:
        raise ValueError("图片帧不完整")
    magic, version, panel, width, height, length, crc, reserved = struct.unpack(">2sBBHHIHH", frame[:16])
    if (magic != b"\xa5\x5a" or (version, width, height) not in ((1, 768, 552), (2, 800, 600))
            or length != width * height // 4 or len(frame) != length + 16
            or binascii.crc_hqx(frame[16:], 0xffff) != crc):
        raise ValueError("图片帧尺寸或校验错误，请从拍立得保存")
    pixels = bytes(value >> shift & 3 for value in frame[16:] for shift in (6, 4, 2, 0))
    image = Image.frombytes("P", (width, height), pixels).transpose(Image.Transpose.ROTATE_180)
    image.putpalette(PALETTE)
    out = io.BytesIO()
    image.save(out, "PNG")
    return width, height, out.getvalue()


class Store:
    def __init__(self, directory):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        self.path = directory / "album.sqlite3"
        with self.connect() as db:
            db.executescript("""
                CREATE TABLE IF NOT EXISTS photos (
                    id TEXT PRIMARY KEY, name TEXT NOT NULL, created INTEGER NOT NULL,
                    width INTEGER NOT NULL, height INTEGER NOT NULL, frame BLOB NOT NULL, png BLOB NOT NULL);
                CREATE TABLE IF NOT EXISTS playlists (id TEXT PRIMARY KEY, photos TEXT NOT NULL);
            """)

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def add(self, frame, name=None):
        width, height, png = preview(frame)
        created = int(time.time())
        name = time.strftime("%Y%m%d-%H%M%S", time.localtime(created)) + "-" + hashlib.sha256(frame).hexdigest()[:8]
        photo = dict(id=uuid.uuid4().hex, name=name, created=created, width=width, height=height)
        with self.connect() as db:
            db.execute("INSERT INTO photos VALUES (?,?,?,?,?,?,?)", (*photo.values(), frame, png))
        return photo

    def photos(self):
        with self.connect() as db:
            return [dict(row) for row in db.execute("SELECT id,name,created,width,height FROM photos ORDER BY rowid")]

    def image(self, photo_id, kind):
        with self.connect() as db:
            row = db.execute("SELECT frame,png FROM photos WHERE id=?", (photo_id,)).fetchone()
        if row is None:
            raise KeyError("照片不存在")
        return row["png" if kind == "png" else "frame"]

    def delete(self, photo_id):
        with self.connect() as db:
            if not db.execute("DELETE FROM photos WHERE id=?", (photo_id,)).rowcount:
                raise KeyError("照片不存在")

    def playlist(self, photos):
        if not isinstance(photos, list) or not 1 <= len(photos) <= 256 or any(not isinstance(x, str) for x in photos):
            raise ValueError("请选择 1–256 张照片")
        photos = list(dict.fromkeys(photos))
        available = {p["id"] for p in self.photos()}
        if any(p not in available for p in photos):
            raise ValueError("所选照片已被删除，请重新选择")
        # Reuse identical selections; devices keep their own cursor and timing.
        body = json.dumps(photos)
        with self.connect() as db:
            row = db.execute("SELECT id FROM playlists WHERE photos=?", (body,)).fetchone()
            if row:
                return row["id"]
            playlist_id = uuid.uuid4().hex
            db.execute("INSERT INTO playlists VALUES (?,?)", (playlist_id, body))
        return playlist_id

    def next(self, playlist_id, current, mode, width, height):
        if mode not in ("sequence", "random"):
            raise ValueError("不支持的轮播模式")
        with self.connect() as db:
            row = db.execute("SELECT photos FROM playlists WHERE id=?", (playlist_id,)).fetchone()
        if not row:
            raise KeyError("轮播列表不存在，请重新保存相簿设置")
        available = {p["id"]: p for p in self.photos() if p["width"] == width and p["height"] == height}
        photos = [p for p in json.loads(row["photos"]) if p in available]
        if not photos:
            raise KeyError("没有适合当前屏幕尺寸的照片")
        if mode == "random":
            photo_id = random.choice([p for p in photos if p != current] or photos)
        else:
            photo_id = photos[(photos.index(current) + 1) % len(photos)] if current in photos else photos[0]
        return {**available[photo_id], "count": len(photos)}

    def selection(self, playlist_id):
        with self.connect() as db:
            row = db.execute("SELECT photos FROM playlists WHERE id=?", (playlist_id,)).fetchone()
        if not row:
            raise KeyError("轮播列表不存在")
        return json.loads(row["photos"])


def handle(handler, store, url):
    from urllib.parse import parse_qs
    query = parse_qs(url.query)
    value = lambda key, default="": query.get(key, [default])[0]
    try:
        if handler.command == "GET" and url.path == "/v1/album/photos":
            result = {"photos": store.photos()}
        elif handler.command == "POST" and url.path in ("/v1/album/photos", "/v1/album/playlists"):
            size = int(handler.headers.get("Content-Length", "0"))
            if not 0 < size <= (120016 if url.path.endswith("photos") else 16384):
                raise ValueError("请求大小不正确")
            body = handler.rfile.read(size)
            if len(body) != size:
                raise ValueError("请求不完整")
            result = store.add(body, value("name", "照片")) if url.path.endswith("photos") else {"id": store.playlist(json.loads(body).get("photos"))}
        elif handler.command == "GET" and url.path == "/v1/album/next.json":
            result = store.next(value("playlist"), value("current"), value("mode", "sequence"), int(value("width", "768")), int(value("height", "552")))
        elif handler.command == "GET" and url.path.startswith("/v1/album/playlists/"):
            result = {"photos": store.selection(url.path.rsplit("/", 1)[-1])}
        elif url.path.startswith("/v1/album/photos/"):
            photo_id, dot, kind = url.path.rsplit("/", 1)[-1].partition(".")
            if handler.command == "DELETE" and not dot:
                store.delete(photo_id)
                result = {"deleted": True}
            elif handler.command == "GET" and kind in ("bin", "png"):
                return handler.send(200, store.image(photo_id, kind), "image/png" if kind == "png" else "application/octet-stream")
            else:
                raise KeyError("接口不存在")
        else:
            raise KeyError("接口不存在")
        handler.send(200, json.dumps(result, ensure_ascii=False).encode(), "application/json")
    except (ValueError, TypeError, AttributeError) as error:
        handler.send(400, json.dumps({"error": str(error)}, ensure_ascii=False).encode(), "application/json")
    except KeyError as error:
        handler.send(404, json.dumps({"error": str(error.args[0])}, ensure_ascii=False).encode(), "application/json")
