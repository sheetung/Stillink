"""Shared anniversaries, schedules and calendar cards."""
import calendar
from contextlib import contextmanager
import io
import json
import math
import sqlite3
import uuid
from datetime import date, datetime
from pathlib import Path
from urllib.parse import parse_qs
from lunar_python import Solar
from PIL import Image, ImageDraw
from music_card import PALETTE, font, pack_frame, wrap_text
import weather

class Store:
    def __init__(self, directory):
        Path(directory).mkdir(parents=True, exist_ok=True)
        self.path = Path(directory) / 'calendar.sqlite3'
        with self.connect() as db:
            db.execute('CREATE TABLE IF NOT EXISTS entries (id TEXT PRIMARY KEY, kind TEXT NOT NULL, payload TEXT NOT NULL)')
    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path)
        try:
            with db:
                yield db
        finally:
            db.close()
    def items(self, kind):
        with self.connect() as db:
            return [dict(json.loads(row[1]), id=row[0]) for row in db.execute('SELECT id,payload FROM entries WHERE kind=? ORDER BY id', (kind,))]
    def save(self, kind, payload, entry_id=None):
        if not isinstance(payload, dict): raise ValueError('请求必须为 JSON 对象')
        title = payload.get('title', '')
        if not isinstance(title, str) or not 1 <= len(title.strip()) <= 80: raise ValueError('名称应为 1–80 字')
        day = date.fromisoformat(payload.get('date', ''))
        if not 1901 <= day.year <= 2099: raise ValueError('日期应在 1901–2099 年')
        value = {'title': title.strip(), 'date': day.isoformat()}
        if kind == 'anniversaries':
            if not isinstance(payload.get('yearly', False), bool): raise ValueError('重复选项不正确')
            value['yearly'] = payload.get('yearly', False)
        else:
            time = payload.get('time', '')
            if not isinstance(time, str): raise ValueError('时间不正确')
            if time:
                datetime.strptime(time, '%H:%M')
                if len(time) != 5: raise ValueError('时间不正确')
            value['time'] = time
        with self.connect() as db:
            if entry_id:
                if not db.execute('SELECT 1 FROM entries WHERE id=? AND kind=?',(entry_id,kind)).fetchone(): raise KeyError('记录不存在')
            else:
                if db.execute('SELECT COUNT(*) FROM entries WHERE kind=?',(kind,)).fetchone()[0] >= 500: raise ValueError('最多保存 500 条记录')
                entry_id = uuid.uuid4().hex
            db.execute('INSERT OR REPLACE INTO entries VALUES (?,?,?)',(entry_id,kind,json.dumps(value,ensure_ascii=False)))
        return entry_id
    def delete(self, kind, entry_id):
        with self.connect() as db:
            if not db.execute('DELETE FROM entries WHERE id=? AND kind=?',(entry_id,kind)).rowcount: raise KeyError('记录不存在')

def occurrence(item, year):
    d = date.fromisoformat(item['date'])
    if not item.get('yearly'): return d
    return date(year,d.month,min(d.day,calendar.monthrange(year,d.month)[1]))

def anniversary_day(item, today):
    d = occurrence(item,today.year)
    return occurrence(item,today.year+1) if item.get('yearly') and d < today else d

def month_days(year, month):
    result=[]
    for day in range(1,calendar.monthrange(year,month)[1]+1):
        d=date(year,month,day);lunar=Solar.fromYmd(year,month,day).getLunar()
        festivals=list(lunar.getFestivals())+list(Solar.fromYmd(year,month,day).getFestivals())
        result.append({'date':d.isoformat(),'lunar':lunar.getMonthInChinese()+'月' if lunar.getDay()==1 else lunar.getDayInChinese(), 'term':lunar.getJieQi(), 'festival':' · '.join(festivals),'weekend':d.weekday()>=5})
    return result

