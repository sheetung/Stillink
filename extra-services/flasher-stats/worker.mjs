// Paste this entire file into the Cloudflare Worker editor. Requires D1 binding DB.
const EVENTS = new Set(['visit', 'connect_start', 'connect_success', 'connect_failure', 'connect_cancel', 'flash_start', 'flash_success', 'flash_failure']);
const ERRORS = new Set(['', 'permission', 'connection', 'download', 'integrity', 'chip_mismatch', 'invalid_file', 'write', 'unknown']);
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const isUUID = value => typeof value === 'string' && UUID.test(value);
const DAY = 86400000, ZONE = 8 * 3600000;
function json(value, status = 200, headers = {}) {
  return Response.json(value, { status, headers: { 'Cache-Control': 'no-store', ...headers } });
}
function cors(origin) {
  return { 'Access-Control-Allow-Origin': origin, 'Access-Control-Allow-Methods': 'GET, POST, OPTIONS', 'Access-Control-Allow-Headers': 'Content-Type', 'Access-Control-Max-Age': '86400', Vary: 'Origin' };
}
async function publicStats(request, env) {
  const headers = cors('*');
  if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers });
  if (request.method !== 'GET') return json({ error: 'method not allowed' }, 405, headers);
  const totals = await env.DB.prepare(`SELECT
    COUNT(DISTINCT CASE WHEN event_type = 'visit' THEN visitor_id END) AS visitors,
    COUNT(CASE WHEN event_type = 'flash_success' THEN 1 END) AS success FROM events`).first();
  return json({ visitors: totals.visitors, success: totals.success }, 200, headers);
}
async function readEvent(request) {
  const reader = request.body?.getReader();
  if (!reader) throw new Error('empty body');
  const chunks = []; let size = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.length;
    if (size > 1024) { await reader.cancel(); throw new Error('body too large'); }
    chunks.push(value);
  }
  const bytes = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  const e = JSON.parse(new TextDecoder().decode(bytes));
  const fields = ['event_id', 'visitor_id', 'event', 'attempt_id', 'chip', 'version', 'mode', 'duration_ms', 'error'];
  if (!e || Object.keys(e).some(k => !fields.includes(k)) || !EVENTS.has(e.event) || !isUUID(e.event_id) || !isUUID(e.visitor_id)) throw new Error('invalid event');
  if (e.event !== 'visit' && !isUUID(e.attempt_id)) throw new Error('invalid attempt');
  if (e.event === 'visit' && e.attempt_id != null) throw new Error('invalid visit');
  if (e.chip !== undefined && !['esp32s3', 'esp32c3', 'unknown'].includes(e.chip)) throw new Error('invalid chip');
  if (e.version !== undefined && (typeof e.version !== 'string' || (e.version !== 'custom' && !/^v\d{1,4}\.\d{1,4}\.\d{1,4}$/.test(e.version)))) throw new Error('invalid version');
  if (e.mode !== undefined && !['ota', 'full', 'custom'].includes(e.mode)) throw new Error('invalid mode');
  if (e.duration_ms !== undefined && (!Number.isInteger(e.duration_ms) || e.duration_ms < 0 || e.duration_ms > DAY)) throw new Error('invalid duration');
  if (!ERRORS.has(e.error ?? '')) throw new Error('invalid error');
  return e;
}
async function ingest(request, env) {
  const origin = request.headers.get('Origin');
  const allowed = (env.ALLOWED_ORIGINS || 'https://092399.xyz').split(',').map(s => s.trim());
  if (!origin || !allowed.includes(origin)) return json({ error: 'origin not allowed' }, 403);
  const headers = cors(origin);
  if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers });
  if (request.method !== 'POST') return json({ error: 'method not allowed' }, 405, headers);
  if (request.headers.get('Content-Type')?.split(';')[0].trim().toLowerCase() !== 'application/json') return json({ error: 'expected JSON' }, 415, headers);
  let event;
  try { event = await readEvent(request); } catch { return json({ error: 'invalid event' }, 400, headers); }
  const now = Date.now();
  const rate = await env.DB.prepare('SELECT COUNT(*) AS n FROM events WHERE visitor_id = ? AND created_at > ?').bind(event.visitor_id, now - 60000).first();
  if (rate.n >= 60) return json({ error: 'too many events' }, 429, headers);
  await env.DB.prepare(`INSERT INTO events (event_id, created_at, event_type, visitor_id, attempt_id, chip, version, mode, duration_ms, error)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT DO NOTHING`).bind(
    event.event_id, now, event.event, event.visitor_id, event.attempt_id ?? null,
    event.chip ?? 'unknown', event.version ?? '', event.mode ?? '', event.duration_ms ?? null, event.error ?? '',
  ).run();
  return new Response(null, { status: 204, headers });
}
async function stats(request, env) {
  if (!env.ADMIN_KEY) return json({ error: '请先设置 ADMIN_KEY 密钥' }, 503);
  if (request.headers.get('Authorization') !== 'Bearer ' + env.ADMIN_KEY) return json({ error: '管理密钥不正确' }, 401);
  if (request.method !== 'GET') return json({ error: 'method not allowed' }, 405);
  const days = Number(new URL(request.url).searchParams.get('days') || 30);
  if (![7, 30, 90].includes(days)) return json({ error: 'invalid range' }, 400);
  const since = Math.floor((Date.now() + ZONE) / DAY) * DAY - ZONE - (days - 1) * DAY;
  // Group by attempt so duplicated or reordered terminal events cannot inflate totals.
  const attempts = `WITH attempts AS (
    SELECT attempt_id, MAX(event_type='flash_start') AS started, MAX(event_type='flash_success') AS success,
      MAX(event_type='flash_failure') AS failure FROM events
    WHERE attempt_id IN (SELECT attempt_id FROM events WHERE created_at >= ? AND event_type LIKE 'flash_%')
      AND event_type LIKE 'flash_%' GROUP BY attempt_id
  )`;
  const queries = [
    ['SELECT COUNT(*) AS views, COUNT(DISTINCT visitor_id) AS visitors FROM events WHERE event_type = \'visit\' AND created_at >= ?', since],
    [attempts + ' SELECT COUNT(*) AS attempts, COALESCE(SUM(success),0) AS success, COALESCE(SUM(failure AND NOT success),0) AS failure, COALESCE(SUM(NOT success AND NOT failure),0) AS unfinished FROM attempts', since],
    [`SELECT strftime('%Y-%m-%d',created_at/1000,'unixepoch','+8 hours') AS day,
      SUM(event_type='visit') AS views, COUNT(DISTINCT CASE WHEN event_type='visit' THEN visitor_id END) AS visitors,
      SUM(event_type='flash_success') AS success, SUM(event_type='flash_failure') AS failure
      FROM events WHERE created_at >= ? GROUP BY day ORDER BY day DESC`, since],
    [`SELECT chip, version, mode, SUM(event_type='flash_success') AS success, SUM(event_type='flash_failure') AS failure
      FROM events WHERE created_at >= ? AND event_type IN ('flash_success','flash_failure') GROUP BY chip, version, mode ORDER BY success+failure DESC LIMIT 50`, since],
    [`SELECT error, event_type, COUNT(*) AS count FROM events WHERE created_at >= ? AND event_type IN ('flash_failure','connect_failure') GROUP BY error, event_type ORDER BY count DESC LIMIT 20`, since],
    [`SELECT SUM(event_type='connect_success') AS success, SUM(event_type='connect_failure') AS failure, SUM(event_type='connect_cancel') AS cancelled FROM events WHERE created_at >= ?`, since],
  ];
  const results = await env.DB.batch(queries.map(([sql, value]) => env.DB.prepare(sql).bind(value)));
  return json({ days, timezone: 'Asia/Shanghai', visits: results[0].results[0], flashes: results[1].results[0], daily: results[2].results, firmware: results[3].results, errors: results[4].results, connections: results[5].results[0] });
}
export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (path === '/' && request.method === 'GET') return new Response(DASHBOARD, { headers: {
      'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
      'Content-Security-Policy': "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'",
    } });
    if (!['/events', '/stats', '/public-stats'].includes(path)) return json({ error: 'not found' }, 404);
    try {
      if (path === '/public-stats') return await publicStats(request, env);
      return path === '/events' ? await ingest(request, env) : await stats(request, env);
    } catch {
      const origin = request.headers.get('Origin');
      const allowed = (env.ALLOWED_ORIGINS || 'https://092399.xyz').split(',').map(s => s.trim());
      return json({ error: '统计服务暂不可用，请检查 DB 绑定和建表是否完成' }, 503, path === '/events' && allowed.includes(origin) ? cors(origin) : {});
    }
  },
};

