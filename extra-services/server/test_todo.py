import io
import json
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, ProxyHandler, build_opener

from PIL import Image
import album
import app
import todo


class TodoTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.previous = app.TODO, app.AUTH_KEY
        app.TODO, app.AUTH_KEY = todo.Store(self.directory.name), "todo-test-key"
        self.server = app.ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"
        self.opener = build_opener(ProxyHandler({}))

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        app.TODO, app.AUTH_KEY = self.previous
        self.directory.cleanup()

    def request(self, path, body=None, method=None, key="todo-test-key"):
        data = json.dumps(body).encode() if body is not None else None
        return self.opener.open(Request(self.base + path, data=data, method=method,
                                      headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"}))

    def items(self, body=None, path="/v1/todo/items", method=None):
        with self.request(path, body, method) as response:
            return json.load(response)["items"]

    def test_shared_persistence_complete_reopen_delete(self):
        first = self.items({"text": "  买牛奶  "})[0]
        self.items({"text": "散步"})
        app.TODO = todo.Store(self.directory.name)
        self.assertEqual([i["text"] for i in self.items()], ["买牛奶", "散步"])
        items = self.items({"done": True}, "/v1/todo/items/" + first["id"])
        self.assertEqual(items[-1]["id"], first["id"])
        self.assertTrue(items[-1]["done"])
        items = self.items({"done": False}, "/v1/todo/items/" + first["id"])
        self.assertEqual(items[0]["id"], first["id"])
        items = self.items(path="/v1/todo/items/" + first["id"], method="DELETE")
        self.assertEqual([i["text"] for i in items], ["散步"])
        with self.request("/health") as response:
            self.assertTrue(json.load(response)["services"]["todo"]["available"])

    def test_card_pages_native_geometry_and_binary_crc(self):
        for index in range(9): self.items({"text": f"事项 {index} " + "长文本" * 20})
        for dimensions, panel in [((768, 552), 0), ((768, 552), 1), ((800, 600), 1)]:
            for page in [0, 1]:
                query = f"?width={dimensions[0]}&height={dimensions[1]}&panel={panel}&page={page}"
                with self.request("/v1/todo/card.png" + query) as response:
                    png = Image.open(io.BytesIO(response.read())).convert("RGB")
                    self.assertEqual(png.size, dimensions)
                    self.assertLessEqual(len(png.getcolors()), 4)
                with self.request("/v1/todo/card.bin" + query) as response:
                    self.assertEqual(response.headers["Content-Type"], "application/octet-stream")
                    frame = response.read()
                self.assertEqual(frame[3], panel)
                width, height, decoded = album.preview(frame)
                self.assertEqual((width, height), dimensions)
                self.assertEqual(Image.open(io.BytesIO(decoded)).convert("RGB").tobytes(), png.tobytes())

    def test_date_weather_and_weather_failure_still_produce_card(self):
        path = "/v1/todo/card.png?date=2026-10-17&latitude=30.27&longitude=120.15&name=Hangzhou"
        with patch("weather.fetch_weather", return_value={"temperature": 24, "code": 2}) as fetch:
            with self.request(path) as response:
                sunny = response.read()
            fetch.assert_called_once_with(30.27, 120.15)
        with patch("weather.fetch_weather", side_effect=OSError("offline")):
            with self.request(path) as response:
                offline = response.read()
            self.assertNotEqual(sunny, offline)
            self.assertEqual(Image.open(io.BytesIO(offline)).size, (768, 552))
        with self.assertRaises(HTTPError) as caught:
            self.request("/v1/todo/card.png?date=not-a-date")
        self.assertEqual(caught.exception.code, 400)

    def test_auth_validation_and_empty_card(self):
        for path, method in [("/v1/todo/items", "GET"), ("/v1/todo/items", "POST"),
                             ("/v1/todo/items/missing", "DELETE"), ("/v1/todo/card.bin", "GET")]:
            with self.assertRaises(HTTPError) as caught:
                self.request(path, method=method, key="wrong")
            self.assertEqual(caught.exception.code, 401)
        for payload in [{"text": " "}, {"text": "x" * 121}, {"text": 12}, []]:
            with self.assertRaises(HTTPError) as caught: self.items(payload)
            self.assertEqual(caught.exception.code, 400)
        first = self.items({"text": "测试"})[0]
        with self.assertRaises(HTTPError) as caught:
            self.items({"done": "true"}, "/v1/todo/items/" + first["id"])
        self.assertEqual(caught.exception.code, 400)
        self.assertFalse(self.items()[0]["done"])
        self.items(path="/v1/todo/items/" + first["id"], method="DELETE")
        with self.request("/v1/todo/card.png") as response: self.assertEqual(response.status, 200)
        with self.assertRaises(HTTPError) as caught: self.request("/v1/todo/card.png?page=1")
        self.assertEqual(caught.exception.code, 400)


if __name__ == "__main__": unittest.main()
