// Real dashboard form, validation and save round trip without changing tuning.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process'),{CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function main(){
 const base=process.argv[2] || 'http://127.0.0.1:3030',out=path.resolve('build/character-balance');fs.mkdirSync(out,{recursive:true});
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
  await page.call('Page.navigate',{url:base+'/balance.html?game=circuit-clash'});
  for(let n=0;n<100;n++){if(await page.eval('document.querySelectorAll("#fields input").length>80'))break;await delay(100);}
  assert(await page.eval('document.querySelectorAll("#fields input").length>80'));
  assert.equal(await page.eval('document.querySelectorAll("#comparison tbody tr").length'),6);
  assert.match(await page.eval('document.querySelector("#comparison tbody").textContent'),/frames/);
  assert.doesNotMatch(await page.eval('document.querySelector("main").textContent'),/\bticks\b|\bms\b/);
  assert.equal(await page.eval('document.querySelector(\'[name="punch_followthrough_ticks"]\').value'), '4');
  const before=await(await fetch(base+'/api/games/circuit-clash/characters/arthur')).json();
  await page.eval(`document.querySelector('[name="crouch_kick_bottom_px"]').value=30;document.querySelector('[name="crouch_kick_bottom_px"]').dispatchEvent(new Event('input',{bubbles:true}));document.querySelector('#save').click()`);
  for(let n=0;n<100;n++){if(await page.eval('document.querySelector("#status").dataset.error==="true"'))break;await delay(50);}
  assert.match(await page.eval('document.querySelector("#status").textContent'),/crouch_kick_bottom_px < crouch_kick_top_px/);
  assert.equal((await(await fetch(base+'/api/games/circuit-clash/characters/arthur')).json()).hash,before.hash);
  await page.eval(`document.querySelector('[name="crouch_kick_bottom_px"]').value=1;document.querySelector('[name="crouch_kick_bottom_px"]').dispatchEvent(new Event('input',{bubbles:true}));document.querySelector('#save').click()`);
  for(let n=0;n<100;n++){if(await page.eval('document.querySelector("#status").textContent.startsWith("Saved locally")'))break;await delay(50);}
  assert.match(await page.eval('document.querySelector("#status").textContent'),/^Saved locally/);
  assert.equal((await(await fetch(base+'/api/games/circuit-clash/characters/arthur')).json()).hash,before.hash);
  for(const [name,width,height,density,mobile] of [['desktop',1200,1000,1,false],['mobile',390,844,2.625,true]]){
   await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});await delay(150);
   assert(await page.eval(`document.documentElement.scrollWidth<=${width} && window.innerWidth<=${width}`));
   const shot=await page.call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(out,name+'.png'),Buffer.from(shot.data,'base64'));
  }
  assert.deepEqual(errors,[]);console.log('Character balance: six moves, numeric fields, rejected invalid hitbox, unchanged valid save, desktop/mobile layout passed.');
 }finally{
  page?.close();if(browser){try{await browser.call('Browser.close');}catch{}browser.close();}
  if(child.exitCode===null)await Promise.race([new Promise(r=>child.once('exit',r)),delay(2000)]);
  if(child.exitCode===null)child.kill();
  assert.equal(path.dirname(profile),path.resolve(os.tmpdir()));assert(path.basename(profile).startsWith('chirky-balance-'));
  fs.rmSync(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1});
