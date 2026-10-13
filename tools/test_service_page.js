const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync(require('node:path').join(__dirname,'../page/index.html'),'utf8');
const source=page.slice(page.indexOf('/* ---------- 额外服务支持：'),page.indexOf('/* ---------- 天气卡片：'));
const elements=Object.fromEntries(['supportService','saveSupportService','supportServiceMessage','checkSupportService','supportWeatherStatus','supportMusicStatus','modeWeather','modeMusic'].map(id=>[id,{value:'',hidden:true,events:{},addEventListener(k,f){this.events[k]=f;}}]));
const states=[{enabled:true,service_url:'http://server:8787'},{enabled:false,service_url:'http://server:8787'}],posts=[];
let healthError=false;
const context={AbortController,setTimeout,clearTimeout,document:{querySelector:()=>({click(){}})},$:id=>elements[id],api:p=>p,TextEncoder,Promise,JSON,fetch:async(path,options)=>{
  if(path.startsWith('http')){if(healthError)throw new Error('offline');return {ok:true,json:async()=>({status:'ok',services:{weather:{available:true},music:{available:true}}})};}
  const i=path.includes('music')?1:0;
  if(options){const body=JSON.parse(options.body);posts.push(body);Object.assign(states[i],body);}
  return {ok:true,json:async()=>({...states[i]})};
}};
vm.runInNewContext(source,context);
(async()=>{
  await new Promise(setImmediate);
  assert.equal(elements.supportService.value,'http://server:8787');assert.equal(elements.modeMusic.hidden,false);
  elements.supportService.value='http://new-server:8787/';await elements.saveSupportService.events.click();
  assert.equal(states[0].service_url,states[1].service_url);assert.equal(states[0].enabled,true);assert.equal(states[1].enabled,false);
  assert.equal(elements.supportWeatherStatus.textContent,'天气：可用');assert.equal(elements.supportMusicStatus.textContent,'音乐：可用');
  healthError=true;elements.supportService.value='http://offline';await elements.saveSupportService.events.click();assert.equal(posts.length,2);assert.equal(elements.supportMusicStatus.textContent,'音乐：检测失败');healthError=false;
  elements.supportService.value='bad';await elements.saveSupportService.events.click();assert.equal(posts.length,2);
  elements.supportService.value='';await elements.saveSupportService.events.click();
  assert.equal(states[0].enabled,false);assert.equal(elements.modeWeather.hidden,true);assert.equal(elements.modeMusic.hidden,true);
  assert(!page.includes('id="weatherService"'));assert(!page.includes('id="musicService"'));
  console.log('SERVICE PAGE: PASS (shared save, enabled state, validation, clearing and conditional entry)');
})().catch(e=>{console.error(e);process.exitCode=1;});
