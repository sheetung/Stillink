import binascii
import importlib.util
import io
import json
import struct
import threading
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import ProxyHandler, build_opener

from PIL import Image

urlopen = build_opener(ProxyHandler({})).open

spec = importlib.util.spec_from_file_location("weather_app", Path(__file__).with_name("app.py"))
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)
DATA = dict(temperature=22, humidity=65, wind=2.3, code=0, updated=1791802800,
            utc_offset=28800, rain_probability=10,
            days=[dict(code=c, low=17, high=27, date=1791763200+i*86400) for i, c in enumerate((0, 2, 61, 3))])


class WeatherTests(unittest.TestCase):
    def test_frame_contract(self):
        image = Image.new("P", (768, 552), 1)
        image.putpixel((0, 0), 3)
        image.putpixel((767, 551), 2)
        frame = app.pack_frame(image)
        magic, ver, panel, w, h, length, crc, reserved = struct.unpack(">2sBBHHIH2s", frame[:16])
        self.assertEqual((magic, ver, panel, w, h, length, reserved), (b"\xa5\x5a", 1, 0, 768, 552, 105984, b"\0\0"))
        self.assertEqual(len(frame), 106000)
        self.assertEqual(crc, binascii.crc_hqx(frame[16:], 0xffff))
        self.assertEqual(frame[-1] & 3, 3)
        self.assertEqual(frame[16] >> 6, 2)

    def test_http_and_png_binary_equivalence(self):
        server = app.ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        query = "?latitude=30.29365&longitude=120.16142&name=%E6%9D%AD%E5%B7%9E"
        try:
            with patch.object(app, "fetch_weather", return_value=DATA) as fetch:
                with urlopen(base+"/v1/weather.bin"+query) as response:
                    frame = response.read()
                    self.assertEqual(response.headers["X-Weather-UTC-Offset"], "28800")
                    self.assertEqual(response.headers["Content-Type"], "application/octet-stream")
                with urlopen(base+"/v1/weather.png"+query) as response:
                    image = Image.open(io.BytesIO(response.read()))
                self.assertEqual(app.pack_frame(image), frame, "Preview and ESP frame must have identical pixels")
                self.assertEqual(set(image.tobytes()), {0, 1, 2})
                before = fetch.call_count
                for bad in ("?latitude=nan&longitude=120", "?latitude=91&longitude=120", "?latitude=30"):
                    with self.assertRaises(HTTPError) as caught: urlopen(base+"/v1/weather.bin"+bad)
                    self.assertEqual(caught.exception.code, 400)
                self.assertEqual(fetch.call_count, before)
            with patch.object(app, "fetch_weather", side_effect=TimeoutError("provider offline")):
                with self.assertRaises(HTTPError) as caught: urlopen(base+"/v1/weather.bin"+query)
                self.assertEqual(caught.exception.code, 502)
        finally:
            server.shutdown(); server.server_close(); thread.join()


if __name__ == "__main__":
    unittest.main()
