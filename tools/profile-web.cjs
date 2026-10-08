// Fresh browser profiles measure browser-cold visits (CDN caches are not purged).
// node tools/profile-web.cjs URL OUTPUT [--mobile] [--games]
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const {CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const base=process.argv[2] || 'https://chirky.org/';
const out=process.argv[3] || 'build/load-profile';fs.mkdirSync(out,{recursive:true});
const mobile=process.argv.includes('--mobile');
const conditions=mobile?{latency:85,downloadThroughput:1600000/8,uploadThroughput:750000/8,cpuSlowdown:4}:null;
const injected=`(()=>{
 const p=globalThis.__loadProfile={stages:[],wasm:[],contexts:[],longTasks:[],firstDraw:null};
 let last='';new MutationObserver(()=>{const status=document.querySelector('#status')?.textContent,percent=document.querySelector('#startup-percent')?.textContent,hidden=document.querySelector('#startup')?.hidden;
 const key=JSON.stringify([status,percent,hidden]);if(key!==last){last=key;p.stages.push({at:performance.now(),status,percent,hidden});}}).observe(document,{subtree:true,childList:true,attributes:true,characterData:true});
 new PerformanceObserver(list=>{for(const e of list.getEntries())p.longTasks.push({start:e.startTime,duration:e.duration});}).observe({type:'longtask',buffered:true});
 for(const name of ['compile','compileStreaming','instantiate','instantiateStreaming']){const original=WebAssembly[name];WebAssembly[name]=function(...args){const entry={name,start:performance.now(),bytes:args[0]?.byteLength};p.wasm.push(entry);try{const result=original.apply(this,args);return Promise.resolve(result).then(value=>{entry.end=performance.now();return value;},error=>{entry.end=performance.now();entry.error=String(error);throw error;});}catch(error){entry.error=String(error);throw error;}};}
 const context=HTMLCanvasElement.prototype.getContext;HTMLCanvasElement.prototype.getContext=function(...args){const start=performance.now(),result=context.apply(this,args);if(args[0]==='webgl')p.contexts.push({start,end:performance.now()});return result;};
 const draw=WebGLRenderingContext.prototype.drawArrays;WebGLRenderingContext.prototype.drawArrays=function(...args){if(p.firstDraw===null)p.firstDraw=performance.now();return draw.apply(this,args);};
})();`;
async function run(index){
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'chirky-prod-load-'));
 const chrome=process.env.CHROME || (process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':'google-chrome');
 const child=spawn(chrome,['--headless','--no-sandbox','--no-first-run','--no-default-browser-check','--enable-unsafe-swiftshader','--disable-background-timer-throttling','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
 let page;const results=[];
 try{
  const portFile=path.join(profile,'DevToolsActivePort');for(let i=0;i<200&&!fs.existsSync(portFile);i++)await delay(100);
  const port=fs.readFileSync(portFile,'utf8').split('\n')[0];const tabs=await(await fetch('http://127.0.0.1:'+port+'/json')).json();
  page=new CDP(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  for(const domain of ['Page','Runtime','Network'])await page.call(domain+'.enable');
  if(conditions){
   const {cpuSlowdown,...network}=conditions;
   await page.call('Network.emulateNetworkConditions',{offline:false,...network});
   await page.call('Emulation.setCPUThrottlingRate',{rate:cpuSlowdown});
  }
  await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:850,deviceScaleFactor:1,mobile:false});
  await page.call('Page.addScriptToEvaluateOnNewDocument',{source:injected});
  let requests=new Map(),errors=[];
  page.on('Runtime.exceptionThrown',e=>errors.push(e.exceptionDetails));
  page.on('Network.requestWillBeSent',e=>requests.set(e.requestId,{url:e.request.url,start:e.timestamp,type:e.type,initiator:e.initiator.type}));
  page.on('Network.responseReceived',e=>{const r=requests.get(e.requestId);if(r)Object.assign(r,{response:e.timestamp,status:e.response.status,headers:e.response.headers,protocol:e.response.protocol,timing:e.response.timing,diskCache:e.response.fromDiskCache,serviceWorker:e.response.fromServiceWorker});});
  page.on('Network.loadingFinished',e=>{const r=requests.get(e.requestId);if(r)Object.assign(r,{end:e.timestamp,wireBytes:e.encodedDataLength});});
  page.on('Network.loadingFailed',e=>{const r=requests.get(e.requestId);if(r)r.error=e.errorText;});
  async function ready(status){for(let i=0;i<600;i++){if(await page.eval("!globalThis.__profileNavigating && document.querySelector('#startup')?.hidden && document.querySelector('#status')?.textContent==="+JSON.stringify(status)))return;await delay(100);}throw Error('Startup timeout '+status);}
  async function capture(label){
   const data=await page.eval(`(()=>{const gl=document.querySelector('#screen').getContext('webgl'),ext=gl?.getExtension('WEBGL_debug_renderer_info');return {...__loadProfile,now:performance.now(),navigation:performance.getEntriesByType('navigation').map(e=>e.toJSON()),resources:performance.getEntriesByType('resource').map(e=>e.toJSON()),paints:performance.getEntriesByType('paint').map(e=>e.toJSON()),renderer:ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):null};})()`);
   const report={label,base,conditions,...data,requests:[...requests.values()],errors};results.push(report);fs.writeFileSync(out+'/'+label+'.json',JSON.stringify(report,null,2));
   assert.equal(errors.length,0,'Browser exceptions during profiling');
   return report;
  }
  for(const cache of ['cold','warm']){
   requests=new Map();errors=[];
   if(index===1&&cache==='cold'){await page.call('Profiler.enable');await page.call('Profiler.start');}
   await page.eval('globalThis.__profileNavigating=true');
   await page.call('Page.navigate',{url:base});await ready('Launcher');await delay(120);
   await capture(index+'-'+cache+'-launcher');
   if(index===1&&cache==='cold'){
    const shot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(out+'/launcher.png',Buffer.from(shot.data,'base64'));
   }
   if(index===1&&cache==='cold'){const cpu=await page.call('Profiler.stop');fs.writeFileSync(out+'/startup.cpuprofile',JSON.stringify(cpu.profile));}
   if(!process.argv.includes('--games'))continue;
   await page.eval('globalThis.__loadProfile.gameRequested=performance.now()');
   await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await delay(90);
   await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n',windowsVirtualKeyCode:78});
   await ready('Phosphor Run');await delay(120);await capture(index+'-'+cache+'-game');
  }
 }finally{page?.close();child.kill();}
 return results;
}
(async()=>{const reports=[];for(let i=1;i<=3;i++){reports.push(...await run(i));console.log('Completed cold/warm sample '+i);}fs.writeFileSync(out+'/all.json',JSON.stringify(reports));
 const summary=reports.filter(r=>r.label.endsWith('-launcher')).map(r=>{
  const ready=r.stages.find(s=>s.hidden)?.at,html=r.navigation[0].responseEnd;
  return {sample:r.label,readyMs:ready,afterHtmlMs:ready-html,
   transferBytes:r.navigation[0].transferSize+r.resources.reduce((sum,a)=>sum+a.transferSize,0),
   wasmMs:r.wasm.reduce((sum,w)=>sum+w.end-w.start,0)};
 });fs.writeFileSync(out+'/summary.json',JSON.stringify({base,conditions,samples:summary},null,2));console.table(summary);
})().catch(e=>{console.error(e);process.exitCode=1});
