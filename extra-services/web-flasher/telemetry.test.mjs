import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { webcrypto } from 'node:crypto';
import vm from 'node:vm';
const source = (await readFile(new URL('telemetry.js', import.meta.url), 'utf8')).replace('export function', 'function');
const endpoint = 'https://stats.test/events';
const counterSource = (await readFile(new URL('counters.js', import.meta.url), 'utf8')).replace('export function', 'function');
const counterNodes = new Map(['publicStats', 'visitorCount', 'successCount'].map(id => [id, { hidden: true, textContent: '—' }]));
let counterData = { visitors: 1234, success: 56 }, counterOffline = false;
const counterContext = { URL, document: { getElementById: id => counterNodes.get(id) }, fetch: async url => {
  assert.equal(url, 'https://stats.test/public-stats');
  if (counterOffline) throw new Error('offline');
  return { ok: true, json: async () => counterData };
} };
vm.runInNewContext(counterSource, counterContext);
const refresh = counterContext.createPublicCounters(endpoint, counterContext.document);
await refresh();
assert.equal(counterNodes.get('publicStats').hidden, false);
assert.equal(counterNodes.get('visitorCount').textContent, '1,234');
assert.equal(counterNodes.get('successCount').textContent, '56');
counterData = { visitors: -1, success: 100 }; await refresh();
assert.equal(counterNodes.get('successCount').textContent, '56');
counterOffline = true; await refresh();
assert.equal(counterNodes.get('successCount').textContent, '56');
const storage = new Map();
function create({ dnt = false, broken = false, denied = false } = {}) {
  const sent = [];
  const context = {
    URL, crypto: webcrypto, Date, window: {}, navigator: { doNotTrack: dnt ? '1' : '0' },
    localStorage: { getItem(k) { if (denied) throw new Error('blocked'); return storage.get(k); }, setItem(k,v) { storage.set(k,v); } },
    fetch(url, options) { if (broken) throw new Error('offline'); sent.push({ url, ...JSON.parse(options.body), options }); return Promise.reject(new Error('network unavailable')); },
  };
  vm.runInNewContext(source, context);
  return { telemetry: context.createTelemetry(endpoint), sent, context };
}
const first = create();
assert.equal(first.sent[0].event, 'visit');
const finish = first.telemetry.start('flash', { chip: 'esp32s3', version: 'v1.6.0', mode: 'ota' });
finish('success'); finish('failure', 'write');
assert.deepEqual(first.sent.map(x => x.event), ['visit', 'flash_start', 'flash_success']);
assert.equal(first.sent[1].attempt_id, first.sent[2].attempt_id);
assert.equal(first.sent[0].options.credentials, 'omit');
assert.equal(first.sent[0].options.keepalive, true);
assert.equal(create().sent[0].visitor_id, first.sent[0].visitor_id, 'same browser retains anonymous ID');
assert.equal(create({ dnt: true }).sent.length, 0);
assert.equal(create({ denied: true }).sent.length, 1, 'storage blocking must not break flashing');
assert.doesNotThrow(() => create({ broken: true }).telemetry.start('flash')('failure', 'write'));
assert.doesNotThrow(() => first.context.createTelemetry('not a url').start('flash')('success'));
assert.doesNotThrow(() => first.context.createTelemetry('').start('flash')('success'));
assert.doesNotThrow(() => first.context.createTelemetry('http://insecure.test/events').start('flash')('success'));
await new Promise(resolve => setImmediate(resolve)); // rejected analytics requests remain handled
console.log('TELEMETRY: PASS (nonblocking, rejected network, opt-out, identity, exactly one outcome)');