def render(store, kind, query, font_path=''):
    q=lambda k,default='':query.get(k,[str(default)])[0]
    w,h=int(q('width',768)),int(q('height',552))
    if (w,h) not in ((768,552),(800,600)): raise ValueError('不支持的屏幕尺寸')
    today=date.fromisoformat(q('date',date.today().isoformat()))
    image=Image.new('P',(768,552),1);image.putpalette(PALETTE);draw=ImageDraw.Draw(image)
    def text(x,y,s,size=18,fill=0,anchor='lt'):
        draw.text((x,y),str(s),font=font(size,font_path),fill=fill,anchor=anchor)
    anniversaries=store.items('anniversaries')
    schedules=store.items('schedules')
    if kind=='anniversary':
        item=next((x for x in anniversaries if x['id']==q('id')),None)
        if item is None: raise KeyError('请选择纪念日')
        days=(anniversary_day(item,today)-today).days
        text(36,30,'A DAY TO REMEMBER',15)
        lines=wrap_text(draw,item['title'],font(32,font_path),680,2)
        for i,line in enumerate(lines):text(384,92+i*40,line,32,anchor='mt')
        text(384,190,'美好如期而至' if days==0 else '距离这一天，还有' if days>0 else '我们已经走过',20,anchor='mt')
        text(384,230,abs(days),126,3,anchor='mt');text(520,327,'天',22)
        text(384,402,item['date']+(' · 每年重复' if item.get('yearly') else ''),18,anchor='mt')
        draw.line((36,464,732,464),fill=0,width=1);text(36,490,today.isoformat(),17)
    else:
        year,month=map(int,q('month',today.strftime('%Y-%m')).split('-'))
        if not 1901<=year<=2099: raise ValueError('年份应在 1901–2099 年')
        days=month_days(year,month);text(36,24,f'CALENDAR / {year}',16);text(36,44,f'{month:02d}',54);text(125,72,'月',22)
        if q('weather')=='1' and q('latitude') and q('longitude'):
            lat,lon=float(q('latitude')),float(q('longitude'))
            if not math.isfinite(lat) or not math.isfinite(lon) or abs(lat)>90 or abs(lon)>180: raise ValueError('位置不正确')
            try:
                forecast=weather.fetch_weather(lat,lon)
                weather.draw_icon(draw, forecast['code'], 646, 3, 78)
                text(732,77,f'{q("name")[:12]} · {weather.description(forecast["code"])} {round(forecast["temperature"])}°',19,anchor='rt')
            except Exception:
                text(732,77,'天气暂不可用',18,anchor='rt')
        draw.line((36,110,732,110),fill=0,width=2)
        for i,label in enumerate('一二三四五六日'):text(36+(i+.5)*696/7,124,label,17,3 if i>=5 else 0,'mt')
        offset=date(year,month,1).weekday();rows=math.ceil((offset+len(days))/7);rh=316/rows
        for i,day in enumerate(days):
            row,col=divmod(i+offset,7);x=36+(col+.5)*696/7;y=156+row*rh
            if day['date']==today.isoformat() and q('today','1')=='1':draw.rounded_rectangle((int(x-30),int(y-4),int(x+30),int(y+30)),radius=5,fill=2)
            text(x,y,i+1,24,3 if col>=5 else 0,'mt')
            if q('lunar','1')=='1':text(x,y+29,(day['festival'] or day['term'] or day['lunar'])[:6],12,anchor='mt')
            if any(e['date']==day['date'] for e in schedules):draw.line((int(x-12),int(y+46),int(x+12),int(y+46)),fill=3,width=2)
            if q('anniversaries','1')=='1' and any(occurrence(e,year).isoformat()==day['date'] for e in anniversaries):draw.ellipse((int(x+27),int(y+4),int(x+32),int(y+9)),fill=3)
        draw.line((36,490,732,490),fill=0,width=1)
        upcoming=[e for e in anniversaries if occurrence(e,year).year==year and occurrence(e,year).month==month] if q('anniversaries','1')=='1' else []
        text(36,507,'    '.join(f'{occurrence(e,year).day}日 {e["title"][:12]}' for e in upcoming[:2]) or 'STILLINK / CALENDAR',16)
    if (w,h)!=(768,552):image=image.resize((w,h),Image.Resampling.NEAREST)
    return image

def handle(handler,store,url,font_path=''):
    try:
        if store is None: raise RuntimeError('日历服务未就绪')
        parts=url.path.strip('/').split('/');q=parse_qs(url.query)
        if len(parts)>=3 and parts[2] in ('anniversaries','schedules'):
            kind=parts[2];entry_id=parts[3] if len(parts)==4 else None
            if handler.command=='POST':
                n=int(handler.headers.get('Content-Length','0'))
                if not 0<n<=4096: raise ValueError('请求大小不正确')
                store.save(kind,json.loads(handler.rfile.read(n)),entry_id)
            elif handler.command=='DELETE' and entry_id:store.delete(kind,entry_id)
            elif handler.command!='GET':raise KeyError('接口不存在')
            result={'items':store.items(kind)}
        elif handler.command=='GET' and parts[-1]=='month':
            year,month=map(int,q.get('month',[date.today().strftime('%Y-%m')])[0].split('-'))
            if not 1901<=year<=2099: raise ValueError('年份应在 1901–2099 年')
            result={'days':month_days(year,month),'anniversaries':store.items('anniversaries'),'schedules':store.items('schedules')}
        elif handler.command=='GET' and parts[-1] in ('calendar.png','calendar.bin','anniversary.png','anniversary.bin'):
            kind=parts[-1].split('.')[0];image=render(store,kind,q,font_path)
            if url.path.endswith('.bin'):return handler.send(200,pack_frame(image,panel=int(q.get('panel',['0'])[0])),'application/octet-stream')
            out=io.BytesIO();image.save(out,'PNG');return handler.send(200,out.getvalue(),'image/png')
        else:raise KeyError('接口不存在')
        return handler.send(200,json.dumps(result,ensure_ascii=False).encode(),'application/json')
    except (ValueError,TypeError,UnicodeError,IndexError) as e:handler.send(400,json.dumps({'error':str(e)},ensure_ascii=False).encode(),'application/json')
    except KeyError as e:handler.send(404,json.dumps({'error':e.args[0]},ensure_ascii=False).encode(),'application/json')
    except (RuntimeError,sqlite3.Error) as e:handler.send(503,json.dumps({'error':str(e)},ensure_ascii=False).encode(),'application/json')
