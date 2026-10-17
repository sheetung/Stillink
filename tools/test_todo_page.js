"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const page=fs.readFileSync(require("node:path").join(__dirname,"../page/index.html"),"utf8");
const source=page.slice(page.indexOf("/* ---------- 待办："),page.lastIndexOf("</script>"));
function el(){return {value:"",disabled:false,hidden:false,children:[],style:{},events:{},classList:{add(){},remove(){}},setAttribute(k,v){this[k]=v;},appendChild(v){this.children.push(v);},replaceChildren(...v){this.children=v;},addEventListener(k,f){this.events[k]=f;},set src(v){this.url=v;if(this.onload)this.onload();}};}
const elements=Object.fromEntries([...page.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],el()])),notifications=[],loads=[],requests=[];
let items=[{id:"one",text:"<script>read a book</script>",done:false},{id:"two",text:"Take a walk",done:false}],failMutation=false,xhr;
const frame=new Uint8Array([165,90,1]).buffer;
const context={PANEL:{w:768,h:552,id:0},deviceToken:"token",notifyDisplayMode:m=>notifications.push(m),api:p=>p,serviceProxy:p=>p,$:id=>elements[id],JSON,Date,Math,Number,URL:{createObjectURL:()=>"blob:preview",revokeObjectURL(){}},setTimeout:()=>1,clearTimeout(){},confirm:()=>true,showWorkLoading:(text,nonBlocking)=>loads.push(nonBlocking),hideWorkLoading(){},updateWorkLoading(){},
 document:{createElement:()=>el(),querySelectorAll:()=>[]},
 fetch:async(route,options)=>{
  requests.push({route,options});let data={};
  if(route.includes("/card.bin"))return {ok:true,arrayBuffer:async()=>frame};
  if(route.includes("/card.png"))return {ok:true,blob:async()=>({})};
  if(options){
   if(failMutation)return {ok:false,json:async()=>({error:"storage offline"})};
   const payload=options.body&&JSON.parse(options.body);
   if(route==="/v1/todo/items")items.push({id:"new",text:payload.text,done:false});
   else if(options.method==="DELETE")items=items.filter(i=>i.id!==route.split("/").pop());
   else items.find(i=>i.id===route.split("/").pop()).done=payload.done;
  }
  data={items:JSON.parse(JSON.stringify(items)),page_size:8};return {ok:true,json:async()=>data};
 },
 XMLHttpRequest:class{constructor(){xhr=this;this.headers={};this.upload={};}open(method,url){this.url=url;}setRequestHeader(k,v){this.headers[k]=v;}send(body){this.body=body;}}
};
vm.runInNewContext(source,context);
const flush=()=>new Promise(setImmediate),click=id=>elements[id].events.click();
(async()=>{
 click("modeTodo");await flush();assert.equal(elements.todoPane.hidden,false);assert.equal(elements.todoList.children.length,2);assert.equal(elements.todoUpload.disabled,false);
 assert.equal(elements.todoList.children[0].children[0].children[1].textContent,"<script>read a book</script>","User content is plain text");
 elements.todoText.value="Buy milk";elements.todoForm.events.submit({preventDefault(){}});await flush();assert.equal(elements.todoList.children.length,3);assert.equal(elements.todoText.value,"");
 let check=elements.todoList.children[0].children[0].children[0];check.checked=true;check.events.change();await flush();assert.equal(items[0].done,true);assert.deepEqual(notifications,[],"Saving/checking tasks does not change device display");
 failMutation=true;check=elements.todoList.children[1].children[0].children[0];check.checked=true;check.events.change();await flush();assert.equal(elements.todoList.children[1].children[0].children[0].checked,false);assert.match(elements.todoMessage.textContent,/storage offline/);failMutation=false;
 await click("todoUpload");assert.equal(xhr.body,frame);assert.equal(xhr.headers.Authorization,"Bearer token");assert.equal(loads[0],true,"Upload uses nonblocking feedback");
 xhr.status=500;xhr.responseText="busy";xhr.onload();assert.deepEqual(notifications,[],"Failed uploads preserve automatic cards");assert.equal(elements.todoUpload.disabled,false);
 await click("todoUpload");xhr.status=200;xhr.onload();assert.deepEqual(notifications,["manual"]);
 elements.todoList.children[0].children[1].events.click();await flush();assert.equal(elements.todoList.children.length,2);
 for(let i=0;i<8;i++)items.push({id:"extra"+i,text:"Task "+i,done:false});click("todoReload");await flush();assert.equal(elements.todoNext.disabled,false);click("todoNext");await flush();assert.match(elements.todoPageLabel.textContent,/2/);assert(requests.some(r=>r.route.includes("page=1")));
 console.log("TODO PAGE: PASS (shared list, plain text, add/complete/delete, failure rollback, pagination, nonblocking upload and display exclusivity)");
})().catch(e=>{console.error(e);process.exitCode=1;});
