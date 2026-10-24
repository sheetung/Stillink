"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync('page/index.html','utf8');
const scripts=[...page.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
for(const script of scripts)new vm.Script(script);
const main=scripts.find(s=>s.includes('function clearImg()'));
const controls={};
for(const id of ['filerow','thumb','fname','go'])controls[id]={style:{},removeAttribute(name){delete this[name]}};
const objects=[{text:'Keep this note'}]; let invalidated=0,revoked='',scheduled=0;
const context=vm.createContext({img:{},imgUrl:'blob:test',curRot:90,cfg:{cropRot:90,objsImg:objects,objsNote:[]},previewOK:true,MODE:0,
 $:id=>controls[id],goEl:controls.go,URL:{revokeObjectURL:u=>revoked=u},dropEl:{classList:{remove(){}},style:{},querySelector:()=>({textContent:''})},
 syncRotUI(){},destroyCrop(){},cropInvalid(){invalidated++},setMode(){},msg(){},saveCfg(){},schedulePreview(){scheduled++},updateSteps(){}});
vm.runInContext(main.slice(main.indexOf('function clearImg()'),main.indexOf('$("rmImg").addEventListener')),context);
context.clearImg();
assert.equal(context.img,null);assert.equal(revoked,'blob:test');assert.equal(context.curRot,0);assert.equal(context.cfg.cropRot,0);
assert.equal(controls.filerow.style.display,'none');assert.equal(controls.go.disabled,true);assert.equal(context.cfg.objsImg,objects,'removing a photo retains the editable text');assert.equal(invalidated,1);assert.equal(scheduled,1);
context.clearImg();assert.equal(invalidated,1,'removal is harmless with no image');
vm.runInContext(main.slice(main.indexOf('function curObjs()'),main.indexOf('function newObj()')),context);
assert.equal(context.curObjs(),objects,'text-only and photo composition use the same object list');
const colors=[];
const render=vm.createContext({img:null,PAL:[[255,255,255],[0,0,0],[255,255,0],[255,0,0]],C_W:0,C_K:1,C_Y:2,C_R:3,clamp:(n,a,b)=>Math.max(a,Math.min(b,n)),cfg:{textBg:0}});
vm.runInContext(main.slice(main.indexOf('function drawBase('),main.indexOf('function renderTo(')),render);
for(let i=0;i<4;i++){render.cfg.textBg=i;const ctx={fillStyle:'',fillRect(){colors.push(this.fillStyle)}};render.drawBase(ctx,552,768);}
assert.deepEqual(colors,['rgb(255,255,255)','rgb(0,0,0)','rgb(255,255,0)','rgb(255,0,0)']);
console.log('CREATIVE EDITOR: PASS (all inline scripts parse, photo removal retains text, resets crop, empty removal, shared text objects, four background colors)');
