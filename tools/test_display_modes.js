"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const page=fs.readFileSync(path.join(__dirname,"../page/index.html"),"utf8");
const elements={};
for(const card of ["weather","music","album"]){
  for(const suffix of ["Enabled","Status","Refresh","Message"])
    elements[card+suffix]={checked:true,disabled:false,textContent:"",classList:{remove(){}}};
}
const context={window:{},$:id=>elements[id],hideWorkLoading(){},barEl:{style:{}},msg(){},goEl:{},img:null,curObjs:()=>[{}],MODE:1};
vm.createContext(context);
vm.runInContext(page.slice(page.indexOf("var displayModeRevision="),page.indexOf("/* ---------- 天气卡片：")),context);
context.notifyDisplayMode("weather");
assert.equal(elements.weatherEnabled.checked,true);
assert.equal(elements.musicEnabled.checked,false);
assert.equal(elements.musicRefresh.disabled,true);
elements.musicEnabled.checked=true;
context.notifyDisplayMode("music");
assert.equal(elements.weatherEnabled.checked,false);
assert.equal(elements.musicEnabled.checked,true);
elements.albumEnabled.checked=true;
context.notifyDisplayMode("album");
assert.equal(elements.albumEnabled.checked,true);
assert.equal(elements.musicEnabled.checked,false);
elements.musicEnabled.checked=true;
// Run the actual upload completion callback: a rejected upload must leave modes enabled.
const callback=page.slice(page.indexOf("  xhr.onload = function(){",page.indexOf("function upload(")),page.indexOf("  xhr.onerror = function(){",page.indexOf("function upload(")));
context.xhr={status:400,responseText:"bad CRC"};
vm.runInContext(callback,context);
elements.weatherEnabled.checked=true;
context.xhr.onload();
assert.equal(elements.weatherEnabled.checked,true);
assert.equal(elements.musicEnabled.checked,true);
context.xhr.status=200;
context.xhr.onload();
assert.equal(elements.weatherEnabled.checked,false);
assert.equal(elements.musicEnabled.checked,false);
assert.equal(elements.albumEnabled.checked,false);
assert.equal(elements.weatherRefresh.disabled,true);
assert.match(elements.musicMessage.textContent,/配置保留/);
(async()=>{
  let finish;
  context.showWorkLoading=()=>{};
  context.waitDisplayTask=id=>{assert.equal(id,"17");return new Promise(resolve=>{finish=resolve;});};
  context.xhr.getResponseHeader=()=>"17";
  context.goEl.disabled=true;
  context.xhr.onload();
  assert.equal(context.goEl.disabled,true,"keep upload disabled until actual screen completion");
  finish();
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(context.goEl.disabled,false,"release upload after task completion");
  console.log("DISPLAY MODES: PASS (exclusive modes, rejected upload preserves modes, manual refresh waits for completion)");
})().catch(error=>{console.error(error);process.exitCode=1;});
