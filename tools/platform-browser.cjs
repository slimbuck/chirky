// Read-only integration checks against the running dashboard. No npm dependencies.
// node tools/platform-browser.cjs [http://localhost:3030/play/] [output-directory]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

class CDP extends EventEmitter {
  constructor(url) {
    super(); this.next = 0; this.pending = new Map(); this.ws = new WebSocket(url);
    this.open = new Promise((resolve, reject) => { this.ws.onopen = resolve; this.ws.onerror = reject; });
    this.ws.onmessage = ({ data }) => {
      const message = JSON.parse(data);
      if (message.id) {
        const entry = this.pending.get(message.id); if (!entry) return;
        this.pending.delete(message.id); clearTimeout(entry.timer);
        if (message.error) entry.reject(new Error(JSON.stringify(message.error))); else entry.resolve(message.result);
      } else this.emit(message.method, message.params);
    };
  }
  async call(method, params = {}) {
    await this.open;
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`Timeout: ${method}`)); }, 15000);
      this.pending.set(id, { resolve, reject, timer }); this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  async eval(expression) {
    const value = await this.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (value.exceptionDetails) throw new Error(JSON.stringify(value.exceptionDetails));
    return value.result.value;
  }
  close() { this.ws.close(); }
}

const instrumentation = `(() => {
  globalThis.__platformLifecycle = event => {const key='platform-browser-lifecycle',items=JSON.parse(sessionStorage.getItem(key)||'[]');items.push({event,url:location.href,...(event==='destroy-end'?{texturesCreated:__platformAudit.texturesCreated,texturesDeleted:__platformAudit.texturesDeleted}:{})});sessionStorage.setItem(key,JSON.stringify(items));};
  addEventListener('pagehide',()=>__platformLifecycle('pagehide'));
  const audit = globalThis.__platformAudit = { texturesCreated:0, texturesDeleted:0, draws:0, uploads:[], buffers:[], starts:0, stops:0, decodes:0, contexts:[] };
  const gl = WebGLRenderingContext.prototype;
  for (const [name,counter] of [['createTexture','texturesCreated'],['deleteTexture','texturesDeleted'],['drawArrays','draws']]) {
    const original=gl[name];gl[name]=function(...args){const result=original.apply(this,args);audit[counter]++;return result;};
  }
  const upload=gl.texImage2D;gl.texImage2D=function(...args){audit.uploads.push({width:args[3],height:args[4]});return upload.apply(this,args);};
  globalThis.AudioBuffer=new Proxy(AudioBuffer,{construct(target,args){const result=Reflect.construct(target,args);audit.buffers.push({length:result.length,rate:result.sampleRate,channels:result.numberOfChannels});return result;}});
  const ac=AudioContext.prototype,create=ac.createBufferSource,decode=ac.decodeAudioData;
  ac.decodeAudioData=function(...args){audit.decodes++;return decode.apply(this,args);};
  ac.createBufferSource=function(...args){const source=create.apply(this,args),start=source.start,stop=source.stop;source.start=function(...a){audit.starts++;return start.apply(this,a);};source.stop=function(...a){audit.stops++;return stop.apply(this,a);};return source;};
  globalThis.AudioContext=new Proxy(AudioContext,{construct(target,args){const result=Reflect.construct(target,args);audit.contexts.push(result);return result;}});
})();`;

const attachProbe = `(() => {
  if(globalThis.__platformProbe)return;
  const probe=globalThis.__platformProbe={runtime,ticks:0,masks:[],assetPlays:0};
  probe.state=()=>({ready,paused,muted,leaving,status:status.textContent,assetSounds:assetSounds.size,
    callbacks:['onSound','onAssetReady','onAssetSound'].map(key=>[key,typeof runtime[key]]),
    audioState:audio?.state,activeSounds:sources.size,ticks:probe.ticks,masks:probe.masks,assetPlays:probe.assetPlays,
    audit:{...__platformAudit,contexts:__platformAudit.contexts.map(context=>context.state)}});
  let tick=runtime._web_tick;const tickProbe=function(mask){probe.ticks++;if(mask && probe.masks.at(-1)!==mask)probe.masks.push(mask);return tick(mask);};
  Object.defineProperty(runtime,'_web_tick',{configurable:true,get:()=>tickProbe,set:value=>{tick=value;}});
  const play=runtime.onAssetSound;runtime.onAssetSound=function(...args){probe.assetPlays++;return play(...args);};
  const destroy=runtime._web_destroy;runtime._web_destroy=function(...args){__platformLifecycle('destroy-begin');const result=destroy(...args);__platformLifecycle('destroy-end');return result;};
})();`;

