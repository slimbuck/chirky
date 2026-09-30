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
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:850,deviceScaleFactor:1,mobile:false});
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
      await delay(250);await key('KeyX','x',88);await delay(1900);await key('ArrowRight','ArrowRight',39,500);
    }
    await launch();await shot('initials');
    await key('ArrowUp','ArrowUp',38);await shot('initials-edited');
    await delay(520);await shot('initials-blink');
    await key('KeyX','x',88);await key('KeyX','x',88);await key('ArrowDown','ArrowDown',40);await key('KeyX','x',88);
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
