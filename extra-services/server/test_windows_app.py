import json
import socket
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.request import Request, ProxyHandler, build_opener

import app
from PIL import Image
from music_card import pack_frame
from music_receiver import Receiver
from windows_app import Service, service_addresses


class WindowsServiceTests(unittest.TestCase):
    def test_addresses_exclude_loopback_link_local_and_duplicates(self):
        rows = [(socket.AF_INET, 0, 0, "", (ip, 0)) for ip in
                ["127.0.0.1", "169.254.1.2", "192.168.31.12", "192.168.31.12", "10.0.0.2"]]
        with patch("socket.getaddrinfo", return_value=rows):
            self.assertEqual(service_addresses(), ["http://10.0.0.2:8787", "http://192.168.31.12:8787"])

    def test_service_health_storage_and_clean_shutdown(self):
        old_music, old_album, old_key, old_todo = app.MUSIC, app.ALBUM, app.AUTH_KEY, app.TODO
        app.MUSIC = Receiver()
        try:
            with tempfile.TemporaryDirectory() as directory, patch("weather.find_font", return_value="test-font"):
                service = Service(directory, host="127.0.0.1", port=0, music_port=0)
                try:
                    opener = build_opener(ProxyHandler({}))
                    base = f"http://127.0.0.1:{service.http.server_port}"
                    with opener.open(Request(base + "/health", headers={"Authorization": "Bearer " + app.AUTH_KEY}), timeout=5) as response:
                        health = json.load(response)
                    self.assertTrue(health["services"]["music"]["receiver_ready"])
                    self.assertTrue(health["services"]["album"]["available"])
                    self.assertTrue(Path(directory, "data/album.sqlite3").is_file())
                    frame = pack_frame(Image.new("P", (768, 552), 1))
                    saved_key = app.AUTH_KEY
                    app.TODO.add("Keep shared task after upgrade")
                    photo = app.ALBUM.add(frame)
                    playlist = app.ALBUM.playlist([photo["id"]])
                    udp_port = service.music.getsockname()[1]
                finally:
                    service.close()
                self.assertFalse(service.thread.is_alive())
                self.assertFalse(app.MUSIC.listening)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.bind(("127.0.0.1", udp_port))
                # A replacement EXE opens the same directory without clearing photos or playlists.
                service = Service(directory, host="127.0.0.1", port=0, music_port=0)
                try:
                    self.assertEqual(app.TODO.items()[0]["text"], "Keep shared task after upgrade")
                    self.assertEqual(app.AUTH_KEY, saved_key)
                    self.assertEqual(app.ALBUM.image(photo["id"], "bin"), frame)
                    self.assertEqual(app.ALBUM.photos()[0]["id"], photo["id"])
                    self.assertEqual(app.ALBUM.selection(playlist), [photo["id"]])
                finally:
                    service.close()
        finally:
            app.MUSIC, app.ALBUM, app.AUTH_KEY, app.TODO = old_music, old_album, old_key, old_todo


if __name__ == "__main__":
    unittest.main()
