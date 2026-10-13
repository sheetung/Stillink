const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync('page/index.html','utf8');
const source=page.split('/* ---------- GitHub Release 在线升级 ---------- */')[1].split('/* ---------- 统一升级入口')[0];
const nodes=new Map(),requests=[];
const $=id=>{if(!nodes.has(id))nodes.set(id,{disabled:false,hidden:true,value:0,textContent:'',events:{},addEventListener(name,fn){this.events[name]=fn;}});return nodes.get(id);};
let state={current:'V1.2.2',chip:'esp32s3',connected:false,status:'idle',latest:'',update_available:false,size:0,received:0,error:''},accept=true;
vm.runInNewContext(source,{$,LOCAL_PREVIEW:false,api:p=>'http://192.168.31.14'+p,setTimeout(){return 1;},clearTimeout(){},confirm:()=>accept,fetch:async(url,options)=>{requests.push({url,options});return {ok:true,json:async()=>({...state})};}});
const flush=()=>new Promise(setImmediate);
(async()=>{
 await flush();assert($('onlineOtaCheck').disabled);assert($('onlineOtaInstall').disabled);
 state.connected=true;await $('gearBtn').events.click();assert(!$('onlineOtaCheck').disabled);assert($('onlineOtaInstall').disabled);
 state={...state,status:'ready',latest:'v1.2.3',update_available:true,size:1000};await $('gearBtn').events.click();assert(!$('onlineOtaInstall').disabled);
 accept=false;let count=requests.length;await $('onlineOtaInstall').events.click();await flush();assert.equal(requests.length,count);
 accept=true;state={...state,status:'downloading',received:500};await $('onlineOtaInstall').events.click();await flush();assert.equal(requests.at(-1).url,'http://192.168.31.14/api/ota/online/install');assert.equal(requests.at(-1).options.method,'POST');assert.equal($('onlineOtaProgress').value,50);assert($('upGo').disabled);
 state={...state,status:'error',error:'GitHub HTTP 403'};await $('gearBtn').events.click();assert.match($('onlineOtaMessage').textContent,/403/);assert(!$('upGo').disabled);assert($('onlineOtaInstall').disabled);
 state={...state,status:'ready',latest:'v1.2.2',update_available:false};await $('gearBtn').events.click();assert.match($('onlineOtaMessage').textContent,/已是最新/);assert($('onlineOtaInstall').disabled);
 state.status='rebooting';await $('gearBtn').events.click();assert.match($('onlineOtaMessage').textContent,/校验通过/);
 console.log('ONLINE OTA UI: PASS (offline, LAN target, confirmation, progress, error, latest, reboot)');
})().catch(e=>{console.error(e);process.exitCode=1;});
