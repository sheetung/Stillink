import binascii
import io
import json
import struct
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, ProxyHandler, build_opener

from PIL import Image
import album
import app
from music_card import pack_frame

urlopen = build_opener(ProxyHandler({})).open


class AlbumTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.previous = app.ALBUM
        app.ALBUM = album.Store(self.directory.name)
        self.server = app.ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"
        image = Image.new("P", (768, 552), 1)
        image.putpixel((0, 0), 3)
        image.putpixel((767, 551), 2)
        self.frame = pack_frame(image)

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        app.ALBUM = self.previous
        self.directory.cleanup()

    def request(self, path, body=None, method=None):
        if isinstance(body, dict):
            body = json.dumps(body).encode()
        return urlopen(Request(self.base + path, data=body, method=method))

    def add(self, name="photo"):
        with self.request("/v1/album/photos?name=" + name, self.frame) as response:
            return json.load(response)["id"]

    def test_persisted_frame_and_preview_match(self):
        photo_id = self.add()
        app.ALBUM = album.Store(self.directory.name)  # Simulate service restart.
        with self.request("/v1/album/photos") as response:
            self.assertEqual(json.load(response)["photos"][0]["id"], photo_id)
        with self.request(f"/v1/album/photos/{photo_id}.bin") as response:
            self.assertEqual(response.read(), self.frame)
        with self.request(f"/v1/album/photos/{photo_id}.png") as response:
            image = Image.open(io.BytesIO(response.read()))
            self.assertEqual(pack_frame(image), self.frame)
        with self.request("/health") as response:
            self.assertTrue(json.load(response)["services"]["album"]["available"])

    def test_selection_sequence_random_deletion_and_independent_cursors(self):
        first, second = self.add("first"), self.add("second")
        with self.request("/v1/album/playlists", {"photos": [first, second]}) as response:
            playlist = json.load(response)["id"]
        app.ALBUM = album.Store(self.directory.name)
        def next_photo(current="", mode="sequence"):
            with self.request(f"/v1/album/next.json?playlist={playlist}&current={current}&mode={mode}") as response:
                return json.load(response)["id"]
        self.assertEqual(next_photo(), first)
        self.assertEqual(next_photo(first), second)
        self.assertEqual(next_photo(second), first)
        self.assertEqual(next_photo(), first, "A second device starts from its own cursor")
        self.assertEqual(next_photo(first, "random"), second)
        with self.request(f"/v1/album/photos/{first}", method="DELETE") as response:
            self.assertTrue(json.load(response)["deleted"])
        self.assertEqual(next_photo(first), second)
        with self.request(f"/v1/album/photos/{second}", method="DELETE"): pass
        with self.assertRaises(HTTPError) as caught: next_photo()
        self.assertEqual(caught.exception.code, 404)

    def test_bad_frames_and_preflight(self):
        corrupt = self.frame[:-1] + bytes([self.frame[-1] ^ 1])
        for body in (b"not an image", corrupt, self.frame[:-1]):
            with self.assertRaises(HTTPError) as caught:
                self.request("/v1/album/photos", body)
            self.assertEqual(caught.exception.code, 400)
        self.assertEqual(app.ALBUM.photos(), [])
        with self.request("/v1/album/photos", method="OPTIONS") as response:
            self.assertEqual(response.status, 204)
            self.assertIn("DELETE", response.headers["Access-Control-Allow-Methods"])
            self.assertEqual(response.headers["Access-Control-Allow-Origin"], "*")

    def test_native_geometry_and_invalid_selection(self):
        payload = bytes([0x55]) * 120000
        frame = struct.pack(">2sBBHHIHH", b"\xa5\x5a", 2, 1, 800, 600, len(payload), binascii.crc_hqx(payload, 0xffff), 0) + payload
        photo = app.ALBUM.add(frame, "native")
        playlist = app.ALBUM.playlist([photo["id"]])
        self.assertEqual(app.ALBUM.next(playlist, "", "sequence", 800, 600)["id"], photo["id"])
        with self.assertRaises(KeyError): app.ALBUM.next(playlist, "", "sequence", 768, 552)
        for ids in ([], ["missing"], [None]):
            with self.assertRaises(ValueError): app.ALBUM.playlist(ids)


if __name__ == "__main__":
    unittest.main()
