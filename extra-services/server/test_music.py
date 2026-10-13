import base64
import binascii
import hashlib
import json
import io
from PIL import Image
from music_card import Track, render_card
import struct
import unittest
from music_receiver import Receiver


def packets(message):
    body=json.dumps(message,ensure_ascii=False).encode()
    count=(len(body)+1199)//1200
    return [struct.pack(">4s32sHHI",b"SKM1",hashlib.sha256(body).digest(),i,count,len(body))+body[i*1200:(i+1)*1200] for i in range(count)]


class MusicReceiverTests(unittest.TestCase):
    def test_reordering_duplicates_and_playback(self):
        receiver=Receiver()
        message=dict(title="歌曲",artist="歌手",album="专辑",player="player",playing=True,cover=base64.b64encode(b"invalid cover"*300).decode())
        burst=packets(message)
        for p in reversed(burst):receiver.feed(p,"host")
        data=json.loads(receiver.response("/v1/music.json")[1])
        self.assertTrue(data["playing"]);self.assertEqual(data["title"],"歌曲")
        _,frame,_=receiver.response("/v1/music.bin")
        self.assertEqual(len(frame),106000)
        self.assertEqual(struct.unpack(">H",frame[12:14])[0],binascii.crc_hqx(frame[16:],0xffff))
        for p in burst:receiver.feed(p,"host")
        self.assertEqual(receiver.response("/v1/music.bin")[1],frame)
        message["playing"]=False
        for p in packets(message):receiver.feed(p,"host")
        paused=json.loads(receiver.response("/v1/music.json")[1])
        self.assertFalse(paused["playing"]);self.assertEqual(paused["version"],data["version"])
        receiver.received-=40
        self.assertFalse(json.loads(receiver.response("/v1/music.json")[1])["playing"])

    def test_large_cover_preserves_artwork_area(self):
        source=Image.new("RGB",(100,100),"black")
        source.paste((255,0,0),(50,0,100,100))
        out=io.BytesIO();source.save(out,"PNG")
        image=render_card(Track("大封面测试","歌手",cover=out.getvalue()))
        self.assertEqual(image.size,(768,552))
        self.assertEqual(image.getpixel((20,20)),0)
        self.assertEqual(image.getpixel((531,531)),3)
        self.assertEqual(image.getpixel((10,10)),1)
        self.assertEqual(image.getpixel((540,200)),1)

    def test_incomplete_and_corrupted_broadcast(self):
        receiver=Receiver()
        burst=packets(dict(title="歌",artist="",album="",player="",playing=True,cover=base64.b64encode(b"x"*2000).decode()))
        receiver.feed(burst[0],"host")
        self.assertEqual(receiver.response("/v1/music.bin")[0],204)
        for p in burst[1:]:receiver.feed(p[:-1]+b"!","host")
        self.assertEqual(receiver.response("/v1/music.bin")[0],204)


if __name__=="__main__":unittest.main()
