"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync(require('node:path').join(__dirname,'../page/index.html'),'utf8');
const source=[...page.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)].at(-1)[1];
new vm.Script(source);
const nodes={},calls=[],notifications=[],loading=[];let failPreview=false,failMonth=false,serial=0,daily={enabled:false,mode:'calendar',status:'disabled',path:''};
function node(id){return nodes[id]??=(function(){const n={id,value:'',checked:true,hidden:false,disabled:false,style:{},children:[],events:{},textContent:'',classList:{toggle(){},add(){},remove(){}},parentElement:{nextElementSibling:{}},elements:{title:{},date:{},yearly:{},time:{}},setAttribute(k,v){this[k]=v;},appendChild(x){this.children.push(x);},replaceChildren(...x){this.textContent='';this.children=x;},after(){},addEventListener(k,f){this.events[k]=f;},click(){this.events.click?.();return this.onclick?.();},showModal(){this.open=true;},close(){this.open=false;},reset(){}};let text='';Object.defineProperty(n,'textContent',{get(){return text;},set(v){text=v;n.children=[];}});return n;})();}
const creative=node('creative'),other=node('modeWeather');node('datesMock').hidden=true;
const document={hidden:false,getElementById:node,body:{appendChild(){}},addEventListener(){},querySelector:()=>creative,querySelectorAll:s=>s.includes(':not')?[creative,other]:[creative,other,node('annTab'),node('calTab')],createElement:tag=>{const n=node('generated-'+(++serial));n.tagName=tag;if(tag==='img')Object.defineProperty(n,'src',{set(v){this.url=v;queueMicrotask(()=>this.onload?.());}});return n;}};
const context={pullServiceFrame:async path=>calls.push({route:path,pull:true}),window:{},document,Date,Math,Number,String,Object,URLSearchParams,Promise,PANEL:{w:768,h:552,id:0},api:x=>x,serviceProxy:x=>x,URL:{createObjectURL:()=>`blob:${++serial}`,revokeObjectURL(){}},setInterval(){},confirm:()=>true,showWorkLoading:(s,n)=>loading.push(n),hideWorkLoading(){},notifyDisplayMode:m=>notifications.push(m),fetch:async(route,options)=>{
 calls.push({route,options});
 if(route==='/api/dates'){if(options){const d=JSON.parse(options.body);daily={...d,mode:d.path.includes('/anniversary.bin?')?'anniversary':'calendar',status:d.enabled?'waiting':'disabled'};}return {ok:true,json:async()=>daily};}
 if(route==='/api/weather')return {ok:true,json:async()=>({name:'杭州',latitude:30.27,longitude:120.15})};
 if(route.includes('/month?'))return {ok:!failMonth,json:async()=>failMonth?{error:'service offline'}:{anniversaries:[{id:'ann-1',title:'Birthday',date:'2026-11-01',yearly:true}],schedules:[],days:[]}};
 if(route.includes('.png?'))return {ok:!failPreview,blob:async()=>({server:true})};
 if(route.includes('.bin?'))return {ok:true,arrayBuffer:async()=>new Uint8Array([1,2,3]).buffer};
 return {ok:true,json:async()=>({saved:true})};
}};
vm.runInNewContext(source,context);
const flush=async()=>{for(let i=0;i<6;i++)await new Promise(setImmediate);};
(async()=>{
 assert.equal(calls.length,0,'inactive calendar does not fetch');
 node('calTab').click();await flush();
 assert.equal(node('datesMock').hidden,false);assert.equal(node('dm-paper').children.length,1);assert.equal(node('dm-paper').children[0].tagName,'img');assert.equal(node('dm-upload').disabled,false);
 for(const [id,param] of [['dm-showAnn','anniversaries'],['dm-showToday','today'],['dm-showCalendarWeather','weather'],['dm-lunar','lunar']]){
   node(id).checked=false;await node(id).onchange();await flush();const path=calls.filter(x=>x.route.includes('.png?')).at(-1).route;assert.equal(new URL('http://preview'+path).searchParams.get(param),'0',id+' requests a new server image');assert.equal(node('dm-paper').children.length,1);assert.equal(node('dm-paper').children[0].tagName,'img');
 }
 const png=calls.filter(x=>x.route.includes('.png?')).at(-1).route;
 await node('dm-upload').onclick();assert(calls.some(x=>x.route===png.replace('.png?','.bin?')),'upload uses the same card parameters as the displayed PNG');assert(!calls.some(x=>x.route==='/api/frame'),'browser never reuploads a service frame');assert.deepEqual(notifications,['manual']);assert.deepEqual(loading,[true]);
 const beforeSame=calls.length;await node('dm-lunar').onchange();await flush();assert.equal(calls.length,beforeSame,'unchanged render does not request a new image');
 failPreview=true;node('dm-lunar').checked=true;await node('dm-lunar').onchange();await flush();assert.equal(node('dm-upload').disabled,true);assert.equal(node('dm-paper').children.length,0);assert.match(node('dm-paper').textContent,/服务器/);assert.match(node('dm-notice').textContent,/卡片生成失败/);
 failPreview=false;node('annTab').click();await flush();assert(calls.some(x=>x.route.includes('/anniversary.png?')));assert.equal(node('dm-paper').children[0].tagName,'img');
 failMonth=true;node('calTab').click();await flush();assert.equal(node('dm-upload').disabled,true);assert.match(node('dm-notice').textContent,/service offline/);
 failMonth=false;node('calTab').click();await flush();node('dm-autoEnabled').checked=true;await node('dm-autoSave').onclick();await flush();
 const saved=calls.filter(x=>x.route==='/api/dates'&&x.options).at(-1);const dailyConfig=JSON.parse(saved.options.body);
 assert.equal(dailyConfig.enabled,true);assert(dailyConfig.path.startsWith('/v1/calendar/calendar.bin?'));
 const params=new URL('http://preview'+dailyConfig.path).searchParams;
 for(const key of ['date','month','width','height','panel'])assert.equal(params.has(key),false,'Device supplies fresh date and geometry');
 assert.equal(dailyConfig.utc_offset,-new Date().getTimezoneOffset()*60);
 assert.equal(notifications.at(-1),'calendar');
 await node('dm-autoRefresh').onclick();assert(calls.some(x=>x.route==='/api/dates/refresh'));
 context.window.onDatesDisplayMode('manual');assert.equal(node('dm-autoEnabled').checked,false);assert.equal(node('dm-autoRefresh').disabled,true);
 assert(!source.includes('setInterval(')&&!source.includes("addEventListener('visibilitychange'"),'no automatic calendar refresh');
 assert(!source.includes('CALENDAR /')&&!source.includes('贵阳')&&!source.includes('renderLocal')&&!source.includes("createElement('canvas')"),'no local card renderer or canned weather');
 console.log('CALENDAR PAGE: PASS (server-only calendar/anniversary images, every display toggle, matching upload parameters, no fallback on failure, service errors, no idle requests)');
})().catch(e=>{console.error(e);process.exitCode=1;});
