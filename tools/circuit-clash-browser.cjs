// Real WASM integration check with simulated Gamepad API devices and keyboard.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function main(){
  const base=process.argv[2] || 'http://127.0.0.1:3030/play/';
  const out=path.resolve('build/circuit-clash-browser');fs.mkdirSync(out,{recursive:true});
  const profile=fs.mkdtempSync(path.join(os.tmpdir(),'chirky-fighter-'));
  const chrome=process.env.CHROME || (process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':'chromium');
  const child=spawn(chrome,['--headless','--no-sandbox','--no-first-run','--use-angle=swiftshader',
    '--enable-unsafe-swiftshader','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{windowsHide:true,stdio:'ignore'});
  let browser,page;
  try{
    const portFile=path.join(profile,'DevToolsActivePort');
    for(let i=0;i<450 && !fs.existsSync(portFile);i++)await delay(100);
    assert(fs.existsSync(portFile),'Chrome did not start');
    const endpoint='http://127.0.0.1:'+fs.readFileSync(portFile,'utf8').split('\n')[0];
    browser=new CDP((await(await fetch(endpoint+'/json/version')).json()).webSocketDebuggerUrl);await browser.open;
    page=new CDP((await(await fetch(endpoint+'/json/new?about:blank',{method:'PUT'})).json()).webSocketDebuggerUrl);await page.open;
    await page.call('Page.enable');await page.call('Runtime.enable');
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});
    const errors=[];page.on('Runtime.exceptionThrown',e=>errors.push(e.exceptionDetails.text));
    const html=await(await fetch(base)).text();
    const assetBase=new URL(/<base href="([^"]+)"/.exec(html)?.[1] || '.',base);
    const source=(await(await fetch(new URL('player.js',assetBase))).text()).replace('start().catch(startupFailed);',`
      globalThis.fighterProbe={
        ready:()=>ready&&!loading&&shell._web_console_state()===4,freeze:()=>{leaving=true;},
        step:(n=1)=>{for(let i=0;i<n;i++){feedLocalInputs();const m=mask();
          if(shell._web_console_tick(m,keyboardMask(),controllerMask(),keys.size,0,0,0))runtime._web_tick(m);
          pending=dpadPending=0;}runtime._web_render();},
        devices:()=>localDevices(),label:onDeviceLabel,state:()=>shell._web_console_state(),
        health:()=>{runtime._web_render();const gl=canvas.getContext('webgl'),row=new Uint8Array(320*4);
          gl.readPixels(0,190,320,1,gl.RGBA,gl.UNSIGNED_BYTE,row);
          let n=0;for(let x=168;x<296;x++)if(row[x*4]===255 && row[x*4+1]===121 && row[x*4+2]===92)n++;
          return n;},
        fighters:()=>{runtime._web_render();const gl=canvas.getContext('webgl'),pixels=new Uint8Array(320*240*4);
          gl.readPixels(0,0,320,240,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
          return [[82,220,203],[255,121,92]].map(c=>{let n=0,x=0;for(let y=54;y<150;y++)for(let px=0;px<320;px++){
            const o=(y*320+px)*4;if(c.every((v,k)=>pixels[o+k]===v)){n++;x+=px;}}
            return {n,x:n?x/n:0};});}
      };start().catch(startupFailed);`);
    fs.writeFileSync(path.join(out,'instrumented-player.mjs'),source);
    await page.call('Fetch.enable',{patterns:[{urlPattern:'*player.js*'}]});
    page.on('Fetch.requestPaused',async e=>{await page.call('Fetch.fulfillRequest',{requestId:e.requestId,responseCode:200,
      responseHeaders:[{name:'Content-Type',value:'application/javascript'}],body:Buffer.from(source).toString('base64')});});
    await page.call('Page.addScriptToEvaluateOnNewDocument',{source:`
      globalThis.testPads=[0,1].map(index=>({index,id:'Identical test pads',mapping:'standard',
        buttons:Array.from({length:16},()=>({pressed:false})),axes:[0,0]}));
      Object.defineProperty(navigator,'getGamepads',{value:()=>testPads});`});
    const navigation=await page.call('Page.navigate',{url:base+'?game=circuit-clash'});
    assert(!navigation.errorText,JSON.stringify(navigation));
    for(let i=0;i<200;i++){if(await page.eval('globalThis.fighterProbe?.ready()'))break;await delay(100);}
    assert(await page.eval('globalThis.fighterProbe?.ready()'),JSON.stringify({errors,state:await page.eval('({url:location.href,html:document.documentElement.outerHTML.slice(0,500)})')}));
    await page.eval('fighterProbe.freeze();fighterProbe.step(10)');
    async function shot(name){
      const clip=await page.eval(`(()=>{const r=document.querySelector('#screen').getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,scale:1};})()`);
      assert(Math.abs(clip.height-clip.width*240/320)<.001);
      const png=await page.call('Page.captureScreenshot',{format:'png',clip});
      fs.writeFileSync(path.join(out,name+'.png'),Buffer.from(png.data,'base64'));
    }
    await shot('lobby');
    const devices=await page.eval('fighterProbe.devices()');assert.notEqual(devices[3].id,devices[4].id);
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step();testPads[0].buttons[0].pressed=false;fighterProbe.step()');
    await shot('player-one-joined');
    await page.eval('testPads[1].buttons[0].pressed=true;fighterProbe.step();testPads[1].buttons[0].pressed=false;fighterProbe.step(92)');
    await shot('two-pads');
    const before=await page.eval('fighterProbe.fighters()');assert(before.every(f=>f.n>100),JSON.stringify({before,devices:await page.eval('fighterProbe.devices()'),state:await page.eval('fighterProbe.state()')}));
    await page.eval('testPads[0].buttons[15].pressed=true;fighterProbe.step(16);testPads[0].buttons[15].pressed=false;fighterProbe.step()');
    const after=await page.eval('fighterProbe.fighters()');assert(after[0].x>before[0].x+15);assert.equal(after[1].x,before[1].x);
    await page.eval('testPads[1].buttons[14].pressed=true;fighterProbe.step(16);testPads[1].buttons[14].pressed=false;fighterProbe.step()');
    const moved=await page.eval('fighterProbe.fighters()');assert(moved[1].x<after[1].x-15);assert.equal(moved[0].x,after[0].x);
    await shot('independent-movement');
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step(7)');await shot('punch');
    await page.eval('testPads[0].buttons[0].pressed=false;fighterProbe.step();testPads[1].buttons[2].pressed=true;fighterProbe.step(5)');await shot('jump');
    await page.eval('testPads[1].buttons[2].pressed=false;fighterProbe.step(55);testPads=testPads.slice(0,1);fighterProbe.step()');
    await shot('disconnected');
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step()');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step(92)');
    await shot('keyboard-and-pad');
    assert.equal(await page.eval('fighterProbe.label(1,4)'),'N');
    const rejoined=await page.eval('fighterProbe.fighters()');assert(rejoined.every(f=>f.n>100));
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowLeft',key:'ArrowLeft',windowsVirtualKeyCode:37});await page.eval('fighterProbe.step(12)');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowLeft',key:'ArrowLeft',windowsVirtualKeyCode:37});await page.eval('fighterProbe.step()');
    const keyboard=await page.eval('fighterProbe.fighters()');assert(keyboard[1].x<rejoined[1].x-10);assert.equal(keyboard[0].x,rejoined[0].x);
    assert.equal(await page.eval('fighterProbe.health()'),128);
    for(let hit=0;hit<5;hit++){
      await page.eval(`testPads[0].buttons[15].pressed=true;
        for(let i=0;i<100;i++){const f=fighterProbe.fighters();if(f[1].x-f[0].x<20)break;fighterProbe.step();}
        testPads[0].buttons[15].pressed=false;fighterProbe.step();
        testPads[0].buttons[0].pressed=true;fighterProbe.step(7);
        testPads[0].buttons[0].pressed=false;fighterProbe.step(24);`);
      assert.equal(await page.eval('fighterProbe.health()'),Math.floor(128*(4-hit)/5));
    }
    await shot('knockout');
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step();testPads[0].buttons[0].pressed=false;fighterProbe.step()');
    await shot('rematch-one-ready');
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step()');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step(92)');
    assert.equal(await page.eval('fighterProbe.health()'),128);await shot('rematch');
    // Start a fresh match using only two layouts on one keyboard.
    await page.eval('globalThis.fighterProbe=null');
    await page.call('Page.reload');
    for(let i=0;i<200;i++){if(await page.eval('globalThis.fighterProbe?.ready()'))break;await delay(100);}
    await page.eval('testPads=[];fighterProbe.freeze();fighterProbe.step(10)');
    async function key(code,down=true){
      await page.call('Input.dispatchKeyEvent',{type:down?'keyDown':'keyUp',code,key:code});
    }
    async function tapKey(code){await key(code);await page.eval('fighterProbe.step()');await key(code,false);await page.eval('fighterProbe.step(3)');}
    await tapKey('Enter');
    assert((await page.eval('fighterProbe.fighters()')).every(f=>f.n===0),'Shared Enter cannot claim player slots');
    await tapKey('KeyN');await tapKey('KeyF');await page.eval('fighterProbe.step(92)');
    const joined=await page.eval('fighterProbe.fighters()');assert(joined.every(f=>f.n>100));
    await key('ArrowRight');await key('KeyA');await page.eval('fighterProbe.step(12)');
    await key('ArrowRight',false);await key('KeyA',false);await page.eval('fighterProbe.step()');
    const simultaneous=await page.eval('fighterProbe.fighters()');
    assert(simultaneous[0].x>joined[0].x+10 && simultaneous[1].x<joined[1].x-10);
    await shot('two-keyboards');
    await tapKey('Escape');assert.notEqual(await page.eval('fighterProbe.state()'),4);
    await shot('keyboard-pause');
    await tapKey('Escape');assert.equal(await page.eval('fighterProbe.state()'),4);
    assert.deepEqual(errors,[]);console.log('Real WASM: identical pads, independent movement, keyboard rejoin, shared-keyboard movement/pause, mapped labels, damage/KO/rematch and 4:3 screenshots passed.');
  }finally{
    page?.close();if(browser){try{await browser.call('Browser.close');}catch{}browser.close();}
    if(child.exitCode===null)await Promise.race([new Promise(resolve=>child.once('exit',resolve)),delay(3000)]);
    if(child.exitCode===null)child.kill();
    assert.equal(path.dirname(profile),path.resolve(os.tmpdir()));
    assert(path.basename(profile).startsWith('chirky-fighter-'));
    fs.rmSync(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});
  }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
