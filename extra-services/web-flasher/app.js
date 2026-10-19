import { ESPLoader, Transport } from 'esptool-js';
import SparkMD5 from 'spark-md5';
import { createTelemetry } from './telemetry.js';
import { createPublicCounters } from './counters.js';

const $=id=>document.getElementById(id);
const catalog=JSON.parse($('firmwareManifest').textContent);
function selectedFirmware(){return $('release').value===''?undefined:catalog.releases[Number($('release').value)]?.[$('imageType').value];}
let mode='stillink',transport=null,loader=null,busy=false;
const statsEndpoint=typeof __STATS_ENDPOINT__==='string'?__STATS_ENDPOINT__:'';
const refreshCounters=createPublicCounters(statsEndpoint,document);
refreshCounters();
const stats=createTelemetry(statsEndpoint,refreshCounters);
function statsChip(){return loader?.chip.CHIP_NAME==='ESP32-S3'?'esp32s3':loader?.chip.CHIP_NAME==='ESP32-C3'?'esp32c3':'unknown';}
const supported=window.isSecureContext&&'serial' in navigator;
function message(text,kind=''){ $('status').textContent=text;$('status').className='status '+kind; }
function log(text){$('log').textContent+=text;$('log').scrollTop=$('log').scrollHeight;}
function controls(){
  $('connect').disabled=busy||!!loader||(!supported&&window.isSecureContext);$('disconnect').disabled=busy||!transport;
  $('flash').disabled=busy||!loader||(mode==='stillink'&&!selectedFirmware());$('baud').disabled=busy||!!transport;
  ['chooseStillink','chooseCustom','customFile','address','erase','chip','release','imageType'].forEach(id=>$(id).disabled=busy);
  if(mode==='stillink'&&$('imageType').value==='ota'){$('erase').disabled=true;$('erase').checked=false;}
}
function select(value){mode=value;['stillink','custom'].forEach(name=>{
  $(name+'Pane').hidden=name!==mode;
  const button=$(name==='stillink'?'chooseStillink':'chooseCustom');button.classList.toggle('selected',name===mode);button.setAttribute('aria-pressed',String(name===mode));
});$('flash').textContent=mode==='stillink'?'烧录 Stillink':'烧录所选固件';controls();}
async function disconnect(){
  loader=null;
  if(transport){try{await transport.disconnect();}finally{transport=null;}}
  $('deviceDot').classList.remove('online');$('deviceName').textContent='尚未连接设备';$('deviceInfo').textContent='通过 USB 数据线连接电脑';controls();
}
function updateVersions(){
  const previous=catalog.releases[Number($('release').value)]?.version;
  const options=[];
  for(const [i,release] of catalog.releases.entries()){
    if(release.chip!==$('chip').value)continue;
    const option=document.createElement('option');option.value=String(i);option.textContent=release.version;options.push(option);
  }
  $('release').replaceChildren(...options);
  $('release').value=options.find(option=>catalog.releases[Number(option.value)].version===previous)?.value||options[0]?.value||'';
  showFirmware();
}
$('chip').value='esp32s3';
$('imageType').value='ota';
function showFirmware(){
  const release=$('release').value===''?undefined:catalog.releases[Number($('release').value)],firmware=selectedFirmware();
  $('version').textContent=release?.version||'';
  $('firmwareChip').textContent=release?.chip==='esp32c3'?'ESP32-C3':'ESP32-S3';
  $('chipBadge').textContent='推荐 · '+$('firmwareChip').textContent;
  $('firmwareName').textContent=firmware?.file||'当前没有可用的 Release 固件';
  $('firmwareSize').textContent=firmware?(firmware.size/1024/1024).toFixed(2)+' MiB':'';
  $('download').hidden=!firmware;if(firmware)$('download').href=firmware.url;
  $('imageHint').textContent=$('imageType').value==='ota'?'仅用于已安装 Stillink 且分区表一致的设备；保留启动程序、分区表和配置。':'包含启动程序、分区表与应用，从 0x0 烧录，适用于首次安装。';
  controls();
}
$('chip').addEventListener('change',updateVersions);$('release').addEventListener('change',showFirmware);$('imageType').addEventListener('change',showFirmware);updateVersions();
if(!supported){
  if(!window.isSecureContext){
    message('点击“连接并选择设备”，将在本机烧录页选择 USB 串口。');
  }else message('请使用电脑上的 Chrome 或 Edge 连接 USB 设备。','error');
}
$('chooseStillink').addEventListener('click',()=>select('stillink'));
$('chooseCustom').addEventListener('click',()=>select('custom'));
$('connect').addEventListener('click',async()=>{
  if(!window.isSecureContext){location.href='http://localhost:8788/';return;}
  const report=stats.start('connect');
  busy=true;controls();$('log').textContent='';
  try{
    const port=await navigator.serial.requestPort();
    transport=new Transport(port);
    const candidate=new ESPLoader({transport,baudrate:Number($('baud').value),terminal:{clean(){},write:log,writeLine:text=>log(text+'\n')}});
    message('正在连接并识别芯片…');$('phase').textContent='识别芯片';
    await candidate.main();loader=candidate;
    report('success','',{chip:statsChip()});
    $('deviceName').textContent=loader.chip.CHIP_NAME;$('deviceInfo').textContent=transport.getInfo();$('deviceDot').classList.add('online');
    message('设备已连接，可以开始烧录。');$('phase').textContent='设备已连接';
  }catch(error){report(error.name==='NotFoundError'?'cancel':'failure',error.name==='NotFoundError'?'':['SecurityError','NotAllowedError'].includes(error.name)?'permission':'connection');await disconnect().catch(()=>{});message(error.name==='NotFoundError'?'已取消选择串口。':'连接失败：'+error.message,'error');}
  finally{busy=false;controls();}
});
$('disconnect').addEventListener('click',async()=>{busy=true;controls();try{await disconnect();message('已断开连接。');}catch(e){message(e.message,'error');}finally{busy=false;controls();}});
function confirmModel(expected,actual){
  const dialog=$('modelConfirm');
  $('confirmFirmware').textContent=expected;$('confirmDevice').textContent=actual;
  dialog.returnValue='cancel';
  return new Promise(resolve=>{
    dialog.addEventListener('close',()=>resolve(dialog.returnValue==='continue'),{once:true});
    dialog.showModal();
  });
}
$('flash').addEventListener('click',async()=>{
  if(busy||!loader)return;
  if(mode==='stillink'&&selectedFirmware()){
    const expected=catalog.releases[Number($('release').value)].chip==='esp32c3'?'ESP32-C3':'ESP32-S3';
    if(loader.chip.CHIP_NAME!==expected){
      busy=true;controls();
      const proceed=await confirmModel(expected,loader.chip.CHIP_NAME);
      busy=false;controls();
      if(!proceed){message('已取消烧录，请检查设备型号。');return;}
      if(!loader){message('设备已断开，请重新连接。','error');return;}
    }
  }
  const report=stats.start('flash',{chip:statsChip(),version:mode==='stillink'?(catalog.releases[Number($('release').value)||0]?.version||'custom'):'custom',mode:mode==='stillink'?$('imageType').value:'custom'});
  busy=true;controls();$('progress').value=0;$('percent').textContent='0%';
  let written=false,errorType='invalid_file';
  try{
    let data,address=0,ota=false;
    if(mode==='stillink'){
      const firmware=selectedFirmware();if(!firmware)throw new Error('当前版本尚未提供所选固件，请选择其他版本或烧录方式');
      message('正在读取 Stillink 固件…');
      errorType='download';
      const response=await fetch(firmware.url);if(!response.ok)throw new Error('固件下载失败：'+response.status);
      data=new Uint8Array(await response.arrayBuffer());
      errorType='integrity';
      const sha=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',data)),x=>x.toString(16).padStart(2,'0')).join('');
      if(sha!==firmware.sha256)throw new Error('固件校验失败，请刷新页面后重试');
      ota=$('imageType').value==='ota';if(ota)address=0x10000;
    }else{
      const file=$('customFile').files[0];if(!file)throw new Error('请选择固件 BIN 文件');
      const raw=$('address').value.trim();if(!/^(?:0x[0-9a-f]+|\d+)$/i.test(raw))throw new Error('写入地址应为 0x0 等十六进制或十进制数字');
      address=Number(raw);if(!Number.isSafeInteger(address)||address<0||address>0xffffffff)throw new Error('写入地址超出范围');
      data=new Uint8Array(await file.arrayBuffer());if(!data.length)throw new Error('固件文件为空');
    }
    message('正在烧录，请保持 USB 连接…');$('phase').textContent='写入并校验';
    const fileArray=[{data,address}];
    if(ota)fileArray.push({data:new Uint8Array(0x2000).fill(255),address:0xd000});
    const fractions=fileArray.map(()=>0),totalBytes=fileArray.reduce((sum,file)=>sum+file.data.length,0);
    errorType='write';
    await loader.writeFlash({fileArray,flashMode:mode==='stillink'?'dio':'keep',flashFreq:mode==='stillink'?'40m':'keep',flashSize:mode==='stillink'?'4MB':'keep',eraseAll:!ota&&$('erase').checked,compress:true,
      calculateMD5Hash:bytes=>SparkMD5.ArrayBuffer.hash(bytes.slice().buffer),
      reportProgress(file,done,total){fractions[file]=done/total;const progress=Math.round(fileArray.reduce((sum,item,i)=>sum+item.data.length*fractions[i],0)/totalBytes*100);$('progress').value=progress;$('percent').textContent=progress+'%';}});
    written=true;report('success');await loader.after('hard_reset');
    message(mode==='stillink'?'烧录成功！请断开 ESP 的 USB／电源，再重新通电。启动后连接 Stillink 热点，打开 192.168.4.1。':'固件烧录成功！请断开 ESP 的 USB／电源，再重新通电。','success');$('phase').textContent='烧录完成';$('progress').value=100;$('percent').textContent='100%';
  }catch(error){report(written?'success':'failure',written?'':errorType);message(written?'固件已写入并校验，请重新插拔 USB 启动设备。':'烧录失败：'+error.message,written?'success':'error');$('phase').textContent=written?'写入完成':'烧录未完成';log('\n'+error.message+'\n');}
  finally{await disconnect().catch(()=>{});busy=false;controls();}
});
navigator.serial?.addEventListener('disconnect',()=>{if(!busy)disconnect().catch(()=>{});});
controls();
