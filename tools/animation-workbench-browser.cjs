// Real WASM workbench: replay, pixel fidelity, A/B drafts and browser controls.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process'),{CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function main(){
 const base=process.argv[2] || 'http://127.0.0.1:3030',out=path.resolve('build/animation-workbench-review');fs.mkdirSync(out,{recursive:true});
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'chirky-balance-'));
 const chrome=process.env.CHROME || (process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':'chromium');
 const child=spawn(chrome,['--headless','--no-sandbox','--no-first-run','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{windowsHide:true,stdio:'ignore'});
 let browser,page;
 try{
  const portFile=path.join(profile,'DevToolsActivePort');for(let n=0;n<200&&!fs.existsSync(portFile);n++)await delay(100);
  assert(fs.existsSync(portFile));const endpoint='http://127.0.0.1:'+fs.readFileSync(portFile,'utf8').split('\n')[0];
  browser=new CDP((await(await fetch(endpoint+'/json/version')).json()).webSocketDebuggerUrl);await browser.open;
  page=new CDP((await(await fetch(endpoint+'/json/new?about:blank',{method:'PUT'})).json()).webSocketDebuggerUrl);await page.open;
  await page.call('Page.enable');await page.call('Runtime.enable');const errors=[];page.on('Runtime.exceptionThrown',e=>errors.push(e.exceptionDetails.text));
  await page.call('Emulation.setDeviceMetricsOverride',{width:1200,height:1000,deviceScaleFactor:1,mobile:false});

  await page.call('Page.navigate',{url:base+'/animation-workbench.html?game=circuit-clash'});
  for(let n=0;n<150;n++){if(await page.eval('window.animationWorkbench?.ready'))break;await delay(100);}
  assert(await page.eval('window.animationWorkbench?.ready'),await page.eval('document.querySelector("#status").textContent'));
  await page.eval('document.querySelector("#play").click()');
  const exchange=await page.eval('animationWorkbench.samples[1]');
  const hit=exchange.findIndex(s=>s[82]===1);assert(hit>0);
  assert.equal(exchange[hit][74],86);assert.equal(exchange.filter(s=>s[81]).length,3);
  for(let i=1;i<=3;i++)assert.deepEqual(exchange[hit+i].slice(0,80),exchange[hit].slice(0,80));
  assert.equal(Math.abs(exchange.at(-1)[40]-exchange[hit][40]),8*8);
  await page.eval('document.querySelector("#contact").click()');assert.equal(await page.eval('animationWorkbench.frame'),hit);
  await page.eval('{const e=document.querySelector("[name=punch_hitstop_ticks]");e.value=0;e.dispatchEvent(new Event("input"));}');
  const withoutFreeze=await page.eval('animationWorkbench.samples');
  assert.equal(withoutFreeze[0].filter(s=>s[81]).length,3);assert.equal(withoutFreeze[1].filter(s=>s[81]).length,0);
  await page.eval('document.querySelector("#reset-draft").click();document.querySelector("#opponent").value=2;document.querySelector("#opponent").dispatchEvent(new Event("change"))');
  const guard=await page.eval('animationWorkbench.samples[1]');assert(guard.some(s=>s[82]===4));assert.equal(guard.at(-1)[74],100);
  assert.equal(guard.filter(s=>s[81]).length,1);
  await page.eval('document.querySelector("#miss").click()');
  assert((await page.eval('animationWorkbench.samples[1]')).every(s=>!s[80]&&!s[82]&&s[74]===100));
  // Solo mode isolates native texels from the other fighter's overlapping art.
  await page.eval('document.querySelector("#opponent").value=0;document.querySelector("#opponent").dispatchEvent(new Event("change"))');
  const data=await page.eval('animationWorkbench.samples');assert.equal(data.length,2);assert.deepEqual(data[0],data[1]);
  assert(data[1].some(s=>s[9]===2)&&data[1].some(s=>s[9]===3));
  // Compare every opaque native sprite texel against the actual runtime atlas,
  // including mirroring. Guides are disabled so they cannot cover a pixel.
  await page.eval('document.querySelector("#guides").click()');
  const pam=fs.readFileSync('games/circuit-clash/assets/characters/arthur.pam'),offset=pam.indexOf('ENDHDR\n')+7;
  const width=Number(/WIDTH (\d+)/.exec(pam.subarray(0,offset).toString())[1]);
  for(let facing=0;facing<2;facing++){
   for(const f of [0,13,25,36,46,57,63,76,80,82,86]){
    await page.eval(`document.querySelector('#scrub').value=${f};document.querySelector('#scrub').dispatchEvent(new Event('input'))`);
    const s=await page.eval(`animationWorkbench.samples[1][${f}]`),pixels=Buffer.from(await page.eval('Array.from(document.querySelector("#screen-b").getContext("2d").getImageData(0,0,320,240).data)'));
    for(let y=0;y<96;y++)for(let x=0;x<96;x++){
     const source=offset+((s[19]+y)*width+s[18]+x)*4;if(!pam[source+3])continue;
     const dx=s[26]+s[14]+(s[22]?95-x:x),dy=s[29]-s[27]-s[15]-s[17]+y;
     if(dx<s[26]||dx>=s[26]+s[30]||dy<s[27]||dy>=s[27]+s[31])continue;
     assert(pam.subarray(source,source+4).equals(pixels.subarray((dy*320+dx)*4,(dy*320+dx)*4+4)),`Native texel ${f}:${x},${y}, mirror ${facing}`);
    }
   }
   await page.eval('document.querySelector("#facing").click()');
  }
  const png=await page.eval('document.querySelector("#screen-b").toDataURL("image/png").split(",")[1]');fs.writeFileSync(path.join(out,'native.png'),Buffer.from(png,'base64'));
  await page.eval('document.querySelector("#guides").click()');
  await page.eval('document.querySelector("#compare").click();document.querySelector("#scrub").value=82;document.querySelector("#scrub").dispatchEvent(new Event("input"))');
  assert.equal(await page.eval('animationWorkbench.frame'),82);
  const saved=await(await fetch(base+'/api/games/circuit-clash/characters/arthur')).json();
  await page.eval('{const e=document.querySelector("[name=ground_accel_fp8]");e.value=1;e.dispatchEvent(new Event("input"));}');
  const changed=await page.eval('animationWorkbench.samples');assert.deepEqual(changed[0],data[0]);assert.notDeepEqual(changed[1],data[1]);
  assert(changed[1].slice(46,64).some((s,i)=>s[4]!==data[1][46+i][4]),'Acceleration changes actual walk drawings');
  for(const phase of [1,2,3,4,5])assert(data[1].some(s=>s[32]===phase),'Missing locomotion phase '+phase);
  assert.equal((await(await fetch(base+'/api/games/circuit-clash/characters/arthur')).json()).hash,saved.hash);
  await page.eval('document.querySelector("#take-name").value="Slower acceleration";document.querySelector("#keep-take").click()');
  assert.match(await page.eval('document.querySelector("#status").textContent'),/Kept/);
  await page.eval('document.querySelector("#reference").value=document.querySelector("#takes").value;document.querySelector("#reference").dispatchEvent(new Event("change"))');
  assert.deepEqual(...await page.eval('animationWorkbench.samples'));
  await page.eval('{const e=document.querySelector("[name=punch_followthrough_ticks]");e.value=120;e.dispatchEvent(new Event("input"));}');
  assert.equal(await page.eval('animationWorkbench.valid'),false);assert(await page.eval('document.querySelector("#save").disabled'));
  await page.eval('document.querySelector("#reset-draft").click();document.querySelector("#reference").value="saved";document.querySelector("#reference").dispatchEvent(new Event("change"))');
  assert(await page.eval('animationWorkbench.valid'));assert.deepEqual(...await page.eval('animationWorkbench.samples'));
  await page.eval('document.querySelector("#scrub").value=82;document.querySelector("#scrub").dispatchEvent(new Event("input"));document.querySelector("#onion").click();document.querySelector("#boxes").click()');
  for(const [name,width,height,density,mobile] of [['desktop',1440,1100,1,false],['fractional',1200,1000,1.25,false],['mobile',390,844,2.625,true]]){
   await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});await delay(300);
   assert(await page.eval(`document.documentElement.scrollWidth<=${width} && window.innerWidth<=${width}`));
   const fit=await page.eval('Array.from(document.querySelectorAll("canvas")).map(c=>{const r=c.getBoundingClientRect();return {w:r.width*devicePixelRatio/320,x:r.left*devicePixelRatio,y:r.top*devicePixelRatio};})');
   for(const p of fit){assert(Math.abs(p.w-Math.round(p.w))<.01);assert(Math.abs(p.x-Math.round(p.x))<.02);assert(Math.abs(p.y-Math.round(p.y))<.02);}
   const shot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(out,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  await page.eval('document.querySelector("#live").click();document.querySelector("#restart").click();document.querySelector("#views").focus()');
  await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowRight',key:'ArrowRight'});
  await page.eval('for(let n=0;n<10;n++)document.querySelector("#next").click()');
  await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowRight',key:'ArrowRight'});
  assert.equal(await page.eval('animationWorkbench.frame'),10);assert(await page.eval('animationWorkbench.samples[1][10][0]>animationWorkbench.samples[1][0][0]'));
  await page.eval('document.querySelector("#previous").click();document.querySelector("#next").click()');assert.equal(await page.eval('animationWorkbench.frame'),10);
  assert.deepEqual(errors,[]);console.log('Workbench: real WASM replay, A/B isolation, draft safety, takes, scrubbing, invalid tuning, live inputs and pixel-aligned desktop/mobile previews passed.');

 }finally{
  page?.close();if(browser){try{await browser.call('Browser.close');}catch{}browser.close();}
  if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(2000)]);
  if(child.exitCode===null)child.kill();
  assert.equal(path.dirname(profile),path.resolve(os.tmpdir()));assert(path.basename(profile).startsWith('chirky-balance-'));
  fs.rmSync(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1});
