const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync('page/index.html','utf8'),source=page.split('var infoInFlight=null')[1].split('/* ---------- 设备连接心跳结束 ---------- */')[0];
const nodes=new Map(),timers=new Map(),events={},winEvents={};let id=0,requests=0,reply=null;
function $(name){if(!nodes.has(name))nodes.set(name,{textContent:'',style:{},classList:{toggle(k,v){this[k]=v;}}});return nodes.get(name);}
const document={hidden:false,addEventListener(n,fn){events[n]=fn;}},window={addEventListener(n,fn){winEvents[n]=fn;}};
let nextInfo={ver:'V2.0.1',fw:'V2.0.1',api:2,ssid:'Stillink-example',panel:'A0'},status=200;
const ctx={$,document,window,LOCAL_PREVIEW:false,api:p=>p,DEV_FW:'',DEV_API:2,PANEL:{},A1MODE:1,A1MODE_MAX:4,clamp:()=>1,verNum:()=>0,applyPanelGeom:()=>false,syncPanelRows(){},AbortController,Promise,
 setTimeout(fn,ms){timers.set(++id,{fn,ms});return id;},clearTimeout(n){timers.delete(n);},fetch:(url,options)=>{requests++;assert.equal(options.cache,'no-store');return new Promise((resolve,reject)=>{reply=()=>resolve({ok:status===200,status,json:async()=>nextInfo});options.signal.addEventListener('abort',()=>reject(new Error('aborted')));});}};
vm.runInNewContext('var infoInFlight=null'+source,ctx);
const flush=async()=>{for(let i=0;i<4;i++)await new Promise(setImmediate);},fire=ms=>{const found=[...timers].find(([,t])=>t.ms===ms);assert(found,'missing timer '+ms);timers.delete(found[0]);found[1].fn();};
(async()=>{
 assert.equal(requests,1);ctx.refreshInfo();assert.equal(requests,1,'no overlapping probes');reply();await flush();assert.equal($('conn').classList.ok,true);
 fire(10000);fire(4000);await flush();assert.equal($('conn').textContent,'未连接');assert.equal($('conn').classList.ok,false);
 fire(10000);reply();await flush();assert.equal($('conn').classList.ok,true,'recovers');
 document.hidden=true;events.visibilitychange();await flush();assert.equal(timers.size,0,'hidden tab stops polling');
 document.hidden=false;events.visibilitychange();assert.equal(requests,4);status=401;reply();await flush();assert.equal($('conn').textContent,'需要登录');assert.equal($('conn').classList.ok,false);
 fire(10000);status=500;reply();await flush();assert.equal($('conn').textContent,'未连接');
 fire(10000);document.hidden=true;events.visibilitychange();document.hidden=false;events.visibilitychange();await flush();assert.equal([...timers.values()].filter(t=>t.ms===10000).length,1,'one loop after rapid hide/show');
 console.log('CONNECTION HEARTBEAT: PASS (live status, timeout clears green dot, recovery, HTTP errors, login state, hidden pause/resume, no overlapping probes or duplicate loops)');
})().catch(e=>{console.error(e);process.exitCode=1;});
