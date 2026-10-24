"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const page = fs.readFileSync(path.join(__dirname, "..", "page", "index.html"), "utf8");
const script = page.match(/<script>\r?\n"use strict";\r?\n([\s\S]*?)\r?\n<\/script>/)[1];
new vm.Script(script); // Parse the complete page script, including the new controls.
const start = script.indexOf("var wifiPollTimer = null;");
const end = script.indexOf("var SETT_LABEL", start);
assert(start >= 0 && end > start);
assert(!page.includes('id="scanWifi"') && !page.includes('id="chooseWifi"'), "extra buttons removed");
const elements = Object.create(null);
const requests = [], responses = [], timers = [];
for (const id of ["wifiStatus", "staSsid", "staPass", "connectWifi", "forgetWifi", "lanAddress", "lanLink", "mdnsAddress", "mdnsLink", "wifiNetworks"]) {
  assert(page.includes('id="' + id + '"'));
  elements[id] = { value:"", style:{}, hidden:false, disabled:false, textContent:"", options:[], events:{},
    setAttribute(name, value){ this[name] = value; },
    focus(){ this.focused = true; },
    addEventListener(event, callback){ this.events[event] = callback; },
    replaceChildren(...options){ this.options = options; },
    add(option){ this.options.push(option); } };
}
const context = vm.createContext({
  $: id => elements[id], api: url => url, Date, TextEncoder, encodeURIComponent,
  Option: function(text, value){ this.text = text; this.value = value; },
  setTimeout: callback => { timers.push(callback); return timers.length; },
  clearTimeout: () => {},
  fetch: async (url, options) => {
    requests.push({url, options});
    assert(responses.length, "unexpected request: " + url);
    const response = responses.shift();
    if (response instanceof Error) throw response;
    return { ok: response.status < 400, status: response.status,
      json: async () => response.body, text: async () => "request failed" };
  },
});
vm.runInContext(script.slice(start, end), context);
const flush = async () => { for (let i = 0; i < 4; i++) await new Promise(resolve => setImmediate(resolve)); };
const respond = (body, status = 200) => responses.push({body, status});
const click = id => elements[id].events.click.call(elements[id]);

(async () => {
  const quoted = 'Home "Wi-Fi" & kitchen';
  const markup = '<img src=x onerror=alert(1)>';
  respond({networks:[{ssid:quoted, rssi:-40, secured:true}, {ssid:quoted, rssi:-60, secured:true}, {ssid:markup, rssi:-70, secured:false}]});
  click("staSsid");
  assert.equal(elements.staSsid.disabled, true);
  await flush();
  assert.equal(requests.at(-1).url, "/api/wifi/scan");
  assert.equal(elements.wifiNetworks.hidden, false, "selection opens after scan");
  assert.equal(elements.wifiNetworks.focused, true);
  assert.equal(elements.wifiNetworks.options.length, 3, "duplicates are removed");
  assert.equal(elements.wifiNetworks.options[1].value, quoted);
  assert.equal(elements.wifiNetworks.options[2].value, markup, "SSID stays plain option text");
  assert.equal(elements.staSsid.disabled, false);

  elements.wifiNetworks.value = quoted;
  elements.wifiNetworks.events.change.call(elements.wifiNetworks);
  assert.equal(elements.staSsid.value, quoted);
  assert.equal(elements.wifiNetworks.hidden, true, "selection closes after choosing a network");
  const password = 'correct&pass+word';
  elements.staPass.value = password;
  respond({status:"connecting"}, 202);
  respond({status:"connecting", ssid:quoted, saved:false, ip:"", reason:0});
  click("connectWifi");
  await flush();
  const request = requests.find(entry => entry.url === "/api/wifi/connect");
  assert.equal(request.options.method, "POST");
  const fields = new URLSearchParams(request.options.body);
  assert.equal(fields.get("ssid"), quoted);
  assert.equal(fields.get("password"), password, "credentials round-trip URL encoding");
  assert.equal(elements.connectWifi.disabled, true);
  assert(timers.length, "connecting polls asynchronously");

  context.renderWifiStatus({status:"connected", ssid:quoted, ip:"192.168.1.42", hostname:"stillink-8120.local", saved:true});
  assert.equal(elements.lanAddress.hidden, false);
  assert.equal(elements.lanLink.href, "http://192.168.1.42/");
  assert.equal(elements.mdnsLink.href, "http://stillink-8120.local/");
  assert.equal(elements.mdnsAddress.hidden, false);
  assert.equal(elements.staPass.value, "", "password field clears after connection");
  assert.equal(elements.connectWifi.disabled, false);

  context.renderWifiStatus({status:"failed", ssid:quoted, ip:"", saved:false, reason:202});
  assert.equal(elements.lanAddress.hidden, true);
  assert(elements.wifiStatus.textContent.includes("连接失败"));
  assert.equal(elements.connectWifi.disabled, false, "failed connection can be retried");

  const count = requests.length;
  elements.staSsid.value = "中".repeat(11);
  click("connectWifi");
  await flush();
  assert.equal(requests.length, count, "SSID limit is bytes, not characters");
  assert(elements.wifiStatus.textContent.includes("32 字节"));

  respond({networks:[]});
  click("staSsid");
  await flush();
  assert.equal(elements.wifiNetworks.hidden, true, "empty scan keeps choices hidden");
  assert(elements.wifiStatus.textContent.includes("未找到网络"));

  respond({}, 409);
  click("staSsid");
  await flush();
  assert(elements.wifiStatus.textContent.includes("扫描失败"));
  assert.equal(elements.staSsid.disabled, false);

  respond({status:"idle"});
  elements.staSsid.value = quoted;
  click("forgetWifi");
  await flush();
  assert.equal(requests.at(-1).url, "/api/wifi/forget");
  assert.equal(requests.at(-1).options.method, "POST");
  assert.equal(elements.staSsid.value, "");
  assert.equal(elements.lanAddress.hidden, true);
  assert.equal(responses.length, 0);
  console.log("WIFI PAGE: PASS (scan, SSID escaping/deduplication, connection, polling, failure/retry, byte limits, forget)");
})().catch(error => { console.error(error); process.exitCode = 1; });
