import binascii
import io
import struct
import unittest
from unittest.mock import patch

from PIL import Image

from app import SyncState, check_device
from card import Track, pack_frame, render_card


class MusicTests(unittest.TestCase):
    def test_track_changes_are_coalesced(self):
        state = SyncState(60)
        first = Track("第一首", "歌手", "专辑", "player")
        self.assertFalse(state.due(first, 0))
        self.assertTrue(state.due(first, 3))
        state.success(first, 3)
        self.assertFalse(state.due(first, 120), "Same song must not be uploaded repeatedly")
        first.playing = False
        self.assertFalse(state.due(first, 121))
        first.playing = True
        self.assertFalse(state.due(first, 125), "Resume must not re-upload")
        second = Track("第二首", player="player")
        third = Track("第三首", player="player")
        self.assertFalse(state.due(second, 130))
        self.assertFalse(state.due(third, 131))
        self.assertFalse(state.due(third, 133))
        self.assertTrue(state.due(third, 134), "Rapid skips should send only the settled song")

    def test_interval_and_retry(self):
        state = SyncState(60)
        a, b = Track("A"), Track("B")
        state.success(a, 10)
        self.assertFalse(state.due(b, 12))
        self.assertFalse(state.due(b, 69))
        self.assertTrue(state.due(b, 70))
        state.failed(70)
        self.assertFalse(state.due(b, 99))
        self.assertTrue(state.due(b, 100))

    def test_no_silent_weather_disable(self):
        with patch("app.device_request", side_effect=[b"{}", b'{"enabled":true}']) as request:
            with self.assertRaisesRegex(RuntimeError, "关闭自动天气"):
                check_device("http://device", False)
            self.assertEqual(request.call_count, 2)
        with patch("app.device_request", side_effect=[b"{}", b'{"enabled":true}', b'{"saved":true}']) as request:
            check_device("http://device", True)
            self.assertEqual(request.call_args.args[2], b'{"enabled":false}')

    def test_render_and_frame(self):
        cover = Image.new("RGB", (120, 80), "red")
        out = io.BytesIO(); cover.save(out, "PNG")
        card = render_card(Track("很长的歌曲名称" * 12, "歌手" * 20, "专辑" * 20, cover=out.getvalue()))
        self.assertEqual(card.size, (768, 552))
        self.assertTrue(set(card.tobytes()).issubset({0, 1, 2, 3}))
        self.assertEqual(card.getpixel((100, 100)), 3)
        frame = pack_frame(card)
        header = struct.unpack(">2sBBHHIHH", frame[:16])
        self.assertEqual(header[:6], (b"\xa5\x5a", 1, 0, 768, 552, 105984))
        self.assertEqual(header[6], binascii.crc_hqx(frame[16:], 0xffff))
        self.assertEqual(len(frame), 106000)
        pixels = card.tobytes()
        decoded = bytes((byte >> shift) & 3 for byte in frame[16:] for shift in (6, 4, 2, 0))
        self.assertEqual(decoded, pixels[::-1])
        for cover in (b"", b"not a valid image"):
            fallback = render_card(Track("暂无封面", cover=cover))
            self.assertEqual(fallback.getpixel((40, 100)), 2)


if __name__ == "__main__":
    unittest.main()
