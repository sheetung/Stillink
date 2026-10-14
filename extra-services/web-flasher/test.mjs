import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {webcrypto,createHash} from 'node:crypto';
const source=(await readFile(new URL('app.js',import.meta.url),'utf8')).replace(/^import .*;\r?\n/gm,'');
const data=new Uint8Array([0xe9,1,2,3]),writes=[];
const image={file:'stillink-v1.2.1-esp32s3-ota.bin',url:'firmware/test.bin',size:data.length,sha256:createHash('sha256').update(data).digest('hex')};
const manifest={releases:[{version:'v1.2.1',chip:'esp32s3',ota:image,full:{...image,file:'stillink-v1.2.1-esp32s3.bin'}},{version:'v1.2.1',chip:'esp32c3',ota:{...image,file:'stillink-v1.2.1-esp32c3-ota.bin'},full:{...image,file:'stillink-v1.2.1-esp32c3.bin'}}]};
const nodes=new Map();
function node(id){if(!nodes.has(id))nodes.set(id,{textContent:'',value:id==='baud'?'460800':id==='address'?'0x0':'',checked:false,files:[],classList:{toggle(){},add(){},remove(){}},setAttribute(){},append(){},addEventListener(event,fn){this[event]=fn;}});return nodes.get(id);}
node('firmwareManifest').textContent=JSON.stringify(manifest);
let chip='ESP32-S3',badDownload=false;
class Transport{disconnect(){return Promise.resolve();}getInfo(){return 'USB test';}}
class ESPLoader{constructor(){this.chip={CHIP_NAME:chip};}async main(){}async writeFlash(options){writes.push(options);options.reportProgress(0,4,4);assert.equal(options.calculateMD5Hash(data),createHash('md5').update(data).digest('hex'));}async after(){}}
const context={document:{getElementById:node,createElement:()=>node('code')},window:{isSecureContext:true},navigator:{serial:{requestPort:async()=>({}),addEventListener(){}}},location:{hostname:'localhost'},ESPLoader,Transport,SparkMD5:{ArrayBuffer:{hash:b=>createHash('md5').update(new Uint8Array(b)).digest('hex')}},crypto:webcrypto,Uint8Array,fetch:async()=>({ok:true,arrayBuffer:async()=>badDownload?new Uint8Array([0]).buffer:data.buffer})};
vm.runInNewContext(source,context);
await node('connect').click();await node('flash').click();
assert.match(node('status').textContent,/重新通电/);
assert.equal(writes.length,1);assert.equal(writes[0].fileArray[0].address,0x10000);assert.equal(writes[0].fileArray[1].address,0xd000);assert.equal(writes[0].fileArray[1].data.length,8192);assert(writes[0].fileArray[1].data.every(byte=>byte===255));assert.equal(writes[0].eraseAll,false);assert.equal(node('percent').textContent,'100%');
node('imageType').value='full';await node('imageType').change();await node('connect').click();await node('flash').click();assert.equal(writes.length,2);assert.equal(writes[1].fileArray[0].address,0);assert.equal(writes[1].fileArray.length,1);
chip='ESP32-C3';await node('connect').click();await node('flash').click();assert.equal(writes.length,2);assert.match(node('status').textContent,/仅支持 ESP32-S3/);
chip='ESP32-S3';badDownload=true;await node('connect').click();await node('flash').click();assert.equal(writes.length,2);assert.match(node('status').textContent,/校验失败/);
badDownload=false;await node('chooseCustom').click();await node('connect').click();node('customFile').files=[{arrayBuffer:async()=>data.buffer}];node('address').value='0x10000';await node('flash').click();assert.equal(writes.length,3);assert.equal(writes[2].fileArray[0].address,0x10000);assert.equal(writes[2].flashSize,'keep');
await node('connect').click();node('address').value='invalid';await node('flash').click();assert.equal(writes.length,3);assert.match(node('status').textContent,/写入地址/);
console.log('FLASHER: PASS (default flash, chip guard, integrity, custom address, validation)');
await node('chooseStillink').click();node('release').value='1';node('imageType').value='ota';await node('release').change();
chip='ESP32-C3';await node('connect').click();await node('flash').click();assert.equal(writes.length,4);assert.equal(writes[3].fileArray[0].address,0x10000);assert.equal(node('firmwareChip').textContent,'ESP32-C3');
chip='ESP32-S3';await node('connect').click();await node('flash').click();assert.equal(writes.length,4);assert.match(node('status').textContent,/仅支持 ESP32-C3/);
console.log('C3 selection and wrong-chip rejection: PASS');