async function main() {
  const base = new URL(process.argv[2] || 'http://localhost:3030/play/');
  if(!base.pathname.endsWith('/'))base.pathname+='/';
  const out = path.resolve(process.argv[3] || 'build/platform-browser'); fs.mkdirSync(out, { recursive: true });
  const chrome = process.env.CHROME || (process.platform === 'win32' ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe' : 'chromium');
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'platform-browser-'));
  const child = spawn(chrome, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--remote-debugging-port=0',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', `--user-data-dir=${profile}`, 'about:blank'], { windowsHide: true });
  let browserErrors = ''; child.stderr.on('data', data => { browserErrors += data; });
  child.on('error', error => { browserErrors += error.message; });
  let browser;
  const reports = [];
  try {
    const portFile = path.join(profile, 'DevToolsActivePort');
    for (let i=0; i<100 && !fs.existsSync(portFile); i++) await delay(100);
    assert(fs.existsSync(portFile), browserErrors);
    const port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
    const endpoint = `http://127.0.0.1:${port}`;
    const info = await (await fetch(`${endpoint}/json/version`)).json();
    browser = new CDP(info.webSocketDebuggerUrl); await browser.open;
    const source = await (await fetch(new URL('player.js',base))).text();
    const readyLine = source.split('\n').findIndex(line => /ready=true;/.test(line));
    assert(readyLine>=0, 'Cannot locate initialization checkpoint');
    for (const game of ['phosphor-run','rosey-chop']) for (const mobile of [false,true]) {
      const label = `${game}-${mobile?'mobile':'desktop'}`;
      const report = { label, errors:[], warnings:[], failedRequests:[], destroys:[], checks:[] }; reports.push(report);
      const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method:'PUT' })).json();
      const page = new CDP(target.webSocketDebuggerUrl); await page.open;
      page.on('Runtime.exceptionThrown', p => report.errors.push(p.exceptionDetails.exception?.description || p.exceptionDetails.text));
      page.on('Runtime.consoleAPICalled', p => {
        const values = p.args.map(arg=>arg.value ?? arg.description);
        if (values[0]==='PLATFORM_DESTROY') report.destroys.push(JSON.parse(values[1]));
        else if (p.type==='error') report.errors.push(values.join(' '));
        else if (p.type==='warning') report.warnings.push(values.join(' '));
      });
      page.on('Network.responseReceived', p => { if (p.response.status>=400) report.failedRequests.push({url:p.response.url,status:p.response.status}); });
      page.on('Network.loadingFailed', p => { if (!p.canceled) report.failedRequests.push({error:p.errorText}); });
      page.on('Debugger.paused', async p => {
        try {
          const result = await page.call('Debugger.evaluateOnCallFrame', { callFrameId:p.callFrames[0].callFrameId, expression:attachProbe });
          if (result.exceptionDetails) report.errors.push(JSON.stringify(result.exceptionDetails));
        } catch(error) { report.errors.push(error.message); }
        finally { await page.call('Debugger.resume').catch(()=>{}); }
      });
      await page.call('Page.enable');await page.call('Runtime.enable');await page.call('Network.enable');await page.call('Debugger.enable');
      await page.call('Network.setCacheDisabled', {cacheDisabled:true});
      await page.call('Debugger.setBreakpointByUrl', {url:new URL('player.js',base).href,lineNumber:readyLine});
      await page.call('Page.addScriptToEvaluateOnNewDocument', {source:instrumentation});
      await page.call('Emulation.setDeviceMetricsOverride', {width:mobile?390:1280,height:mobile?844:1000,deviceScaleFactor:1,mobile});
      await page.call('Emulation.setTouchEmulationEnabled', {enabled:mobile,maxTouchPoints:5});
      const state = () => page.eval('__platformProbe.state()');
      async function ready() {
        for(let i=0;i<150;i++) {
          if(await page.eval('!!globalThis.__platformProbe?.state().ready')) {await delay(250);return;}
          await delay(100);
        }
        throw new Error(`Initialization failed: ${await page.eval('document.querySelector("#status")?.textContent')}`);
      }
      async function click(selector,hold=70) {
        const point=await page.eval(`(() => {const e=document.querySelector(${JSON.stringify(selector)});e.scrollIntoView({block:'center'});const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
        if(mobile) {
          await page.call('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{...point,id:1}]});await delay(hold);
          await page.call('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
        } else {
          await page.call('Input.dispatchMouseEvent',{type:'mousePressed',...point,button:'left',clickCount:1});await delay(hold);
          await page.call('Input.dispatchMouseEvent',{type:'mouseReleased',...point,button:'left',clickCount:1});
        }
      }
      async function key(code,key,value,hold=100) {
        await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:value});await delay(hold);
        await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});
      }
      async function capture(name) {
        const pixels=await page.eval(`(() => {__platformProbe.runtime._web_render();const c=document.querySelector('#screen'),g=c.getContext('webgl'),p=new Uint8Array(c.width*c.height*4);g.readPixels(0,0,c.width,c.height,g.RGBA,g.UNSIGNED_BYTE,p);let hash=2166136261,lit=0;const colors=new Set();for(let i=0;i<p.length;i+=4){hash=Math.imul(hash^p[i],16777619);hash=Math.imul(hash^p[i+1],16777619);hash=Math.imul(hash^p[i+2],16777619);if(p[i]||p[i+1]||p[i+2])lit++;colors.add((p[i]<<16)|(p[i+1]<<8)|p[i+2]);}const r=c.getBoundingClientRect();return {hash:hash>>>0,lit,colors:colors.size,glError:g.getError(),rect:{x:r.x,y:r.y,width:r.width,height:r.height},overflow:document.documentElement.scrollWidth>innerWidth,touchVisible:getComputedStyle(document.querySelector('.touch')).display!=='none'};})()`);
        assert.equal(pixels.glError,0);assert(pixels.lit>1000 && pixels.colors>10,`${name}: blank canvas`);assert(!pixels.overflow,'Horizontal page overflow');
        await page.eval('window.scrollTo(0,0)');
        const screenshot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(path.join(out,`${label}-${name}.png`),Buffer.from(screenshot.data,'base64'));
        report[name]=pixels;return pixels;
      }
      try {
        console.log(`Checking ${label}`);
        await page.call('Page.navigate',{url:new URL(`?game=${game}`,base).href});await ready();
        report.initial=await state();assert.equal(report.initial.assetSounds,game==='phosphor-run'?6:7);
        assert(report.initial.callbacks.every(([,type])=>type==='function'));
        const title=await capture('title');assert.equal(title.touchVisible,mobile);
        if(mobile)await click('[data-button="5"]');else{await click('#screen');await key('Enter','Enter',13);}
        await delay(250);const playing=await capture('playing');assert.notEqual(playing.hash,title.hash);
        if(mobile){await click('[data-button="1"]',350);await click('[data-button="4"]');await click('[data-button="5"]');}
        else{await key('ArrowRight','ArrowRight',39,350);await key('KeyZ','z',90);await key('KeyX','x',88);}
        await delay(200);await capture('input');report.afterInput=await state();
        assert(report.afterInput.masks.some(mask=>mask&2));assert(report.afterInput.masks.some(mask=>mask&(1<<4)));
        assert.equal(report.afterInput.audioState,'running');assert(report.afterInput.assetPlays>0 && report.afterInput.audit.starts>0);
        report.checks.push('title, gameplay, directional/action input, decoded asset sounds played');
        await click('#pause');assert((await state()).paused);const ticks=(await state()).ticks;await delay(250);assert.equal((await state()).ticks,ticks);
        await click('#pause');await delay(150);assert(!(await state()).paused && (await state()).ticks>ticks);
        await click('#mute');assert((await state()).muted);assert.equal(await page.eval('document.querySelector("#mute").getAttribute("aria-pressed")'),'true');
        const starts=(await state()).audit.starts;
        if(mobile)await click('[data-button="5"]');else{await click('#screen');await key('KeyX','x',88);}
        await delay(100);assert.equal((await state()).audit.starts,starts);
        await click('#mute');assert(!(await state()).muted);report.checks.push('pause freezes updates, resume, mute/unmute');
        await click('#restart');await delay(150);await ready();await capture('restarted');
        report.lifecycle=await page.eval('JSON.parse(sessionStorage.getItem("platform-browser-lifecycle")||"[]")');
        report.destroys=report.lifecycle.filter(event=>event.event==='destroy-end');
        assert.equal(report.destroys.length,1,'Restart did not call web_destroy exactly once');
        assert(report.destroys.every(d=>d.texturesCreated===d.texturesDeleted),'Texture teardown imbalance');
        report.restartedState=await state();assert.equal(report.restartedState.assetSounds,report.initial.assetSounds);
        await click('nav a');await delay(150);await ready();assert.equal((await state()).status,'Launcher');
        report.lifecycle=await page.eval('JSON.parse(sessionStorage.getItem("platform-browser-lifecycle")||"[]")');
        report.destroys=report.lifecycle.filter(event=>event.event==='destroy-end');
        assert.equal(report.destroys.length,2,'Launcher navigation did not destroy game');
        assert(report.destroys.every(d=>d.texturesCreated===d.texturesDeleted));
        report.checks.push('UI restart reloads title, launcher navigation destroys game, all textures deleted');
        report.passed=true;
      } catch(error) {report.failure=error.stack;console.error(`${label}: ${error.message}`);}
      finally {
        await page.call('Page.navigate',{url:'about:blank'}).catch(()=>{});page.close();
        await fetch(`${endpoint}/json/close/${target.id}`).catch(()=>{});
        fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(reports,null,2));
        console.log(JSON.stringify({label,passed:report.passed||false,checks:report.checks,errors:report.errors,warnings:report.warnings,failedRequests:report.failedRequests}));
      }
    }
    assert(reports.every(r=>r.passed && !r.errors.length && !r.failedRequests.some(request=>!request.url?.endsWith('/favicon.ico'))),'See report.json for failures');
  } finally {
    if(browser){await browser.call('Browser.close').catch(()=>{});browser.close();}
    if(child.exitCode===null){await Promise.race([new Promise(resolve=>child.once('exit',resolve)),delay(3000)]);if(child.exitCode===null)child.kill();}
    assert.equal(path.dirname(profile),path.resolve(os.tmpdir()));
    fs.rmSync(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});
  }
  console.log(`All integrated browser checks passed. Artifacts: ${out}`);
}
main().catch(error=>{console.error(error);process.exitCode=1;});
