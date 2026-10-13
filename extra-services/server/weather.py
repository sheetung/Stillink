"""Open-Meteo -> four-colour PNG and Stillink v1 binary frame.

Run: uv run app.py --host 0.0.0.0 --port 8787 --font /path/to/chinese.ttf
"""
import binascii
import io
import json
import math
import os
import struct
import threading
import time
from datetime import datetime, timezone
from functools import lru_cache
from pathlib import Path
from urllib.parse import parse_qs, urlencode
from urllib.request import Request, urlopen

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 768, 552
BLACK, WHITE, YELLOW, RED = 0, 1, 2, 3
PALETTE = [0, 0, 0, 255, 255, 255, 241, 202, 0, 197, 35, 29] + [0] * (768 - 12)
FONT_PATH = os.getenv("STILLINK_FONT", "")
CACHE = {}
CACHE_LOCK = threading.Lock()


def find_font():
    candidates = [FONT_PATH, "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                  "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", "C:/Windows/Fonts/msyh.ttc"]
    for path in candidates:
        if path and Path(path).is_file():
            return path
    raise RuntimeError("Chinese font missing: set STILLINK_FONT or install fonts-noto-cjk")


@lru_cache(maxsize=24)
def font(size):
    return ImageFont.truetype(find_font(), size)


def fetch_weather(latitude, longitude):
    key = (round(latitude, 5), round(longitude, 5))
    # Cache raw forecast per location for 15 minutes, independent of display name.
    with CACHE_LOCK:
        cached = CACHE.get(key)
        if cached and time.monotonic() - cached[0] < 900:
            return cached[1]
    query = urlencode(dict(latitude=key[0], longitude=key[1],
        current="temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
        daily="weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
        forecast_days=4, timezone="auto", timeformat="unixtime", wind_speed_unit="ms"))
    with urlopen(Request("https://api.open-meteo.com/v1/forecast?" + query,
                         headers={"User-Agent": "Stillink/weather"}), timeout=20) as response:
        raw = json.load(response)
    current, daily = raw["current"], raw["daily"]
    data = dict(temperature=current["temperature_2m"], humidity=current["relative_humidity_2m"],
        wind=current["wind_speed_10m"], code=current["weather_code"],
        updated=current["time"], utc_offset=raw["utc_offset_seconds"],
        rain_probability=daily["precipitation_probability_max"][0],
        days=[dict(code=daily["weather_code"][i], low=daily["temperature_2m_min"][i],
                   high=daily["temperature_2m_max"][i], date=daily["time"][i]) for i in range(4)])
    # Reject missing/null weather values before generating a misleading card.
    values = [data[k] for k in ("temperature", "humidity", "wind", "code", "updated", "utc_offset", "rain_probability")]
    values += [v for day in data["days"] for v in day.values()]
    if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
        raise ValueError("Incomplete forecast")
    with CACHE_LOCK:
        if len(CACHE) >= 128:
            CACHE.pop(next(iter(CACHE)))
        CACHE[key] = time.monotonic(), data
    return data


def description(code):
    if code == 0: return "晴"
    if code <= 2: return "多云"
    if code == 3: return "阴"
    if code <= 48: return "雾"
    if code >= 95: return "雷雨"
    if 71 <= code <= 77: return "雪"
    if 85 <= code <= 86: return "阵雪"
    if code >= 80: return "阵雨"
    if code in (65, 67): return "大雨"
    if code == 63: return "中雨"
    return "小雨"


def render(name, data):
    image = Image.new("P", (WIDTH, HEIGHT), WHITE)
    image.putpalette(PALETTE)
    draw = ImageDraw.Draw(image)

    def text(x, y, value, size=18, fill=BLACK, anchor="lt"):
        draw.text((x, y), str(value), font=font(size), fill=fill, anchor=anchor)

    def icon(code, x, y, size):
        def point(a, b): return (x + a * size / 160, y + b * size / 160)
        def circle(a, b, r, fill):
            draw.ellipse((*point(a-r, b-r), *point(a+r, b+r)), fill=fill, outline=BLACK, width=max(1, round(size/32)))
        thick = max(1, round(size/32))
        if code <= 2:
            circle(80 if code == 0 else 100, 70 if code == 0 else 45, 34, YELLOW)
            cx, cy = (80, 70) if code == 0 else (100, 45)
            for angle in range(0, 360, 45):
                a = math.radians(angle)
                draw.line([point(cx+math.cos(a)*44, cy+math.sin(a)*44), point(cx+math.cos(a)*56, cy+math.sin(a)*56)], fill=BLACK, width=thick)
        if code != 0:
            circle(52, 77, 29, WHITE); circle(86, 59, 38, WHITE); circle(119, 81, 27, WHITE)
            draw.rounded_rectangle((*point(27, 70), *point(146, 108)), radius=max(1, round(size/10)), fill=WHITE, outline=BLACK, width=thick)
            draw.rectangle((*point(44, 65), *point(129, 97)), fill=WHITE)
            if code >= 51:
                for a in (50, 84, 118): draw.line([point(a, 120), point(a-10, 138)], fill=BLACK, width=thick)

    # Fit long location names within the left header column.
    location_size = 26
    while location_size > 14 and draw.textlength(name, font=font(location_size)) > 390:
        location_size -= 1
    while draw.textlength(name, font=font(location_size)) > 390:
        name = name[:-2] + "…"
    text(38, 30, name, location_size); text(38, 67, "天气卡片", 13)
    date = datetime.fromtimestamp(data["updated"] + data["utc_offset"], timezone.utc)
    week = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
    text(730, 33, f"{date.month} 月 {date.day} 日 · {week[date.weekday()]}", 17, anchor="rt")
    text(730, 64, f"{date:%H:%M} 更新 · STILLINK", 12, anchor="rt")
    draw.line((38, 102, 730, 102), fill=BLACK, width=2)
    text(33, 124, round(data["temperature"]), 112)
    text(213, 140, "°", 44); text(275, 173, description(data["code"]), 30)
    today = data["days"][0]
    text(277, 223, f'{round(today["low"])}° / {round(today["high"])}°', 19)
    icon(data["code"], 527, 119, 168)
    for x, label, value in [(39, "风速", f'{data["wind"]:.1f} m/s'), (287, "相对湿度", f'{data["humidity"]}%'), (536, "降水概率", f'{data["rain_probability"]}%')]:
        text(x, 338, label, 13); text(x, 364, value, 18)
    draw.line((38, 406, 730, 406), fill=BLACK, width=2)
    for i, day in enumerate(data["days"][1:]):
        x = 38 + i * 243
        dt = datetime.fromtimestamp(day["date"] + data["utc_offset"], timezone.utc)
        text(x, 425, "明天" if i == 0 else week[dt.weekday()], 16)
        icon(day["code"], x+140, 422, 50)
        text(x, 453, description(day["code"]), 14)
        text(x, 480, f'{round(day["low"])}° / {round(day["high"])}°', 22)
        if i < 2: draw.line((x+220, 429, x+220, 504), fill=BLACK)
    text(730, 522, "OPEN-METEO.COM · CC BY 4.0", 10, anchor="rt")
    return image


def pack_frame(image):
    w, h = image.size
    # Rotate 180 degrees; pack 4 pixels per byte, high bits first.
    pixels = image.transpose(Image.Transpose.ROTATE_180).tobytes()
    payload = bytes((pixels[i] << 6) | (pixels[i+1] << 4) | (pixels[i+2] << 2) | pixels[i+3]
                    for i in range(0, len(pixels), 4))
    return struct.pack(">2sBBHHIH2s", b"\xa5\x5a", 1, 0, w, h, len(payload), binascii.crc_hqx(payload, 0xffff), b"\0\0") + payload


def handle(handler, url):
    if url.path not in ("/v1/weather.bin", "/v1/weather.png", "/v1/weather.json"):
        return handler.send(404, b'{"error":"Not found"}', "application/json")
    try:
        query = parse_qs(url.query)
        lat, lon = float(query["latitude"][0]), float(query["longitude"][0])
        name = query.get("name", ["天气"])[0].strip()
        if not (math.isfinite(lat) and math.isfinite(lon) and abs(lat) <= 90 and abs(lon) <= 180):
            raise ValueError()
        if not name or len(name.encode()) >= 96 or any(ord(c) < 32 for c in name): raise ValueError()
    except (KeyError, ValueError):
        return handler.send(400, b'{"error":"Invalid location"}', "application/json")
    try:
        data = fetch_weather(lat, lon)
        if url.path.endswith(".json"):
            return handler.send(200, json.dumps(data, ensure_ascii=False).encode(), "application/json", data)
        image = render(name, data)
        if url.path.endswith(".bin"):
            return handler.send(200, pack_frame(image), "application/octet-stream", data)
        out = io.BytesIO(); image.save(out, "PNG")
        handler.send(200, out.getvalue(), "image/png", data)
    except (BrokenPipeError, ConnectionResetError):
        pass
    except Exception as exc:
        print(f"weather upstream/render error: {exc}", flush=True)
        handler.send(502, b'{"error":"Weather provider unavailable"}', "application/json")
