import hashlib
import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from runtime import load_config
from app import Track, announcement
from protocol import CHUNK, HEADER, packets

class BroadcastTests(unittest.TestCase):
    def test_optional_yaml_config(self):
        with TemporaryDirectory() as folder:
            path=Path(folder)/"config.yml"
            self.assertEqual(load_config(path),dict(target="255.255.255.255",port=8790,player=""))
            path.write_text('target: 192.168.31.12\nport: 8791\nplayer: Spotify\n',encoding='utf-8')
            self.assertEqual(load_config(path),dict(target="192.168.31.12",port=8791,player="Spotify"))
            path.write_text('port: 70000',encoding='utf-8')
            with self.assertRaises(ValueError):load_config(path)

    def test_metadata_and_original_cover(self):
        data=json.loads(announcement(Track("歌曲","歌手","专辑","player",b"original cover")))
        import base64
        self.assertEqual(base64.b64decode(data["cover"]),b"original cover")
        self.assertEqual(data["title"],"歌曲")
        self.assertFalse(json.loads(announcement(None))["playing"])

    def test_chunk_protocol(self):
        body=bytes(range(256))*20
        chunks=packets(body)
        self.assertGreater(len(chunks),1)
        for i,packet in enumerate(chunks):
            magic,digest,index,count,length=HEADER.unpack(packet[:HEADER.size])
            self.assertEqual((magic,digest,index,count,length),(b"SKM1",hashlib.sha256(body).digest(),i,len(chunks),len(body)))
            self.assertLessEqual(len(packet),HEADER.size+CHUNK)
        self.assertEqual(b"".join(p[HEADER.size:] for p in chunks),body)

if __name__=="__main__":unittest.main()
