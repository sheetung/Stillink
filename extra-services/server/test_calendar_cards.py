import binascii
import io
import json
import tempfile
import unittest
from datetime import date
from urllib.parse import urlsplit,parse_qs
from unittest.mock import patch
import calendar_cards as cards
from PIL import Image

class Handler:
    def __init__(self,method='GET',payload=None):
        self.command=method;body=json.dumps(payload).encode() if payload is not None else b''
        self.headers={'Content-Length':str(len(body))};self.rfile=io.BytesIO(body)
    def send(self,status,body,kind):self.status=status;self.body=body;self.kind=kind

class CalendarTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.store=cards.Store(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def test_persistence_and_isolation(self):
        key=self.store.save('anniversaries',{'title':'生日','date':'2024-02-29','yearly':True})
        self.assertEqual(cards.anniversary_day(self.store.items('anniversaries')[0],date(2025,2,1)),date(2025,2,28))
        self.assertEqual(cards.Store(self.tmp.name).items('anniversaries')[0]['id'],key)
        with self.assertRaises(KeyError):self.store.delete('schedules',key)
        self.store.save('anniversaries',{'title':'新名称','date':'2024-02-29','yearly':False},key)
        self.assertEqual(self.store.items('anniversaries')[0]['title'],'新名称')
        self.store.delete('anniversaries',key);self.assertEqual(self.store.items('anniversaries'),[])
    def test_lunar_and_terms(self):
        days={x['date']:x for x in cards.month_days(2026,10)}
        self.assertIn('重阳',days['2026-10-18']['festival'])
        self.assertEqual(days['2026-10-23']['term'],'霜降')
        self.assertEqual(len(cards.month_days(2024,2)),29)
    def test_api_and_validation(self):
        h=Handler('POST',{'title':'开会','date':'2026-10-18','time':'09:00'})
        cards.handle(h,self.store,urlsplit('/v1/calendar/schedules'));self.assertEqual(h.status,200)
        item=json.loads(h.body)['items'][0]
        h=Handler('POST',{'title':'','date':'invalid'});cards.handle(h,self.store,urlsplit('/v1/calendar/schedules'));self.assertEqual(h.status,400)
        self.assertEqual(self.store.items('schedules')[0]['title'],'开会')
        h=Handler('DELETE');cards.handle(h,self.store,urlsplit('/v1/calendar/schedules/'+item['id']));self.assertEqual(h.status,200)
    def test_render_and_binary_geometry(self):
        key=self.store.save('anniversaries',{'title':'测试纪念日','date':'2026-11-15','yearly':True})
        for kind in ('calendar','anniversary'):
            for w,h in ((768,552),(800,600)):
                query=f'date=2026-10-18&month=2026-08&width={w}&height={h}&id={key}'
                r=Handler();cards.handle(r,self.store,urlsplit('/v1/calendar/'+kind+'.bin?'+query))
                self.assertEqual(r.status,200);self.assertEqual(len(r.body),16+w*h//4)
                self.assertEqual(binascii.crc_hqx(r.body[16:],0xffff),int.from_bytes(r.body[12:14],'big'))
        with patch.object(cards.weather,'fetch_weather',side_effect=OSError('offline')):
            image=cards.render(self.store,'calendar',parse_qs('date=2026-10-18&weather=1&latitude=30&longitude=120'))
            self.assertEqual(image.size,(768,552))
if __name__=='__main__':unittest.main()
