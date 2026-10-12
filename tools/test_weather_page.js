"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs"), path = require("node:path"), vm = require("node:vm");
const page = fs.readFileSync(path.join(__dirname, "../page/index.html"), "utf8");
const source = page.slice(page.indexOf("/* ---------- 天气卡片："), page.indexOf("/* ---------- 音乐卡片："));
const elements = {}, requests = [], responses = [], timers = [];
function element(id) {
  return {id, value:"", checked:false, disabled:false, hidden:false, textContent:"", innerHTML:"", style:{}, children:[], events:{},
    classList:{toggle() {}, remove() {}}, setAttribute(k,v){this[k]=v;},
    addEventListener(k,f){this.events[k]=f;}, replaceChildren(...children){this.children=children;},
    appendChild(child){this.children.push(child);} };
}
for (const id of [...page.matchAll(/id="(weather[^" ]*|modeWeather|modeMusic|editorGrid|steps)"/g)].map(m=>m[1])) elements[id]=element(id);
const modes=[element("photo"),element("note")];
const state={enabled:false,name:"杭州",service_url:"http://192.168.31.12:8787",latitude:30.29,longitude:120.16,times:[480,720,1080],status:"disabled",error:""};
const context={
  $:id=>elements[id], api:url=>url, console, TextEncoder, Date, Set, Math, JSON, Array,
  setTimeout:(f,ms)=>{timers.push({f,ms});return timers.length;}, clearTimeout:()=>{},
  fetch:async(url,options)=>{requests.push({url,options});assert(responses.length,"Unexpected request: "+url);const response=responses.shift();if(response instanceof Error)throw response;return {ok:response.status<400,text:async()=>response.text||"error",json:async()=>response.body};},
  document:{querySelectorAll:()=>modes,addEventListener:()=>{},createElement:tag=>{
    if(tag!=="canvas")return element(tag);
    return {getContext:()=>({clearRect(){},fillText(){},getImageData(){const data=new Uint8ClampedArray(1024);data[3]=255;return {data};}})};
  }}
};
vm.runInNewContext(source,context);
const flush=()=>new Promise(setImmediate);
const respond=body=>responses.push({status:200,body});
const trigger=(id,event="click")=>elements[id].events[event].call(elements[id]);
(async()=>{
  respond(state);trigger("modeWeather");await flush();
  assert.equal(elements.weatherPane.hidden,false);
  assert.equal(elements.editorGrid.style.display,"none");
  assert.equal(elements.weatherMorning.value,"08:00");
  assert.equal(elements.weatherNoon.value,"12:00");
  assert.equal(elements.weatherEvening.value,"18:00");
  assert.equal(elements.weatherRefresh.disabled,true);
  elements.weatherEnabled.checked=true;trigger("weatherEnabled","change");
  elements.weatherNoon.value="07:00";
  const before=requests.length;trigger("weatherSave");await flush();
  assert.equal(requests.length,before,"Invalid schedule must not be saved");
  assert.match(elements.weatherMessage.textContent,/顺序/);
  elements.weatherNoon.value="12:00";
  respond({saved:true});respond({...state,enabled:true,status:"waiting"});
  trigger("weatherSave");await flush();
  const saved=JSON.parse(requests.find(r=>r.options?.method==="POST").options.body);
  assert.deepEqual(saved.times,[480,720,1080]);
  assert.equal(saved.name,"杭州");assert.equal(saved.enabled,true);
  assert.equal(saved.service_url,state.service_url);
  assert.equal(saved.glyph_codes,undefined);
  assert.equal(saved.glyph_bits,undefined);
  assert.equal(elements.weatherSave.disabled,false);
  elements.weatherLocation.value="北京";
  trigger("weatherLocation","input");
  respond({results:[{name:"北京",latitude:39.9,longitude:116.4,admin1:"北京",country:"中国"}]});
  timers.filter(t=>t.ms===350).at(-1).f();await flush();
  assert.equal(elements.weatherCities.children[0].textContent,"北京");
  elements.weatherCities.children[0].events.click();
  assert.equal(elements.weatherLocation.value,"北京");
  const date=Date.UTC(2026,9,12), data={temperature:22,humidity:65,wind:2,code:0,rain_probability:10,updated:date,utc_offset:28800,
    days:Array.from({length:4},(_,i)=>({low:17,high:27,code:1,date:date+i*86400000/1000}))};
  data.updated=date/1000;
  data.days.forEach((d,i)=>d.date=date/1000+i*86400);
  modes[0].events.click();respond({...state,name:"<杭州 &>",enabled:true,status:"ready",data});trigger("modeWeather");await flush();
  assert.equal(elements.weatherScreen.children[0].alt,"<杭州 &>天气卡片");
  assert.match(elements.weatherScreen.children[0].src,/%3C/);
  modes[0].events.click();respond({...state,enabled:true,status:"waiting_wifi"});trigger("modeWeather");await flush();
  assert.equal(elements.weatherRefresh.disabled,true);
  assert.match(elements.weatherStatus.textContent,/Wi-Fi/);
  console.log("WEATHER PAGE: PASS (schedule validation, device save, service URL, city search, safe image preview, offline state)");
})().catch(e=>{console.error(e);process.exitCode=1;});
