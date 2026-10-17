"""Shared, persistent to-do list and printable Stillink cards."""
import io
import json
import math
from datetime import date, datetime
import weather
import sqlite3
import time
import uuid
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import parse_qs

from PIL import Image, ImageDraw
from music_card import PALETTE, font, pack_frame, wrap_text

PAGE_SIZE = 8


class Store:
    def __init__(self, directory):
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        self.path = directory / "todo.sqlite3"
        with self.connect() as db:
            db.execute("CREATE TABLE IF NOT EXISTS items (id TEXT PRIMARY KEY, text TEXT NOT NULL, done INTEGER NOT NULL, created INTEGER NOT NULL)")

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def items(self):
        with self.connect() as db:
            rows = db.execute("SELECT * FROM items ORDER BY done, created, id").fetchall()
        return [{**dict(row), "done": bool(row["done"])} for row in rows]

    def add(self, text):
        if not isinstance(text, str) or not text.strip() or len(text.strip()) > 120:
            raise ValueError("请输入 1–120 字的待办事项")
        with self.connect() as db:
            if db.execute("SELECT COUNT(*) FROM items").fetchone()[0] >= 100:
                raise ValueError("最多保存 100 条待办，请删除不再需要的事项")
            db.execute("INSERT INTO items VALUES (?,?,0,?)", (uuid.uuid4().hex, text.strip(), time.time_ns()))

    def update(self, item_id, done):
        if not isinstance(done, bool):
            raise ValueError("done 必须为布尔值")
        with self.connect() as db:
            if not db.execute("UPDATE items SET done=? WHERE id=?", (int(done), item_id)).rowcount:
                raise KeyError("待办事项不存在")

    def delete(self, item_id):
        with self.connect() as db:
            if not db.execute("DELETE FROM items WHERE id=?", (item_id,)).rowcount:
                raise KeyError("待办事项不存在")