const DASHBOARD = `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Stillink 烧录统计</title>
<style>*{box-sizing:border-box}body{margin:0;background:#f4f6f9;color:#273446;font:14px/1.6 system-ui,sans-serif}main{max-width:1120px;margin:40px auto;padding:0 20px}h1{font-size:25px}h2{font-size:16px}header,.login{display:flex;gap:12px;align-items:center;flex-wrap:wrap}header{justify-content:space-between}.card{background:#fff;border:1px solid #dfe5ec;border-radius:14px;padding:20px;margin:18px 0}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}.cards .card{margin:0}.card b{display:block;font-size:28px}small,p{color:#6c7a8c}input,select,button{font:inherit;border:1px solid #dfe5ec;border-radius:8px;padding:9px 12px}button{background:#426c99;color:#fff;cursor:pointer}input{min-width:200px}table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:10px 8px;border-bottom:1px solid #edf0f4;text-align:left;white-space:nowrap}.scroll{overflow:auto}[hidden]{display:none!important}#message{color:#426c99}label{font-size:12px;color:#6c7a8c}</style>
<main><header><div><h1>Stillink 烧录统计</h1><small>按北京时间汇总 · 独立访客为浏览器匿名标识的估算值</small></div><select id="days" aria-label="统计周期"><option value="7">最近 7 天</option><option value="30" selected>最近 30 天</option><option value="90">最近 90 天</option></select></header>
<section class="card login"><input id="key" type="password" placeholder="管理密钥 ADMIN_KEY" aria-label="管理密钥" autocomplete="off"><button id="load">查看统计</button><button id="logout" hidden>退出</button><span id="message" role="status"></span></section>
<div id="report" hidden><div class="cards" id="metrics"></div><p id="connections"></p><p>成功率 = 成功 ÷（成功 + 失败）；未完成可能是烧录中、关闭页面或上报遗漏，不计为失败。</p>
<section class="card"><h2>每日统计</h2><div class="scroll"><table id="daily"></table></div></section>
<section class="card"><h2>固件与芯片</h2><div class="scroll"><table id="firmware"></table></div></section>
<section class="card"><h2>失败原因</h2><div class="scroll"><table id="errors"></table></div></section></div></main>
<script>
const $=id=>document.getElementById(id);let adminKey='';
const labels={permission:'串口权限',connection:'连接失败',download:'固件下载',integrity:'固件校验',chip_mismatch:'芯片不匹配',invalid_file:'文件或地址无效',write:'写入或校验失败',unknown:'其他错误',flash_failure:'烧录',connect_failure:'连接'};
function table(id,head,rows){const el=$(id);el.replaceChildren();const tr=document.createElement('tr');head.forEach(s=>{const th=document.createElement('th');th.textContent=s;tr.appendChild(th)});el.appendChild(tr);(rows.length?rows:[['暂无数据']]).forEach(row=>{const tr=document.createElement('tr');row.forEach(s=>{const td=document.createElement('td');td.textContent=s??'—';tr.appendChild(td)});el.appendChild(tr)})}
async function load(){const key=$('key').value||adminKey;if(!key){$('message').textContent='请输入管理密钥';return}$('load').disabled=true;$('message').textContent='读取中…';try{const r=await fetch('/stats?days='+$('days').value,{headers:{Authorization:'Bearer '+key},credentials:'omit'}),d=await r.json();if(!r.ok)throw new Error(d.error||'读取失败');adminKey=key;$('key').value='';$('key').hidden=true;$('logout').hidden=false;$('report').hidden=false;$('message').textContent='已更新';const f=d.flashes,n=f.success+f.failure;$('metrics').replaceChildren();[['访问量',d.visits.views],['估算访客',d.visits.visitors],['烧录成功',f.success],['烧录失败',f.failure],['未完成',f.unfinished],['成功率',n?(f.success/n*100).toFixed(1)+'%':'—']].forEach(([name,value])=>{const box=document.createElement('div');box.className='card';const small=document.createElement('small'),b=document.createElement('b');small.textContent=name;b.textContent=value;box.append(small,b);$('metrics').appendChild(box)});$('connections').textContent='串口连接：成功 '+(d.connections.success||0)+' / 失败 '+(d.connections.failure||0)+' / 用户取消 '+(d.connections.cancelled||0);table('daily',['日期','访问','访客','成功','失败'],d.daily.map(x=>[x.day,x.views,x.visitors,x.success,x.failure]));table('firmware',['芯片','版本','方式','成功','失败'],d.firmware.map(x=>[x.chip,x.version||'—',x.mode||'—',x.success,x.failure]));table('errors',['阶段','原因','次数'],d.errors.map(x=>[labels[x.event_type],labels[x.error]||'其他错误',x.count]));}catch(e){$('message').textContent=e.message;$('report').hidden=true;$('key').hidden=false;}finally{$('load').disabled=false}}
$('load').onclick=load;$('days').onchange=()=>{if(adminKey)load()};$('key').onkeydown=e=>{if(e.key==='Enter')load()};$('logout').onclick=()=>{adminKey='';$('key').value='';$('key').hidden=false;$('logout').hidden=true;$('report').hidden=true;$('message').textContent='已退出';$('metrics').replaceChildren();['daily','firmware','errors'].forEach(id=>$(id).replaceChildren());$('connections').textContent=''};
</script></html>`;
