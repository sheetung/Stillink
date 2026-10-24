import { build } from 'esbuild';
import { readFile,writeFile,mkdir,rm } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const dir=path.dirname(fileURLToPath(import.meta.url));
const output=path.join(dir,'public');
await rm(output,{recursive:true,force:true});
await mkdir(path.join(output,'firmware'),{recursive:true});
const manifest={releases:[],releaseURL:'https://github.com/sheetung/Stillink/releases'};
async function asset(data,file){
  await writeFile(path.join(output,'firmware',file),data);
  return {file,url:'firmware/'+file,size:data.length,sha256:createHash('sha256').update(data).digest('hex')};
}
if(process.argv.includes('--local')){
  const version=(await readFile(path.join(dir,'../../main/version.h'),'utf8')).match(/#define\s+EPAPER_VERSION\s+"([^"]+)"/)[1].toLowerCase();
  for(const chip of ['esp32s3','esp32c3']){
  const release={version,chip};
  for(const [type,suffix] of [['ota','-ota'],['full','']]){
    const file=`stillink-${version}-${chip}${suffix}.bin`;
    let data;try{data=await readFile(path.join(dir,'../../build/dist',file));}catch(e){if(e.code==='ENOENT')continue;throw e;}
    release[type]=await asset(data,file);
  }
  if(release.ota||release.full)manifest.releases.push(release);
  }
}else{
  const headers={'User-Agent':'Stillink-Pages','Accept':'application/vnd.github+json'};
  if(process.env.GITHUB_TOKEN)headers.Authorization='Bearer '+process.env.GITHUB_TOKEN;
  const response=await fetch('https://api.github.com/repos/sheetung/Stillink/releases?per_page=10',{headers});
  if(!response.ok)throw new Error('Release list request failed: '+response.status);
  let versions=0;
  for(const item of await response.json()){
    if(item.draft||item.prerelease||!/^v\d+\.\d+\.\d+$/.test(item.tag_name))continue;
    for(const chip of ['esp32s3','esp32c3']){
    const release={version:item.tag_name,chip,notes:item.body||""};
    for(const [type,suffix] of [['ota','-ota'],['full','']]){
      const file=`stillink-${item.tag_name}-${chip}${suffix}.bin`;
      const found=item.assets.find(a=>a.name===file);if(!found)continue;
      const download=await fetch(found.browser_download_url);if(!download.ok)throw new Error('Firmware download failed: '+file);
      const data=Buffer.from(await download.arrayBuffer());
      const hash=createHash('sha256').update(data).digest('hex');
      if(found.digest&&found.digest!=='sha256:'+hash)throw new Error('Release digest mismatch: '+file);
      release[type]=await asset(data,file);
    }
    if(release.ota||release.full)manifest.releases.push(release);
    }
    if(++versions===5)break;
  }
}
const statsEndpoint=process.env.FLASHER_STATS_URL||'';
if(statsEndpoint){const url=new URL(statsEndpoint);if(url.protocol!=='https:'||url.username||url.password||url.search||url.hash||url.pathname!=='/events')throw new Error('FLASHER_STATS_URL must be an HTTPS /events URL');}
const bundled=await build({entryPoints:[path.join(dir,'app.js')],bundle:true,write:false,minify:true,format:'iife',legalComments:'inline',define:{__STATS_ENDPOINT__:JSON.stringify(statsEndpoint)}});
let html=await readFile(path.join(dir,'index.html'),'utf8');
html=html.replace('__MANIFEST__',()=>JSON.stringify(manifest)).replace('__BUNDLE__',()=>bundled.outputFiles[0].text.replace(/<\/script/gi,'<\\/script'));
const licenses=await Promise.all(['esptool-js/LICENSE','spark-md5/LICENSE','pako/LICENSE','atob-lite/LICENSE.md'].map(async name=>name+'\n'+await readFile(path.join(dir,'node_modules',name),'utf8')));
html=html.replace('</body>',()=>'<script type="text/plain" id="thirdPartyLicenses">'+licenses.join('\n\n').replace(/<\/script/gi,'<\\/script')+'</script></body>');
await writeFile(path.join(output,'index.html'),html);
await writeFile(path.join(output,'releases.json'),JSON.stringify(manifest));
await writeFile(path.join(output,'.nojekyll'),'');
console.log(`Built page with ${manifest.releases.length} firmware releases`);
