import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import worker from './worker.mjs';

const { Script } = await import('node:vm');
const dashboard = await (await worker.fetch(new Request('https://stats.test/'), {})).text();
new Script(dashboard.match(/<script>([\s\S]*?)<\/script>/)[1]);
const sqlite = new DatabaseSync(':memory:');
sqlite.exec(await readFile(new URL('schema.sql', import.meta.url), 'utf8'));
const env = { ADMIN_KEY: 'test-admin-key', DB: {
  prepare(sql) {
    let args = [];
    return { bind(...values) { args = values; return this; },
      async first() { return sqlite.prepare(sql).get(...args); },
      async all() { return { results: sqlite.prepare(sql).all(...args) }; },
      async run() { return sqlite.prepare(sql).run(...args); },
    };
  },
  async batch(statements) { return Promise.all(statements.map(s => s.all())); },
} };
const origin = 'https://092399.xyz', visitor = randomUUID();
const event = (name, extra = {}) => ({ event_id: randomUUID(), visitor_id: visitor, event: name, ...extra });
const send = (body, from = origin) => worker.fetch(new Request('https://stats.test/events', { method: 'POST', headers: { Origin: from, 'Content-Type': 'application/json' }, body: typeof body === 'string' ? body : JSON.stringify(body) }), env);
const report = (key = env.ADMIN_KEY) => worker.fetch(new Request('https://stats.test/stats?days=7', { headers: { Authorization: 'Bearer ' + key } }), env);

assert.equal((await report('wrong')).status, 401);
assert.equal((await worker.fetch(new Request('https://stats.test/stats'), { ...env, ADMIN_KEY: '' })).status, 503);
assert.equal((await send(event('visit'), 'https://evil.test')).status, 403);
const preflight = await worker.fetch(new Request('https://stats.test/events', { method: 'OPTIONS', headers: { Origin: origin } }), env);
assert.equal(preflight.status, 204);
assert.equal(preflight.headers.get('Access-Control-Allow-Origin'), origin);
assert.equal((await send({ ...event('visit'), raw_error: 'should never store raw logs' })).status, 400);
assert.equal((await send(event('flash_success', { attempt_id: 'invalid' }))).status, 400);
assert.equal((await send(event('visit', { chip: 'arbitrary-device-id' }))).status, 400);
assert.equal((await send(event('visit', { error: 'sensitive raw error' }))).status, 400);
assert.equal((await send(event('visit', { visitor_id: [visitor] }))).status, 400);
assert.equal((await send(event('visit', { version: ['v1.6.0'] }))).status, 400);
assert.equal((await send('x'.repeat(1025))).status, 400);
assert.equal((await send('{')).status, 400);
const visit = event('visit');
assert.equal((await send(visit)).status, 204);
await send(visit); await send(event('visit'));
const attempt = randomUUID();
await send(event('flash_start', { attempt_id: attempt, chip: 'esp32s3', version: 'v1.6.0', mode: 'ota' }));
await send(event('flash_success', { attempt_id: attempt, chip: 'esp32s3', version: 'v1.6.0', mode: 'ota', duration_ms: 42000 }));
await send(event('flash_success', { attempt_id: attempt }));
await send(event('flash_failure', { attempt_id: attempt, error: 'write' })); // first terminal result wins
await send(event('flash_start', { attempt_id: randomUUID() })); // an unfinished attempt
await send(event('flash_failure', { attempt_id: randomUUID(), chip: 'esp32c3', version: 'custom', mode: 'custom', error: 'write' })); // start packet can be lost
await send(event('connect_cancel', { attempt_id: randomUUID() }));
await send(event('connect_failure', { attempt_id: randomUUID(), error: 'connection' }));
const stats = await (await report()).json();
assert.deepEqual(stats.visits, { views: 2, visitors: 1 });
assert.deepEqual(stats.flashes, { attempts: 3, success: 1, failure: 1, unfinished: 1 });
assert.equal(stats.daily[0].success, 1); assert.equal(stats.daily[0].failure, 1);
assert.equal(stats.firmware.length, 2);
assert.equal(stats.connections.cancelled, 1); assert.equal(stats.connections.failure, 1);
assert.equal(stats.errors.length, 2);
const publicResponse = await worker.fetch(new Request('https://stats.test/public-stats'), { ...env, ADMIN_KEY: '' });
assert.equal(publicResponse.status, 200);
assert.equal(publicResponse.headers.get('Access-Control-Allow-Origin'), '*');
assert.deepEqual(await publicResponse.json(), { visitors: 1, success: 1 });
assert.equal((await worker.fetch(new Request('https://stats.test/public-stats', { method: 'POST' }), env)).status, 405);
assert.equal(JSON.stringify(stats).includes(visitor), false, 'admin report contains aggregates, not visitor IDs');
assert.equal((await worker.fetch(new Request('https://stats.test/'), env)).status, 200);
assert.equal((await worker.fetch(new Request('https://stats.test/unknown'), env)).status, 404);
assert.deepEqual(stats.duration, { samples: 1, average_ms: 42000, median_ms: 42000 });
const platformEvent = event('flash_success', { attempt_id: randomUUID(), duration_ms: 18000 });
await worker.fetch(new Request('https://stats.test/events', { method: 'POST', headers: { Origin: origin, 'Content-Type': 'application/json', 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0) Chrome/130.0 Safari/537.36 Edg/130.0' }, body: JSON.stringify(platformEvent) }), env);
const extended = await (await report()).json();
assert.deepEqual(extended.duration, { samples: 2, average_ms: 30000, median_ms: 30000 });
assert.equal(extended.platforms.find(p => p.browser === 'Edge' && p.os === 'Windows').success, 1);
assert.equal(JSON.stringify(extended).includes('Mozilla'), false);
assert.equal((await send(event('flash_success', { attempt_id: randomUUID(), chip: 'esp32c6', version: 'custom', mode: 'custom' }))).status,204);
const c6stats = await (await report()).json();
assert.equal(c6stats.firmware.find(row=>row.chip==='esp32c6'&&row.mode==='custom').success,1);
for (let i = 0; i < 60; ++i) await send(event('visit'));
assert.equal((await send(event('visit'))).status, 429);
sqlite.close();
console.log('STATS WORKER: PASS (CORS, auth, validation, limits, deduplication, reordered outcomes, SQL reports)');
