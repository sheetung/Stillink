'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const page=fs.readFileSync('page/index.html','utf8');
const source=page.slice(page.indexOf('async function waitDisplayTask('),page.indexOf('function authenticatedImage('));
let responses=[],calls=[],progress=[],delays=[];
const ctx={Date,Promise,Error,encodeURIComponent,api:p=>p,updateWorkLoading:t=>progress.push(t),setTimeout:(fn,ms)=>{delays.push(ms);fn();},fetch:async(path,options)=>{calls.push({path,options});assert(responses.length,'unexpected polling');const r=responses.shift();return {ok:r.ok!==false,json:async()=>r,text:async()=>r.error||'bad request'};}};
vm.createContext(ctx);vm.runInContext(source,ctx);
(async()=>{
 responses=[{task_id:12},{task_status:'queued'},{task_status:'running'},{task_status:'completed'}];await ctx.pullServiceFrame('/v1/todo/card.bin?page=0');
 assert.equal(calls[0].path,'/api/display/pull');assert.deepEqual(JSON.parse(calls[0].options.body),{path:'/v1/todo/card.bin?page=0'});assert.equal(calls.length,4);assert(calls.slice(1).every(c=>c.path==='/api/display?task_id=12'));assert.equal(progress.length,2);assert.deepEqual(delays,[1000,1000]);
 responses=[{task_id:13},{task_status:'failed',task_error:'CRC mismatch'}];await assert.rejects(ctx.pullServiceFrame('/v1/calendar/calendar.bin'),/CRC mismatch/);
 responses=[{task_id:14},{task_status:'superseded'}];await assert.rejects(ctx.pullServiceFrame('/v1/calendar/calendar.bin'),/替代/);
 responses=[{ok:false,error:'Firmware upgrade in progress'}];await assert.rejects(ctx.pullServiceFrame('/v1/calendar/calendar.bin'),/Firmware upgrade/);assert.equal(responses.length,0);
 console.log('DISPLAY TASK UI: PASS (path-only submission, scoped polling, queue/run/completion, errors, superseded work, rejected submission)');
})().catch(e=>{console.error(e);process.exitCode=1;});
