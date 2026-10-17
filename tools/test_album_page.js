"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const page=fs.readFileSync(path.join(__dirname,"../page/index.html"),"utf8");
const source=page.slice(page.indexOf("/* ---------- 相簿："),page.lastIndexOf("</script>"));
function el(){return {value:"",checked:false,hidden:false,disabled:false,textContent:"",style:{},children:[],events:{},classList:{toggle(){},remove(){},add(){}},addEventListener(k,f){this.events[k]=f;},replaceChildren(...v){this.children=v;},appendChild(v){this.children.push(v);}};}
const elements=Object.fromEntries([...page.matchAll(/id="([^" ]+)"/g)].map(m=>[m[1],el()]));
const photos=[{id:"a".repeat(32),name:"<测试照片>",width:768,height:552},{id:"b".repeat(32),name:"照片二",width:768,height:552}];
let state={enabled:false,playlist:"",mode:"sequence",interval:1800,current:"",status:"disabled"},selection=[],failSave=false;
const posts=[],notifications=[],packed=new Uint8Array([165,90,1]),modes=[el(),el()];
const context={serviceProxy:p=>"http://server"+p,authenticatedImage:(image,url)=>{image.src=url;},supportServiceURL:"http://server",displayModeRevision:0,notifyDisplayMode:m=>notifications.push(m),$:id=>elements[id],api:p=>p,Set,Number,Date,JSON,Promise,Array,encodeURIComponent,confirm:()=>true,setTimeout:()=>1,clearTimeout(){},MODE:0,img:{},previewOK:true,fileEl:{files:[{name:"photo.jpg"}]},packFull:()=>packed,frameWithHeader:p=>p,showWorkLoading(){},hideWorkLoading(){},afterLoadingPaint:f=>f(),document:{querySelectorAll:()=>modes,createElement:()=>el()},fetch:async(url,options)=>{
 let data={};let ok=true;const body=options?.body;
 if(url.startsWith("/api/album")){
   if(options?.method==="POST"){posts.push({url,body});if(failSave){ok=false;data={error:"NVS error"};}else{Object.assign(state,JSON.parse(body));data={saved:true};}}
   else data={...state};
 }else if(url.includes("/playlists")){
   if(options){selection=JSON.parse(body).photos;data={id:"c".repeat(32)};}else data={photos:selection};
 }else if(options?.method==="POST"){posts.push({url,body});data={id:"d".repeat(32)};}
 else data={photos};
 return {ok,status:ok?200:500,json:async()=>data};
}};
vm.runInNewContext(source,context);
const click=id=>elements[id].events.click(),flush=()=>new Promise(setImmediate);
(async()=>{
 click("modeAlbum");await flush();assert.equal(elements.albumPane.hidden,false);assert.equal(elements.albumPhotos.children.length,2);
 assert.equal(elements.albumScreen.children[0].src,"http://server/v1/album/photos/"+photos[0].id+".png","Opening the album previews the first photo without a click");
 elements.albumPhotos.children[1].children[0].events.click();click("albumReload");await flush();
 assert.equal(elements.albumScreen.children[0].src,"http://server/v1/album/photos/"+photos[1].id+".png","Reloading preserves the selected preview");
 state.enabled=true;state.current=photos[0].id;click("modeAlbum");await flush();
 assert.equal(elements.albumScreen.children[0].src,"http://server/v1/album/photos/"+state.current+".png","Opening an active album previews the photo displayed by the device");
 state.enabled=false;state.current="";
 assert.match(elements.albumPhotos.children[0].children[1].children[1].textContent,/ · aaaaaaaa$/);
 assert(!elements.albumPhotos.children[0].children[1].children[1].textContent.includes("测试照片"));
 elements.albumEnabled.checked=true;click("albumSave");await flush();assert.equal(posts.length,0);assert.match(elements.albumMessage.textContent,/至少/);
 click("albumSelectAll");elements.albumInterval.value="0";click("albumSave");await flush();assert.equal(posts.length,0);
 elements.albumInterval.value="30";elements.albumMode.value="random";await click("albumSave");await flush();
 assert.deepEqual(selection,photos.map(p=>p.id));assert.equal(state.interval,1800);assert.equal(state.mode,"random");assert.equal(state.playlist,"c".repeat(32));assert.deepEqual(notifications,["album"]);
 failSave=true;await click("albumSave");await flush();assert.deepEqual(notifications,["album"]);assert.match(elements.albumMessage.textContent,/保存失败/);failSave=false;
 elements.albumEnabled.checked=false;await click("albumSave");await flush();assert.deepEqual(notifications,["album"],"Stopping slideshow must not restart another card");
 await click("saveAlbumPhoto");await flush();const upload=posts.find(p=>p.url.startsWith("http"));assert.equal(upload.body,packed);assert.equal(upload.url,"http://server/v1/album/photos");assert.deepEqual(notifications,["album"],"Saving a photo does not change device display mode");assert.match(elements.albumUploadMessage.textContent,/已保存/);
 console.log("ALBUM PAGE: PASS (gallery, safe title, selection, interval validation, save failure, exclusive enable, processed frame upload)");
})().catch(e=>{console.error(e);process.exitCode=1;});
