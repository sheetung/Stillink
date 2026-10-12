import hashlib
import json
import unittest
from app import Track, announcement
from protocol import CHUNK, HEADER, packets

class BroadcastTests(unittest.TestCase):
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
