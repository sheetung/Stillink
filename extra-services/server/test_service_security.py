import json
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, ProxyHandler, build_opener
import app
import album
from service_security import load_key
from music_card import pack_frame
from PIL import Image

class ServiceSecurityTests(unittest.TestCase):
    def test_persistence_rotation_and_protected_routes(self):
        original_key, original_album = app.AUTH_KEY, app.ALBUM
        opener = build_opener(ProxyHandler({}))
        with tempfile.TemporaryDirectory() as directory:
            key = load_key(directory)
            self.assertEqual(key, load_key(directory))
            app.AUTH_KEY = key
            app.ALBUM = album.Store(directory)
            server = app.ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            def request(path, method="GET", body=None, token=None):
                return opener.open(Request(base+path, data=body, method=method, headers={"Authorization":"Bearer "+token} if token else {}))
            try:
                for path, method in [("/v1/calendar/month","GET"),("/v1/calendar/anniversaries","POST"),("/v1/calendar/schedules/anything","DELETE"),("/health","GET"),("/v1/weather.json","GET"),("/v1/music.png","GET"),("/v1/album/photos","GET"),("/v1/album/photos","POST"),("/v1/album/photos/anything","DELETE")]:
                    with self.assertRaises(HTTPError) as caught: request(path,method,token="wrong")
                    self.assertEqual(caught.exception.code,401)
                with request("/health",token=key) as response:self.assertEqual(json.load(response)["status"],"ok")
                frame=pack_frame(Image.new("P",(768,552),1))
                with request("/v1/album/photos","POST",frame,key) as response:photo=json.load(response)
                rotated=load_key(directory,rotate=True);app.AUTH_KEY=rotated
                self.assertNotEqual(rotated,key)
                self.assertEqual(rotated,load_key(directory))
                with self.assertRaises(HTTPError):request("/health",token=key)
                with request("/v1/album/photos",token=rotated) as response:self.assertEqual(json.load(response)["photos"][0]["id"],photo["id"])
                self.assertEqual(app.ALBUM.image(photo["id"],"bin"),frame)
                with request("/health","OPTIONS") as response:self.assertIn("Authorization",response.headers["Access-Control-Allow-Headers"])
            finally:
                server.shutdown();server.server_close();thread.join()
                app.AUTH_KEY,app.ALBUM=original_key,original_album

if __name__=="__main__":unittest.main()
