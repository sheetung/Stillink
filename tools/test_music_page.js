"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const page=fs.readFileSync(require("node:path").join(__dirname,"../page/index.html"),"utf8");
const source=page.slice(page.indexOf("/* ---------- 音乐卡片："),page.indexOf("/* ---------- 相簿："));
function el(){return {value:"",checked:false,hidden:false,disabled:false,textContent:"",style:{},events:{},children:[],classList:{add(){},remove(){},toggle(){}},addEventListener(k,f){this.events[k]=f;},replaceChildren(...v){this.children=v;}};}
const ids=[...page.matchAll(/id="(music[^" ]*|modeMusic|modeWeather|modeAlbum|modeTodo|modeBoard|editorGrid|steps)"/g)].map(m=>m[1]);
const elements=Object.fromEntries(ids.map(id=>[id,el()])),modes=[el(),el()],queue=[],requests=[];
const respond=body=>queue.push(body),flush=()=>new Promise(setImmediate),click=id=>elements[id].events.click();
const notifications=[];
const context={serviceProxy:p=>"http://server"+p,authenticatedImage:(image,url)=>{image.src=url;},displayModeRevision:0,notifyDisplayMode:mode=>notifications.push(mode),supportServiceURL:"",$:id=>elements[id],api:p=>p,TextEncoder,Date,JSON,Promise,setTimeout:()=>1,clearTimeout(){},
 document:{querySelectorAll:()=>modes,createElement:()=>el()},fetch:async(url,options)=>{requests.push({url,options});assert(queue.length,url);return {ok:true,json:async()=>queue.shift()};}};
vm.runInNewContext(source,context);
(async()=>{
 respond({enabled:false,service_url:"",status:"disabled"});click("modeMusic");await flush();
 assert.equal(elements.musicPane.hidden,false);assert.equal(elements.editorGrid.style.display,"none");
 context.supportServiceURL="bad";elements.musicEnabled.checked=true;const before=requests.length;click("musicSave");await flush();assert.equal(requests.length,before);
 context.supportServiceURL="http://192.168.31.12:8787/";
 respond({saved:true});respond({enabled:true,service_url:"http://192.168.31.12:8787",status:"ready"});respond({playing:true,title:"<歌曲>",artist:"歌手",version:"abc"});click("musicSave");await flush();await flush();
 const post=requests.find(r=>r.options?.method==="POST");assert.deepEqual(JSON.parse(post.options.body),{enabled:true,service_url:"http://192.168.31.12:8787"});
 assert.equal(elements.musicScreen.children[0].alt,"<歌曲>专辑卡片");assert.equal(elements.musicScreen.children[0].src,"http://server/v1/music.png?v=abc");
 elements.modeWeather.events.click();assert.equal(elements.musicPane.hidden,true);
 elements.musicEnabled.checked=false;respond({saved:true});click("musicSave");await flush();
 assert.deepEqual(notifications,["music"],"Disabling music must not reenable weather");
 assert.match(elements.musicMessage.textContent,/已关闭/);
 console.log("MUSIC PAGE: PASS (entry, address validation, save, server preview, safe metadata, mode switch)");
})().catch(e=>{console.error(e);process.exitCode=1;});
