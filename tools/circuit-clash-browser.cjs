// Real WASM integration check with simulated Gamepad API devices and keyboard.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {CDP}=require('./platform-browser.cjs');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function main(){
  const base=process.argv[2] || 'http://127.0.0.1:3030/play/';
  const out=path.resolve('build/circuit-clash-browser');fs.mkdirSync(out,{recursive:true});
  const tuning=Object.fromEntries(fs.readFileSync('games/circuit-clash/assets/characters/arthur.conf','utf8').split(/\r?\n/).filter(l=>/^[a-z_0-9]+=\d+$/.test(l)).map(l=>{const [k,v]=l.split('=');return [k,+v];}));
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
    await page.call('Page.enable');await page.call('Runtime.enable');await page.call('Network.enable');
    const responses=[];page.on('Network.responseReceived',e=>responses.push(e.response));
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
        devices:()=>localDevices(),label:onDeviceLabel,state:()=>shell?._web_console_state(),
        pixels:()=>{runtime._web_render();const gl=canvas.getContext('webgl'),p=new Uint8Array(320*240*4);
          gl.readPixels(0,0,320,240,gl.RGBA,gl.UNSIGNED_BYTE,p);return Array.from(p);},
        health:()=>{runtime._web_render();const gl=canvas.getContext('webgl'),row=new Uint8Array(320*4);
          gl.readPixels(0,190,320,1,gl.RGBA,gl.UNSIGNED_BYTE,row);
          let n=0;for(let x=168;x<296;x++)if(row[x*4]===255 && row[x*4+1]===121 && row[x*4+2]===92)n++;
          return n;},
        fighters:()=>{runtime._web_render();const gl=canvas.getContext('webgl'),pixels=new Uint8Array(320*240*4);
          gl.readPixels(0,0,320,240,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
          return [[82,220,203],[255,121,92]].map(c=>{let n=0,x=0;for(let y=50;y<53;y++)for(let px=0;px<320;px++){
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
    const navigation=await page.call('Page.navigate',{url:base});
    assert(!navigation.errorText,JSON.stringify(navigation));
    for(let i=0;i<200;i++){if(await page.eval('globalThis.fighterProbe?.state()===0'))break;await delay(100);}
    assert.equal(await page.eval('fighterProbe.state()'),0);
    await page.eval('document.querySelector("#screen").focus()');await delay(150);
    async function launcherKey(code) {
      await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key:code});await delay(100);
      await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key:code});await delay(100);
    }
    const catalog=await(await fetch(new URL('catalog.json',assetBase))).json();
    const index=catalog.games.filter(g=>g.role==='game').findIndex(g=>g.id==='circuit-clash');assert(index>=0);
    for(let i=0;i<index;i++)await launcherKey('ArrowDown');
    await launcherKey('KeyN');
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
    // Capture each authored idle frame in real game rendering, then inspect
    // integer physical pixel scales at fractional desktop/mobile densities.
    for(let f=0;f<4;f++){await page.eval('fighterProbe.step(12)');await shot('idle-'+f);}
    const native=Buffer.from(await page.eval('fighterProbe.pixels()'));
    // Raw, top-down PPM allows native-resolution review without browser scaling.
    const rgb=Buffer.alloc(320*240*3);
    for(let y=0;y<240;y++)for(let x=0;x<320;x++)native.copy(rgb,(y*320+x)*3,((239-y)*320+x)*4,((239-y)*320+x)*4+3);
    fs.writeFileSync(path.join(out,'native.ppm'),Buffer.concat([Buffer.from('P6\n320 240\n255\n'),rgb]));
    for(const [width,height,density,mobile] of [[1000,800,1.25,false],[390,844,2.625,true]]) {
      await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});
      await page.call('Emulation.setTouchEmulationEnabled',{enabled:mobile,maxTouchPoints:5});await delay(250);
      await page.eval('window.ChirkyShell.resizePlayer()');
      const r=await page.eval('(()=>{const r=document.querySelector("#screen").getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};})()');
      assert(Math.abs(r.width*density/320-Math.round(r.width*density/320))<.001,JSON.stringify({r,width,height,density,mobile}));
      // The renderer deliberately biases the aligned origin by -1/3 physical pixel.
      assert(Math.abs(r.x*density+1/3-Math.round(r.x*density+1/3))<.03,JSON.stringify({r,density,mobile}));
      assert(Math.abs(r.y*density+1/3-Math.round(r.y*density+1/3))<.03);
      await shot(mobile?'mobile-fractional':'desktop-fractional');
    }
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
    const before=await page.eval('fighterProbe.fighters()');assert(before.every(f=>f.n>=60),JSON.stringify({before,devices:await page.eval('fighterProbe.devices()'),state:await page.eval('fighterProbe.state()')}));
    await page.eval('testPads[0].buttons[15].pressed=true;fighterProbe.step(16);testPads[0].buttons[15].pressed=false;fighterProbe.step()');
    const after=await page.eval('fighterProbe.fighters()');assert(after[0].x>before[0].x+15);assert.equal(after[1].x,before[1].x);
    await page.eval('testPads[1].buttons[14].pressed=true;fighterProbe.step(16);testPads[1].buttons[14].pressed=false;fighterProbe.step()');
    const moved=await page.eval('fighterProbe.fighters()');assert(moved[1].x<after[1].x-15);assert.equal(moved[0].x,after[0].x);
    await shot('independent-movement');
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step()');await shot('punch-windup');
    await page.eval(`fighterProbe.step(${tuning.punch_startup_ticks})`);await shot('punch');
    await page.eval(`fighterProbe.step(${tuning.punch_active_ticks})`);await shot('punch-followthrough');
    await page.eval(`fighterProbe.step(${tuning.punch_followthrough_ticks})`);await shot('punch-retract');
    await page.eval('testPads[0].buttons[0].pressed=false;fighterProbe.step();testPads[1].buttons[12].pressed=true;fighterProbe.step(10)');await shot('jump');
    await page.eval('testPads[1].buttons[12].pressed=false;fighterProbe.step(55);testPads=testPads.slice(0,1);fighterProbe.step()');
    await shot('disconnected');
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step()');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step(92)');
    await shot('keyboard-and-pad');
    assert.equal(await page.eval('fighterProbe.label(1,4)'),'N');
    const rejoined=await page.eval('fighterProbe.fighters()');assert(rejoined.every(f=>f.n>=60));
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowLeft',key:'ArrowLeft',windowsVirtualKeyCode:37});await page.eval('fighterProbe.step(12)');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowLeft',key:'ArrowLeft',windowsVirtualKeyCode:37});await page.eval('fighterProbe.step()');
    const keyboard=await page.eval('fighterProbe.fighters()');assert(keyboard[1].x<rejoined[1].x-10);assert.equal(keyboard[0].x,rejoined[0].x);
    assert.equal(await page.eval('fighterProbe.health()'),128);
    for(let hit=0;hit<Math.ceil(tuning.health/tuning.punch_damage);hit++){
      await page.eval(`testPads[0].buttons[15].pressed=true;
        for(let i=0;i<100;i++){const f=fighterProbe.fighters();if(f[1].x-f[0].x<44)break;fighterProbe.step();}
        testPads[0].buttons[15].pressed=false;fighterProbe.step();
        testPads[0].buttons[0].pressed=true;fighterProbe.step(7);
        testPads[0].buttons[0].pressed=false;fighterProbe.step(24);`);
      assert.equal(await page.eval('fighterProbe.health()'),Math.floor(128*Math.max(0,tuning.health-(hit+1)*tuning.punch_damage)/tuning.health));
    }
    await shot('knockout');
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step();testPads[0].buttons[0].pressed=false;fighterProbe.step()');
    await shot('rematch-one-ready');
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step()');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n',windowsVirtualKeyCode:78});await page.eval('fighterProbe.step(92)');
    assert.equal(await page.eval('fighterProbe.health()'),128);await shot('rematch');
    // Review every gait drawing, both facings and traversal directions in WASM.
    // P1 is the pad; P2 is now the first keyboard layout after reconnecting.
    for(const retreat of [false,true]) {
      const button=retreat?14:15,code=retreat?'ArrowRight':'ArrowLeft';
      await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=true;fighterProbe.step()`);
      const order=retreat?[0,3,2,1]:[0,1,2,3],durations=[10,4,10,4];
      for(let i=0;i<4;i++) {
        if(i)await page.eval(`fighterProbe.step(${durations[order[i-1]]})`);
        await shot(`walk-${retreat?'back':'forward'}-${order[i]}`);
      }
      await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=false;fighterProbe.step(3)`);
    }
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n'});
    await page.eval('testPads[0].buttons[0].pressed=true;fighterProbe.step()');await shot('punch-both-windup');
    await page.eval('fighterProbe.step(4)');await shot('punch-both-strike');
    for(const [width,height,density,mobile] of [[1000,800,1.25,false],[390,844,2.625,true]]) {
      await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});
      await page.call('Emulation.setTouchEmulationEnabled',{enabled:mobile,maxTouchPoints:5});await delay(250);
      await page.eval('window.ChirkyShell.resizePlayer()');
      await shot(mobile?'punch-mobile-fractional':'punch-desktop-fractional');
    }
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
    await page.eval(`fighterProbe.step(${tuning.punch_active_ticks})`);await shot('punch-both-followthrough');
    fs.writeFileSync(path.join(out,'punch-followthrough.rgba'),Buffer.from(await page.eval('fighterProbe.pixels()')));
    await page.eval(`fighterProbe.step(${tuning.punch_followthrough_ticks})`);await shot('punch-both-retract');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n'});
    await page.eval('testPads[0].buttons[0].pressed=false;fighterProbe.step(8)');
    // A complete jump at native resolution, including both mirrored fighters.
    // These raw frames also produce the review GIF without resampling the art.
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowUp',key:'ArrowUp'});
    await page.eval('testPads[0].buttons[12].pressed=true');
    for(let t=0;t<29;t++) {
      await page.eval('fighterProbe.step()');
      fs.writeFileSync(path.join(out,`jump-${String(t).padStart(2,'0')}.rgba`),Buffer.from(await page.eval('fighterProbe.pixels()')));
      if([0,1,7,12,13,21,23,26].includes(t))await shot('jump-phase-'+t);
      if(t===7) {
        await page.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:2.625,mobile:true});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});await delay(250);
        await page.eval('window.ChirkyShell.resizePlayer()');await shot('jump-mobile-fractional');
        await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1.25,mobile:false});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
        await page.eval('window.ChirkyShell.resizePlayer()');await shot('jump-desktop-fractional');
        await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});await delay(150);
      }
    }
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowUp',key:'ArrowUp'});
    await page.eval('testPads[0].buttons[12].pressed=false;fighterProbe.step()');
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyM',key:'m'});
    await page.eval('testPads[0].buttons[2].pressed=true');
    assert.equal(await page.eval('fighterProbe.devices().filter(d=>d.mask&32).length'),2,'Pad and keyboard both request secondary');
    for(let t=0;t<=tuning.kick_startup_ticks+tuning.kick_active_ticks+tuning.kick_recovery_ticks+2;t++) {
      await page.eval('fighterProbe.step()');
      fs.writeFileSync(path.join(out,`kick-${String(t).padStart(2,'0')}.rgba`),Buffer.from(await page.eval('fighterProbe.pixels()')));
      if([0,tuning.kick_startup_ticks,tuning.kick_startup_ticks+tuning.kick_active_ticks,tuning.kick_startup_ticks+tuning.kick_active_ticks+tuning.kick_recovery_ticks].includes(t))await shot('kick-phase-'+t);
      if(t===tuning.kick_startup_ticks) {
        await page.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:2.625,mobile:true});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});await delay(200);
        await page.eval('window.ChirkyShell.resizePlayer()');await shot('kick-mobile-fractional');
        await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1.25,mobile:false});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
        await page.eval('window.ChirkyShell.resizePlayer()');await shot('kick-desktop-fractional');
        await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});await delay(150);
      }
    }
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyM',key:'m'});
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowDown',key:'ArrowDown'});
    await page.eval('testPads[0].buttons[2].pressed=false;testPads[0].buttons[13].pressed=true;fighterProbe.step(5)');
    await shot('kneel-both');
    fs.writeFileSync(path.join(out,'kneel.rgba'),Buffer.from(await page.eval('fighterProbe.pixels()')));
    await page.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:2.625,mobile:true});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});await delay(200);
    await page.eval('window.ChirkyShell.resizePlayer()');await shot('kneel-mobile-fractional');
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
    await page.eval('fighterProbe.step(5)');
    for(const [name,button,code] of [['crouch_punch',0,'KeyN'],['crouch_kick',2,'KeyM']]) {
      await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=true;fighterProbe.step()`);await shot(name+'-windup');
      await page.eval(`fighterProbe.step(${tuning[name+'_startup_ticks']})`);await shot(name+'-strike');
      for(const [width,height,density,mobile] of [[390,844,2.625,true],[1000,800,1.25,false]]) {
        await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:mobile,maxTouchPoints:mobile?5:1});await delay(150);
        await page.eval('window.ChirkyShell.resizePlayer()');await shot(name+(mobile?'-mobile':'-desktop')+'-fractional');
      }
      await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});await delay(150);
      fs.writeFileSync(path.join(out,name+'.rgba'),Buffer.from(await page.eval('fighterProbe.pixels()')));
      await page.eval(`fighterProbe.step(${tuning[name+'_active_ticks']})`);await shot(name+'-followthrough');
      await page.eval(`fighterProbe.step(${tuning[name+'_followthrough_ticks']})`);await shot(name+'-retract');
      await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=false;fighterProbe.step(${tuning[name+'_recovery_ticks']+2})`);
    }
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyN',key:'n'});
    await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'KeyM',key:'m'});
    await page.eval('testPads[0].buttons[0].pressed=testPads[0].buttons[2].pressed=true;fighterProbe.step(5)');await shot('crouch-block-both');
    await page.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:2.625,mobile:true});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});await delay(150);
    await page.eval('window.ChirkyShell.resizePlayer()');await shot('crouch-block-mobile');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowDown',key:'ArrowDown'});
    await page.eval('testPads[0].buttons[13].pressed=false;fighterProbe.step(5)');await shot('block-mobile');
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1.25,mobile:false});
    await page.call('Emulation.setTouchEmulationEnabled',{enabled:false,maxTouchPoints:1});await delay(150);
    await page.eval('window.ChirkyShell.resizePlayer()');await shot('block-both');
    await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});await delay(150);
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyN',key:'n'});
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'KeyM',key:'m'});
    await page.eval('testPads[0].buttons[0].pressed=testPads[0].buttons[2].pressed=false;fighterProbe.step(5)');
    await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowDown',key:'ArrowDown'});
    await page.eval('testPads[0].buttons[13].pressed=false;fighterProbe.step(5)');await shot('kneel-rise');
    // Air attacks use the approved native drawings, with independent timing.
    const manifest=JSON.parse(fs.readFileSync('games/circuit-clash/art/manifest.json','utf8'));
    const pam=fs.readFileSync('games/circuit-clash/assets/characters/arthur.pam');
    const art=pam.subarray(pam.indexOf('ENDHDR\n')+7),atlasWidth=manifest.atlas_size[0];
    function poseRow(pixels,centre,frame,flip) {
      const left=Math.round(centre+.5)-48,samples=[];
      for(let y=0;y<96;y++)for(let x=0;x<96;x++) {
        const a=((Math.floor(frame/manifest.atlas_columns)*96+y)*atlasWidth+(frame%manifest.atlas_columns)*96+(flip?95-x:x))*4;
        if(art[a+3])samples.push([x,y,a]);
      }
      for(let top=0;top<=144;top++) {
        if(samples.every(([x,y,a])=>{
          const b=((239-top-y)*320+left+x)*4;
          return art[a]===pixels[b] && art[a+1]===pixels[b+1] && art[a+2]===pixels[b+2];
        }))return top;
      }
      return -1;
    }
    for(const [name,button,code,durations] of [['punch',0,'KeyN',[tuning.air_punch_startup_ticks,tuning.air_punch_active_ticks,tuning.air_punch_recovery_ticks]],['kick',2,'KeyM',[tuning.air_kick_startup_ticks,tuning.air_kick_active_ticks,tuning.air_kick_recovery_ticks]]]) {
      await page.eval('fighterProbe.step(20)');
      await page.call('Input.dispatchKeyEvent',{type:'keyDown',code:'ArrowUp',key:'ArrowUp'});
      await page.eval('testPads[0].buttons[12].pressed=true;fighterProbe.step(3);testPads[0].buttons[12].pressed=false');
      await page.call('Input.dispatchKeyEvent',{type:'keyUp',code:'ArrowUp',key:'ArrowUp'});
      await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=true`);
      for(let t=0;t<28;t++) {
        await page.eval('fighterProbe.step()');
        const pixels=Buffer.from(await page.eval('fighterProbe.pixels()'));
        fs.writeFileSync(path.join(out,`air-${name}-${String(t).padStart(2,'0')}.rgba`),pixels);
        const hold=tuning['air_'+name+'_followthrough_ticks'];
        if([0,durations[0],durations[0]+durations[1],durations[0]+durations[1]+hold].includes(t)) {
          const phase=t===0?0:t<durations[0]+durations[1]+hold?1:2;
          const frame=manifest.clips.find(c=>c.id===(phase===1?'air_'+name:'jump')).first;
          const centres=await page.eval('fighterProbe.fighters()');
          for(let p=0;p<2;p++)assert(poseRow(pixels,centres[p].x,frame,p===1)>=0,`Air ${name} phase ${phase}, facing ${p}`);
          await shot(`air-${name}-phase-${phase}`);
        }
        if(t===durations[0]) {
          for(const [width,height,density,mobile] of [[390,844,2.625,true],[1000,800,1.25,false]]) {
            await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile});
            await page.call('Emulation.setTouchEmulationEnabled',{enabled:mobile,maxTouchPoints:mobile?5:1});await delay(150);
            await page.eval('window.ChirkyShell.resizePlayer()');await shot(`air-${name}-${mobile?'mobile':'desktop'}-fractional`);
          }
          await page.call('Emulation.setDeviceMetricsOverride',{width:1000,height:800,deviceScaleFactor:1,mobile:false});await delay(150);
        }
      }
      await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key:code});
      await page.eval(`testPads[0].buttons[${button}].pressed=false;fighterProbe.step()`);
    }
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
    const joined=await page.eval('fighterProbe.fighters()');assert(joined.every(f=>f.n>=60));
    await key('ArrowRight');await key('KeyA');await page.eval('fighterProbe.step(12)');
    await key('ArrowRight',false);await key('KeyA',false);await page.eval('fighterProbe.step()');
    const simultaneous=await page.eval('fighterProbe.fighters()');
    assert(simultaneous[0].x>joined[0].x+10 && simultaneous[1].x<joined[1].x-10);
    await shot('two-keyboards');
    await key('ArrowDown');await page.eval('fighterProbe.step(6)');await shot('crouch');
    await key('ArrowDown',false);await key('KeyN');await key('KeyM');await page.eval('fighterProbe.step(6)');await shot('block');
    await key('KeyN',false);await key('KeyM',false);await page.eval('fighterProbe.step(4)');
    await tapKey('Escape');assert.notEqual(await page.eval('fighterProbe.state()'),4);
    await shot('keyboard-pause');
    await tapKey('Escape');assert.equal(await page.eval('fighterProbe.state()'),4);
    // Fresh solo practice, including both wall bounds.
    await page.eval('globalThis.fighterProbe=null');await page.call('Page.reload');
    for(let i=0;i<200;i++){if(await page.eval('globalThis.fighterProbe?.ready()'))break;await delay(100);}
    assert(await page.eval('fighterProbe.ready()'));
    await page.eval('testPads=[];fighterProbe.freeze();fighterProbe.step(10)');
    await tapKey('KeyN');await tapKey('Enter');await page.eval('fighterProbe.step(140)');await shot('practice');
    assert((await page.eval('fighterProbe.fighters()')).every(f=>f.n>=60));
    await key('ArrowLeft');await page.eval('fighterProbe.step(200)');await key('ArrowLeft',false);
    await page.eval('fighterProbe.step(3)');await shot('left-edge');
    assert.equal((await page.eval('fighterProbe.fighters()'))[0].x,41.5);
    await key('KeyM');await page.eval(`fighterProbe.step(${tuning.kick_startup_ticks+1})`);await shot('kick-left-edge');
    await key('KeyM',false);await page.eval(`fighterProbe.step(${tuning.kick_active_ticks+tuning.kick_recovery_ticks+1})`);
    await key('ArrowRight');await page.eval('fighterProbe.step(400)');await key('ArrowRight',false);
    await page.eval('fighterProbe.step(3)');await shot('right-edge');
    const edge=await page.eval('fighterProbe.fighters()');assert(edge[1].x<=278 && edge[1].x-edge[0].x>=40);
    await key('KeyM');await page.eval(`fighterProbe.step(${tuning.kick_startup_ticks+1})`);await shot('kick-right-edge');
    await key('KeyM',false);await page.eval(`fighterProbe.step(${tuning.kick_active_ticks+tuning.kick_recovery_ticks+1})`);
    await tapKey('Enter');await page.eval('fighterProbe.step(140)');await shot('practice-reset');
    assert.equal((await page.eval('fighterProbe.fighters()'))[0].x,87.5);
    for(const suffix of ['/circuit-clash.js','/circuit-clash.wasm','/characters/arthur.pam'])
      assert(responses.some(r=>new URL(r.url).pathname.endsWith(suffix) && r.status===200),suffix+' HTTP 200');
    assert.deepEqual(errors,[]);console.log('Real WASM: launcher, native idle/walk/punch/jump/kick/kneel in both facings, fractional desktop/mobile scales, practice, walls, guard, two-player input, disconnect/rejoin, combat and rematch passed.');
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
