"""Music service card renderer and Stillink binary frame encoder."""
import binascii
import io
import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

WIDTH, HEIGHT = 768, 552
PALETTE = [0, 0, 0, 255, 255, 255, 255, 255, 0, 255, 0, 0] + [0] * (768 - 12)


@dataclass
class Track:
    title: str
    artist: str = ""
    album: str = ""
    player: str = ""
    cover: bytes = b""
    playing: bool = True
    number: int = 0

    @property
    def key(self):
        return self.player, self.title, self.artist, self.album, self.number


@lru_cache(maxsize=20)
def font(size, path=""):
    candidates = [path, "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf",
                  "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    raise RuntimeError("未找到中文字体，请通过 --font 指定字体文件")


def wrap_text(draw, text, face, width, max_lines):
    lines, current = [], ""
    for char in text.replace("\n", " "):
        if current and draw.textlength(current + char, font=face) > width:
            lines.append(current); current = ""
        current += char
    if current: lines.append(current)
    if len(lines) > max_lines:
        lines = lines[:max_lines]
        while lines[-1] and draw.textlength(lines[-1] + "…", font=face) > width:
            lines[-1] = lines[-1][:-1]
        lines[-1] += "…"
    return lines


def render_card(track, font_path=""):
    # Text uses palette pixels directly; only the cover is dithered.
    image = Image.new("P", (WIDTH, HEIGHT), 1); image.putpalette(PALETTE)
    draw = ImageDraw.Draw(image)
    def text(x, y, value, size=18, fill=0):
        draw.text((x, y), value, font=font(size, font_path), fill=fill, anchor="lt")

    cover_box = (20, 20, 532, 532)
    artwork = None
    if track.cover:
        try:
            with Image.open(io.BytesIO(track.cover)) as source:
                rgba = ImageOps.exif_transpose(source).convert("RGBA")
                background = Image.new("RGBA", rgba.size, "white"); background.alpha_composite(rgba)
                artwork = ImageOps.fit(background.convert("RGB"), (512, 512), method=Image.Resampling.LANCZOS)
        except (OSError, ValueError):
            pass
    if artwork is not None:
        palette = Image.new("P", (1, 1)); palette.putpalette(PALETTE)
        # Pillow may choose duplicated black entries in the 256-entry palette.
        indexed = artwork.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
        indexed = indexed.point([i if i < 4 else 0 for i in range(256)])
        mask = Image.new("L", (512, 512), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, 511, 511), radius=24, fill=255)
        image.paste(indexed, cover_box[:2], mask)
    else:
        draw.rounded_rectangle((20, 20, 531, 531), radius=24, fill=2, outline=0, width=2)
        draw.ellipse((132, 132, 420, 420), fill=0)
        draw.ellipse((247, 247, 305, 305), fill=1)
        text(224, 455, "暂无封面", 18)
    text(560, 30, "NOW PLAYING", 12)
    y = 80
    for line in wrap_text(draw, track.title, font(30, font_path), 188, 5):
        text(560, y, line, 30); y += 40
    y += 28
    for line in wrap_text(draw, track.artist or "未知歌手", font(19, font_path), 188, 4):
        text(560, y, line, 19); y += 29
    draw.line((560, 493, 748, 493), fill=0, width=1)
    text(560, 512, "STILLINK", 12)
    return image


def pack_frame(image):
    if image.size != (WIDTH, HEIGHT): raise ValueError("Expected 768x552 card")
    pixels = image.transpose(Image.Transpose.ROTATE_180).tobytes()
    if any(value > 3 for value in pixels): raise ValueError("Expected four-colour pixels")
    payload = bytes((pixels[i] << 6) | (pixels[i+1] << 4) | (pixels[i+2] << 2) | pixels[i+3]
                    for i in range(0, len(pixels), 4))
    return struct.pack(">2sBBHHIHH", b"\xa5\x5a", 1, 0, WIDTH, HEIGHT, len(payload),
                       binascii.crc_hqx(payload, 0xffff), 0) + payload
