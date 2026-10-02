// Exercise the real WASM save callbacks in an isolated Chrome profile.
// Only the test browser receives a tiny level; shipped assets are untouched.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');const {CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const base=new URL(process.argv[2] || 'http://127.0.0.1:3031/');
  const out=path.resolve(process.argv[3] || 'build/score-browser');fs.mkdirSync(out,{recursive:true});
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'chirky-score-test-'));
  const child=spawn(process.env.CHROME || (process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':'google-chrome'),
    ['--headless','--no-sandbox','--no-first-run','--use-angle=swiftshader','--enable-unsafe-swiftshader','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{windowsHide:true,stdio:'ignore'});
  let page;
  try{
    const portFile=path.join(profile,'DevToolsActivePort');for(let i=0;i<450&&!fs.existsSync(portFile);i++)await delay(100);
    const port=fs.readFileSync(portFile,'utf8').split('\n')[0];
    const tabs=await(await fetch(`http://127.0.0.1:${port}/json`)).json();page=new CDP(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
    await page.call('Page.enable');await page.call('Runtime.enable');
    await page.call('Emulation.setDeviceMetricsOverride',{width:844,height:390,deviceScaleFactor:1,mobile:true});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});
    await page.call('Page.addScriptToEvaluateOnNewDocument',{source:`globalThis.__scoreSounds=[];
      const originalStart=AudioBufferSourceNode.prototype.start;
      AudioBufferSourceNode.prototype.start=function(...args){__scoreSounds.push(this.buffer?.duration);return originalStart.apply(this,args);};`});
    const errors=[];page.on('Runtime.exceptionThrown',p=>errors.push(p.exceptionDetails.text));
    const rows=Array.from({length:26},()=>'.'.repeat(40));rows[24]='..S..E'+'.'.repeat(34);rows[25]='#'.repeat(40);
    await page.call('Fetch.enable',{patterns:[{urlPattern:'*runtime/games/phosphor-run/assets/levels/relay-shaft.txt'}]});
    page.on('Fetch.requestPaused',p=>page.call('Fetch.fulfillRequest',{requestId:p.requestId,responseCode:200,body:Buffer.from(rows.join('\n')+'\n').toString('base64')}).catch(e=>errors.push(e.message)));
    async function wait(expression){for(let i=0;i<150;i++){if(await page.eval(expression))return;await delay(100);}throw Error(`Timed out: ${expression}`);}
    async function key(code,key,value,hold=100){await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:value});await delay(hold);await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});await delay(150);}
    async function shot(name){const image=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(out,name+'.png'),Buffer.from(image.data,'base64'));}
    async function launch(){
      await page.eval('globalThis.__previousScorePage=true');
      await page.call('Page.navigate',{url:new URL('?game=phosphor-run',base).href});
      await wait(`!globalThis.__previousScorePage && document.querySelector('#status')?.textContent==='Phosphor Run' && document.querySelector('#startup').hidden`);
      await delay(250);await key('KeyX','x',88);
      // The close-up hold and zoom consume 130 ticks (2.17 seconds at 60 Hz).
      // Finish that introduction before walking the tiny test level.
      await delay(2700);await key('ArrowRight','ArrowRight',39,500);
    }
    await launch();await shot('initials');
    await page.eval('__scoreSounds.length=0');
    const pad=await page.eval(`(()=>{const r=document.querySelector('.dpad').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,r:r.width/2};})()`);
    async function touch(type,x,y){await page.call('Input.dispatchTouchEvent',{type,touchPoints:type==='touchEnd'?[]:[{x,y,id:1,radiusX:4,radiusY:4}]});await delay(120);}
    await touch('touchStart',pad.x,pad.y-pad.r*.7);
    await touch('touchMove',pad.x+pad.r*.7,pad.y-pad.r*.7);
    await touch('touchMove',pad.x+pad.r*.7,pad.y);
    await touch('touchEnd');await shot('initials-edited');
    assert.deepEqual(await page.eval('__scoreSounds'),[.045],'A diagonal slide changes one letter, without a selection sound');
    async function action(button){
      const point=await page.eval(`(()=>{const r=document.querySelector('[data-button="${button}"]').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
      await touch('touchStart',point.x,point.y);await touch('touchEnd');
    }
    await action(4);await action(5); // A forward, B back; editing remains on B.
    await delay(520);await shot('initials-blink');
    await action(4);await action(4);await key('ArrowDown','ArrowDown',40);await action(4);
    assert.deepEqual(await page.eval('__scoreSounds'),[.045,.1,.1,.1,.1,.045,.1],'Letter ticks and confirmations play through real WASM audio');
    const storageKey='chirky.save.v1.phosphor-run.scores-relay-shaft';
    await wait(`!!localStorage.getItem('${storageKey}')`);
    const saved=await page.eval(`atob(localStorage.getItem('${storageKey}'))`);
    assert.match(saved,/^CHIRKY-SCORES-1\n[1-9]\d* BAZ\n$/);await shot('score-saved');
    // A fresh document/main module must load the saved BAZ row into the table.
    await launch();await shot('scores-after-reload');
    await key('KeyX','x',88);await key('KeyX','x',88);await key('KeyX','x',88);
    const restored=await page.eval(`atob(localStorage.getItem('${storageKey}'))`);
    assert.match(restored,/ BAZ\n/);assert.match(restored,/ AAA\n/);assert.equal(restored.trim().split('\n').length,3);
    await key('Escape','Escape',27);await shot('pause-divider');
    assert.deepEqual(errors,[]);console.log('WASM: completed level, saved BAZ, reloaded, retained old score when saving AAA; no browser errors.');
  }finally{page?.close();child.kill();}
})().catch(e=>{console.error(e);process.exitCode=1;});