def render(items, page=0, width=768, height=552, font_path="", day=None, forecast=None, location="", weather_unavailable=False):
    if (width, height) not in ((768, 552), (800, 600)):
        raise ValueError("不支持的屏幕尺寸")
    pages = max(1, (len(items) + PAGE_SIZE - 1) // PAGE_SIZE)
    if not 0 <= page < pages:
        raise ValueError("页码已变化，请刷新待办列表")
    day = day or datetime.now().date()
    image = Image.new("P", (width, height), 1)
    image.putpalette(PALETTE)
    draw = ImageDraw.Draw(image)
    def text(x, y, value, size=18, fill=0, anchor="lt"):
        draw.text((x, y), str(value), font=font(size, font_path), fill=fill, anchor=anchor)
    # A quiet diary header: large day, compact month/week and a weather corner.
    text(32, 22, f"{day.day:02d}", 76)
    text(138, 35, f"{day.year} / {day.month:02d}", 22)
    text(138, 72, "星期" + "一二三四五六日"[day.weekday()], 23)
    if forecast:
        temperature = f'{round(forecast["temperature"])}°'
        text(width - 32, 25, temperature, 52, anchor="rt")
        name = location[:12]
        text(width - 32, 87, f'{name} · {weather.description(forecast["code"])}', 17, anchor="rt")
    else:
        text(width - 32, 42, "STILLINK", 21, anchor="rt")
        text(width - 32, 79, "天气暂不可用" if weather_unavailable else "每日清单", 17, anchor="rt")
    draw.line((32, 126, width - 32, 126), fill=0, width=2)
    text(32, 147, "待办事项", 25)
    done = sum(item["done"] for item in items)
    text(width - 32, 152, f"已完成 {done} / {len(items)}", 17, anchor="rt")
    draw.line((32, 185, width - 32, 185), fill=0, width=1)
    if items and done:
        draw.line((32, 185, 32 + int((width - 64) * done / len(items)), 185), fill=2, width=4)
    rows = items[page * PAGE_SIZE:(page + 1) * PAGE_SIZE]
    if not rows:
        text(width // 2, 300, "今天，想完成些什么？", 26, anchor="mt")
    row_height = (height - 238) // PAGE_SIZE
    face = font(17, font_path)
    for index, item in enumerate(rows):
        y = 202 + index * row_height
        draw.rounded_rectangle((34, y + 3, 54, y + 23), radius=5, outline=0, width=2)
        if item["done"]:
            draw.line((38, y + 13, 43, y + 18, 51, y + 7), fill=3, width=3)
        for line_index, line in enumerate(wrap_text(draw, item["text"], face, width - 112, 2)):
            ty = y + line_index * 18
            draw.text((72, ty), line, font=face, fill=0, anchor="lt")
            if item["done"]:
                draw.line((72, ty + 9, 72 + int(draw.textlength(line, font=face)), ty + 9), fill=0, width=1)
    draw.line((32, height - 31, width - 32, height - 31), fill=0)
    text(32, height - 23, "OPEN-METEO · CC BY 4.0" if forecast else "STILLINK / TO-DO", 10)
    text(width - 32, height - 25, f"{page + 1:02d} / {pages:02d}", 13, anchor="rt")
    return image


def handle(handler, store, url, font_path=""):
    try:
        if store is None:
            return handler.send(503, b'{"error":"Todo storage unavailable"}', "application/json")
        if handler.command == "POST" and (url.path == "/v1/todo/items" or url.path.startswith("/v1/todo/items/")):
            length = int(handler.headers.get("Content-Length", "0"))
            if not 0 < length <= 2048:
                raise ValueError("请求大小不正确")
            body = handler.rfile.read(length)
            if len(body) != length:
                raise ValueError("请求不完整")
            payload = json.loads(body)
            if not isinstance(payload, dict):
                raise ValueError("请求必须为 JSON 对象")
            if url.path == "/v1/todo/items":
                store.add(payload.get("text"))
            else:
                store.update(url.path.rsplit("/", 1)[-1], payload.get("done"))
        elif url.path.startswith("/v1/todo/items/") and handler.command == "DELETE":
            store.delete(url.path.rsplit("/", 1)[-1])
        elif handler.command == "GET" and url.path in ("/v1/todo/card.png", "/v1/todo/card.bin"):
            query = parse_qs(url.query)
            value = lambda key, default: int(query.get(key, [str(default)])[0])
            day = date.fromisoformat(query["date"][0]) if "date" in query else None
            forecast = None
            unavailable = False
            if "latitude" in query and "longitude" in query:
                lat, lon = float(query["latitude"][0]), float(query["longitude"][0])
                if not (math.isfinite(lat) and math.isfinite(lon) and abs(lat) <= 90 and abs(lon) <= 180):
                    raise ValueError("位置不正确")
                try:
                    forecast = weather.fetch_weather(lat, lon)
                except Exception:
                    unavailable = True
            image = render(store.items(), value("page", 0), value("width", 768), value("height", 552), font_path,
                           day, forecast, query.get("name", [""])[0], unavailable)
            if url.path.endswith(".bin"):
                return handler.send(200, pack_frame(image, panel=value("panel", 0)), "application/octet-stream")
            out = io.BytesIO()
            image.save(out, "PNG")
            return handler.send(200, out.getvalue(), "image/png")
        elif not (url.path == "/v1/todo/items" and handler.command == "GET"):
            raise KeyError("接口不存在")
        items = store.items()
        handler.send(200, json.dumps({"items": items, "page_size": PAGE_SIZE}, ensure_ascii=False).encode(), "application/json")
    except (ValueError, TypeError, UnicodeError) as error:
        handler.send(400, json.dumps({"error": str(error)}, ensure_ascii=False).encode(), "application/json")
    except KeyError as error:
        handler.send(404, json.dumps({"error": error.args[0]}, ensure_ascii=False).encode(), "application/json")
    except RuntimeError as error:
        handler.send(503, json.dumps({"error": str(error)}, ensure_ascii=False).encode(), "application/json")
