const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(require('node:path').join(__dirname,'../page/index.html'),'utf8');
const source=html.slice(html.indexOf('  function nativeIndices(){'),html.indexOf('  function rotate(delta)'));
const views=[[0,1,2,3,4,5],[3,0,4,1,5,2],[5,4,3,2,1,0],[2,5,1,4,0,3]];
for(let rotation=0;rotation<4;rotation++){
 const context={PANEL:{w:3,h:2},rotation,canvas:{width:rotation%2?2:3},indices:()=>Uint8Array.from(views[rotation]),Uint8Array};
 vm.runInNewContext(source,context);
 assert.deepEqual(Array.from(context.nativeIndices()),[0,1,2,3,4,5]);
}
console.log('BOARD GEOMETRY: PASS (portrait/landscape upload mapping, all four orientations)');
