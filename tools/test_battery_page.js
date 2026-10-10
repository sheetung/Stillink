"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const page = fs.readFileSync(path.join(__dirname, "../page/index.html"), "utf8");
const block = page.split("/* ---------- 电池校准 ---------- */")[1].split("/* ---------- 电池校准结束 ---------- */")[0];
const elements = Object.fromEntries(["saveBatteryCal", "resetBatteryCal", "batActual", "batteryCalFactor"].map(id => [id, {value:"", disabled:false, addEventListener(event, callback){ this[event]=callback; }}]));
let responses=[], writes=[], message="", refreshed=0;
const context=vm.createContext({$:id=>elements[id], Number, Math, Date, Promise, LOCAL_PREVIEW:false, api:p=>p,
  saveState:(id,kind,text)=>{message=text;}, refreshInfo:()=>{refreshed++;}, loadSettings:()=>{},
  postSettings:(fields,done)=>{writes.push(fields); done();},
  fetch:async()=>({ok:true,json:async()=>responses.shift()})});
vm.runInContext(block,context);
const flush=async()=>{for(let i=0;i<4;i++) await new Promise(resolve=>setImmediate(resolve));};
(async()=>{
  assert.equal(context.batteryCalibrationPpm(4.1,3900,1000000),1051282);
  assert.equal(context.batteryCalibrationPpm(4.1,4100,1051282),1051282,"repeat calibration keeps prior gain");
  assert.throws(()=>context.batteryCalibrationPpm(4.1,-1,1000000),/尚未检测到电池/);
  assert.throws(()=>context.batteryCalibrationPpm(0,3900,1000000),/实际电压/);
  assert.throws(()=>context.batteryCalibrationPpm(4.1,3900,undefined),/升级/);
  assert.throws(()=>context.batteryCalibrationPpm(4.1,1000,1000000),/偏差过大/);
  context.renderBatteryCalibration({}); assert.equal(elements.saveBatteryCal.disabled,true);
  context.renderBatteryCalibration({bat_cal_ppm:1051282}); assert.equal(elements.saveBatteryCal.disabled,false);
  assert(elements.batteryCalFactor.textContent.includes("1.0513"));
  elements.batActual.value="4.10";
  responses=[{bat_mv:3900},{bat_cal_ppm:1000000}];
  elements.saveBatteryCal.click(); await flush();
  assert.equal(writes[0].bat_cal_ppm,1051282); assert.equal(refreshed,1);
  responses=[{bat_mv:-1},{bat_cal_ppm:1000000}];
  elements.saveBatteryCal.click(); await flush();
  assert.equal(writes.length,1); assert(message.includes("尚未检测到电池"));
  elements.resetBatteryCal.click(); assert.equal(writes[1].bat_cal_ppm,1000000);
  console.log("BATTERY CALIBRATION: PASS (gain, repeated calibration, invalid voltage, missing battery, old firmware, save, reset)");
})().catch(error=>{console.error(error);process.exitCode=1;});
