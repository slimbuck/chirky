// Read-only integration checks against the running dashboard. No npm dependencies.
// node tools/platform-browser.cjs [http://localhost:3030/play/] [output-directory]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

// Decode a screenshot scanline to check the actual rasterized pixel widths.
function pngRow(png, y, column) {
  const width=png.readUInt32BE(16), channels=png[25]===2?3:4, chunks=[];
  assert.equal(png[24],8);assert([2,6].includes(png[25]));
  for(let p=8;p<png.length;){const size=png.readUInt32BE(p);if(png.toString('ascii',p+4,p+8)==='IDAT')chunks.push(png.subarray(p+8,p+8+size));p+=size+12;}
  const raw=require('node:zlib').inflateSync(Buffer.concat(chunks)),stride=width*channels;
  const strip=column===undefined?null:Buffer.alloc((y+1)*channels);
  let previous=Buffer.alloc(stride),offset=0;
  for(let row=0;row<=y;row++){
    const filter=raw[offset++],decoded=Buffer.alloc(stride);
    for(let x=0;x<stride;x++){
      const a=x>=channels?decoded[x-channels]:0,b=previous[x],c=x>=channels?previous[x-channels]:0;
      const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);
      const prediction=[0,a,b,Math.floor((a+b)/2),pa<=pb&&pa<=pc?a:pb<=pc?b:c][filter];
      assert.notEqual(prediction,undefined);decoded[x]=(raw[offset++]+prediction)&255;
    }
    if(strip)decoded.copy(strip,row*channels,column*channels,(column+1)*channels);
    previous=decoded;
  }
  return {pixels:strip || previous,channels};
}

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
  const audit = globalThis.__platformAudit = { programsCreated:0, programsDeleted:0, texturesCreated:0, texturesDeleted:0, draws:0, uploads:[], buffers:[], starts:0, stops:0, decodes:0, contexts:[] };
  const gl = WebGLRenderingContext.prototype;
  for (const [name,counter] of [['createProgram','programsCreated'],['deleteProgram','programsDeleted'],['createTexture','texturesCreated'],['deleteTexture','texturesDeleted'],['drawArrays','draws']]) {
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
  const probe=globalThis.__platformProbe ||= {ticks:0,masks:[],assetPlays:0,seen:new WeakSet()};
  probe.runtime=runtime;probe.shell=shell;
  probe.state=()=>({ready,paused,muted,leaving,loading,id,status:status.textContent,screen:shell._web_console_state(),capture:shell._web_capture_keyboard(),assetSounds:assetSounds.size,
    callbacks:['onSound','onAssetReady','onAssetSound'].map(key=>[key,typeof runtime[key]]),
    audioState:audio?.state,activeSounds:sources.size,ticks:probe.ticks,masks:probe.masks,inputs:mask(),assetPlays:probe.assetPlays,
    audit:{...__platformAudit,contexts:__platformAudit.contexts.map(context=>context.state)}});
  if(probe.seen.has(runtime))return;probe.seen.add(runtime);
  let tick=runtime._web_tick;const tickProbe=function(mask){probe.ticks++;if(mask && probe.masks.at(-1)!==mask)probe.masks.push(mask);return tick(mask);};
  Object.defineProperty(runtime,'_web_tick',{configurable:true,get:()=>tickProbe,set:value=>{tick=value;}});
  const play=runtime.onAssetSound;runtime.onAssetSound=function(...args){probe.assetPlays++;return play(...args);};
  const stopped=runtime.onStopped;runtime.onStopped=function(...args){__platformLifecycle('destroy-end');return stopped(...args);};
})();`;

async function main() {
  const base = new URL(process.argv[2] || 'http://localhost:3030/play/');
  if(!base.pathname.endsWith('/'))base.pathname+='/';
  const out = path.resolve(process.argv[3] || 'build/platform-browser'); fs.mkdirSync(out, { recursive: true });
  const chrome = process.env.CHROME || (process.platform === 'win32' ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe' : 'chromium');
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'platform-browser-'));
  const child = spawn(chrome, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--remote-debugging-port=0', '--disable-dev-shm-usage',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', `--user-data-dir=${profile}`, 'about:blank'], { windowsHide: true });
  let browserErrors = ''; child.stderr.on('data', data => { browserErrors += data; });
  child.stdout.on('data', data => { browserErrors += data; });
  child.on('error', error => { browserErrors += error.message; });
  let browser;
  const reports = [];
  try {
    const portFile = path.join(profile, 'DevToolsActivePort');
    // Cold CI runners can spend more than ten seconds starting Chrome. Wait for
    // its actual debugging endpoint, while still failing immediately on exit.
    for (let i=0; i<450 && !fs.existsSync(portFile) && child.exitCode===null && child.signalCode===null; i++) await delay(100);
    assert(fs.existsSync(portFile), `Chrome did not expose DevTools within 45 seconds (exit=${child.exitCode}, signal=${child.signalCode}). ${browserErrors}`);
    const port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
    const endpoint = `http://127.0.0.1:${port}`;
    const info = await (await fetch(`${endpoint}/json/version`)).json();
    browser = new CDP(info.webSocketDebuggerUrl); await browser.open;
    const catalogResponse=await fetch(new URL('catalog.json',base));
    assert.equal(catalogResponse.status,200);
    const catalog=await catalogResponse.json();
    assert.equal(catalog.version,1);assert(catalog.games.length>0);
    // Keep the same physical pixel grid before/after loading and across games,
    // including fractional OS scale and narrow windows.
    const diagnostic=catalog.games.findIndex(game=>game.role==='diagnostic');
    assert(diagnostic>=0);
    for(const [width,height,density,touch=false] of (process.argv.includes('--console-only') || process.argv.includes('--catalog-only')?[]:[[1280,1000,1.25],[1000,800,1.5],[1280,1000,1.75],[390,844,2.625],[670,700,1],[844,390,3,true],[667,375,2,true],[390,844,3,true],[390,844,2.625,true],[412,915,2.625,true],[360,640,3,true],[320,568,2,true],[1024,768,2,true]])){
      const label=`layout-${width}-${density}${touch?'-touch':''}`,target=await(await fetch(`${endpoint}/json/new?about:blank`,{method:'PUT'})).json();
      const page=new CDP(target.webSocketDebuggerUrl);await page.open;
      const report={label,checks:[],errors:[],warnings:[],failedRequests:[],passed:false};reports.push(report);
      let held;
      page.on('Fetch.requestPaused',p=>{held=p.requestId;});
      async function waitFor(expression){for(let i=0;i<150;i++){if(await page.eval(expression))return;await delay(100);}throw Error(`Timed out: ${expression}`);}
      const measure=`(()=>{const c=document.querySelector('#screen'),r=c.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,density:devicePixelRatio,buffer:[c.width,c.height],overflow:document.documentElement.scrollWidth>innerWidth};})()`;
      async function checkLoading(name){
        for(let i=0;i<150&&!held;i++)await delay(100);assert(held,'WASM request must be held to test loading');
        const loading=await page.eval(measure);await page.call('Fetch.continueRequest',{requestId:held});held=null;
        await waitFor(`document.querySelector('#status')?.textContent===${JSON.stringify(name)}`);
        const loaded=await page.eval(measure);assert.deepEqual(loaded,loading,'Loading must not resize or move the screen');return loaded;
      }
      try{
        await page.call('Page.enable');await page.call('Runtime.enable');await page.call('Network.enable');
        await page.call('Network.setCacheDisabled',{cacheDisabled:true});
        await page.call('Fetch.enable',{patterns:[{urlPattern:'*.wasm'}]});
        await page.call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:density,mobile:touch});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:touch,maxTouchPoints:5});
        await page.call('Page.navigate',{url:base.href});
        const launcher=await checkLoading('Launcher');
        await page.eval('document.querySelector("#screen").focus()');
        for(const [code,key,value] of [['Escape','Escape',27],['ArrowDown','ArrowDown',40],['KeyX','x',88]]){
          await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:value});await delay(100);
          await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});await delay(100);
        }
        const game=await checkLoading(catalog.games[diagnostic].name);
        report.screen=game;
        assert.deepEqual(game,launcher,'Launcher and game must use the same screen size');
        assert.deepEqual(game.buffer,[320,240]);assert(!game.overflow);
        const physicalScale=game.width*density/320;
        assert(Math.abs(physicalScale-Math.round(physicalScale))<.001,'Each game pixel must use whole physical pixels');
        // The sampling bias stays within the same physical raster pixel;
        // screenshot checks below verify actual horizontal AND vertical texels.
        assert(Math.abs(game.x*density+1/3-Math.round(game.x*density+1/3))<.03);
        assert(Math.abs(game.y*density+1/3-Math.round(game.y*density+1/3))<.03);
        if(touch){
          const controls=await page.eval(`Array.from(document.querySelectorAll('.touch button')).map(e=>{const r=e.getBoundingClientRect();return {button:e.dataset.button,x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height};})`);
          assert.equal(controls.length,8);
          assert(controls.every(r=>r.width>=44 && r.height>=44 && r.x>=0 && r.y>=0 && r.right<=width && r.bottom<=height),'All eight touch targets must fit and be at least 44 CSS pixels');
          if(width>height){
            // Preserve the original middle-space allocation: two 148px control
            // columns, two 12px gaps, 12px outer padding, and the 1px screen border.
            // Decorative shell borders must not consume this space and knock a
            // narrow phone down an entire physical-pixel scaling step.
            const maximumScale=Math.floor(Math.min((width-346)/320,(height-26)/240)*density+1e-6);
            assert(Math.abs(game.width*density-320*maximumScale)<.03,'Landscape display must fill the original middle space at the largest integer physical scale');
            assert(controls.filter(r=>['0','1','2','3','7'].includes(r.button)).every(r=>r.right<=game.x),'D-pad and Menu must sit left of the display');
            assert(controls.filter(r=>['4','5','6'].includes(r.button)).every(r=>r.x>=game.x+game.width),'Actions and Start must sit right of the display');
            assert(Math.abs(game.y+game.height/2-height/2)<=1,'Display must be vertically centred');
          }else{
            const portrait=await page.eval(`(()=>{const rect=e=>{const r=e.getBoundingClientRect();return {top:r.top,bottom:r.bottom,height:r.height}};return {player:rect(document.querySelector('#player')),slot:rect(document.querySelector('#screen-slot')),grips:Array.from(document.querySelectorAll('.touch')).map(rect),overflow:document.documentElement.scrollHeight>innerHeight,heading:getComputedStyle(document.querySelector('header')).display,help:getComputedStyle(document.querySelector('.help')).display};})()`);
            assert.equal(portrait.heading,'none');assert.equal(portrait.help,'none');
            assert(!portrait.overflow,'Portrait touch console must fit the visible viewport');
            assert(Math.abs(portrait.player.height-height)<1);
            assert(portrait.grips.every(r=>Math.abs(r.bottom-(height-18))<1),'Both portrait grips must stay anchored at the bottom');
            assert(game.y+game.height+12<=Math.min(...portrait.grips.map(r=>r.top)),'Display must not overlap bottom controls');
            const maximumScale=Math.floor(Math.min((width-18)/320,(portrait.slot.height-2)/240)*density+1e-6);
            assert(Math.abs(game.width*density-320*maximumScale)<.03,'Portrait display must use the full available width at the largest crisp scale');
            if(width===390 && density===2.625)assert(game.width>360,'Typical Android portrait display must grow beyond the old nested-page size');
            report.checks.push('portrait fills visible viewport, maximizes display and anchors both grips at bottom');
          }
          report.checks.push('touch targets fit, controller wings flank the centred landscape display');
          if(width===390 && density===3){
            const before=await page.eval('visualViewport.scale');
            for(const [x,y] of [[game.x+game.width/2,game.y+game.height/2],[width/2,70]]){
              await page.call('Input.synthesizePinchGesture',{x,y,scaleFactor:1.8,relativeSpeed:800,gestureSourceType:'touch'});
              assert.equal(await page.eval('visualViewport.scale'),before,'Pinching the display or shell must not zoom the page');
            }
            assert(await page.eval(`['gesturestart','gesturechange'].every(name=>{const e=new Event(name,{bubbles:true,cancelable:true});document.querySelector('#screen').dispatchEvent(e);return e.defaultPrevented;})`),'Safari page-scale events must be cancelled');
            assert.deepEqual(await page.eval(measure),game,'Pinch attempts must preserve the framebuffer layout');
            report.checks.push('display and shell pinch gestures preserve page scale; Safari gesture defaults cancelled');
          }
        }
        const screenshot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(path.join(out,`${label}.png`),Buffer.from(screenshot.data,'base64'));
        await page.eval('window.requestAnimationFrame=()=>0');await delay(100);
        await page.eval(`(()=>{document.querySelector('#screen').blur();const gl=document.querySelector('#screen').getContext('webgl');gl.enable(gl.SCISSOR_TEST);for(let x=0;x<320;x++){gl.scissor(x,0,1,240);gl.clearColor(x%2,0,1-x%2,1);gl.clear(gl.COLOR_BUFFER_BIT);}gl.disable(gl.SCISSOR_TEST);})()`);
        const stripes=Buffer.from((await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false})).data,'base64');
        fs.writeFileSync(path.join(out,`${label}-pixels.png`),stripes);
        const {pixels,channels}=pngRow(stripes,Math.round((game.y+game.height/2)*density));
        const start=Math.round(game.x*density),step=Math.round(physicalScale);
        // Check every column of the CRT-safe game viewport. The surrounding
        // native black border can overlap the page's decorative screen border.
        const runs=[];
        for(let x=16*step;x<(320-16)*step;x++){
          const offset=(start+x)*channels,red=pixels[offset];
          assert(red===0 || red===255,'Pixel edges must not be blurred');
          assert.equal(pixels[offset+1],0);assert.equal(pixels[offset+2],255-red);
          if(runs.at(-1)?.red===red)runs.at(-1).width++;else runs.push({red,width:1});
        }
        // DOM bounds can round a physical pixel differently from the compositor;
        // measure complete color runs, excluding only the two clipped end runs.
        assert(runs.length>=287);
        for(const run of runs.slice(1,-1))assert.equal(run.width,step,'Every game pixel must have the same physical width');
        await page.eval(`(()=>{const gl=document.querySelector('#screen').getContext('webgl');gl.enable(gl.SCISSOR_TEST);for(let y=0;y<240;y++){gl.scissor(0,y,320,1);gl.clearColor(y%2,0,1-y%2,1);gl.clear(gl.COLOR_BUFFER_BIT);}gl.disable(gl.SCISSOR_TEST);})()`);
        const rows=Buffer.from((await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false})).data,'base64');
        const vertical=pngRow(rows,rows.readUInt32BE(20)-1,Math.round((game.x+game.width/2)*density));
        const rowRuns=[],top=Math.round(game.y*density);
        for(let y=12*step;y<(240-12)*step;y++){
          const offset=(top+y)*vertical.channels,red=vertical.pixels[offset];
          assert(red===0 || red===255);assert.equal(vertical.pixels[offset+1],0);assert.equal(vertical.pixels[offset+2],255-red);
          if(rowRuns.at(-1)?.red===red)rowRuns.at(-1).height++;else rowRuns.push({red,height:1});
        }
        assert(rowRuns.length>=215);
        for(const run of rowRuns.slice(1,-1))assert.equal(run.height,step,'Every game pixel must have the same physical height');
        report.checks.push('stable loading and launcher/game size; native framebuffer; uniform screenshot pixel widths at display scale');report.passed=true;
      }catch(error){report.failure=error.stack;console.error(`${label}: ${error.message}`);}
      finally{await page.call('Page.navigate',{url:'about:blank'}).catch(()=>{});page.close();await fetch(`${endpoint}/json/close/${target.id}`).catch(()=>{});console.log(JSON.stringify(report));}
    }
    if(process.argv.includes('--layout-only')){assert(reports.every(report=>report.passed),'Layout checks failed');return;}
    // Exercise the real launcher for every manifest entry, including new games.
    for(const mobile of (process.argv.includes('--console-only')?[]:[false,true])) for(const [index,game] of catalog.games.entries()) {
      const label=`launcher-${game.id}-${mobile?'mobile':'desktop'}`;
      const report={label,errors:[],warnings:[],failedRequests:[],checks:[]};reports.push(report);
      const target=await (await fetch(`${endpoint}/json/new?about:blank`,{method:'PUT'})).json();
      const page=new CDP(target.webSocketDebuggerUrl);await page.open;
      const responses=new Map();
      page.on('Runtime.exceptionThrown',p=>report.errors.push(p.exceptionDetails.text));
      page.on('Runtime.consoleAPICalled',p=>{
        if(p.type==='error')report.errors.push(p.args.map(arg=>arg.value ?? arg.description).join(' '));
        if(p.type==='warning')report.warnings.push(p.args.map(arg=>arg.value ?? arg.description).join(' '));
      });
      page.on('Network.responseReceived',p=>{
        responses.set(p.response.url,p.response.status);
        if(p.response.url.startsWith(base.href) && p.response.status>=400 && !p.response.url.endsWith('/favicon.ico'))
          report.failedRequests.push({url:p.response.url,status:p.response.status});
      });
      try {
        await page.call('Page.enable');await page.call('Runtime.enable');await page.call('Network.enable');
        await page.call('Network.setCacheDisabled',{cacheDisabled:true});
        await page.call('Emulation.setDeviceMetricsOverride',{width:mobile?390:1280,height:mobile?844:1000,deviceScaleFactor:1,mobile});
        await page.call('Emulation.setTouchEmulationEnabled',{enabled:mobile,maxTouchPoints:5});
        async function waitFor(expression) {
          for(let i=0;i<150;i++){if(await page.eval(expression))return;await delay(100);}
          throw new Error(`Timed out: ${expression}`);
        }
        async function press(code,key,value) {
          await page.call('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:value});await delay(100);
          await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});await delay(100);
        }
        await page.call('Page.navigate',{url:base.href});
        await waitFor('document.querySelector("#status")?.textContent==="Launcher"');
        if(index===0) {
          await delay(100);
          const shot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
          fs.writeFileSync(path.join(out,`launcher-${mobile?'mobile':'desktop'}.png`),Buffer.from(shot.data,'base64'));
        }
        await page.eval('document.querySelector("#screen").focus()');
        if(game.role==='diagnostic'){
          await press('Escape','Escape',27);
          for(let i=0;i<=catalog.games.filter(g=>g.role==='diagnostic').findIndex(g=>g.id===game.id);i++)await press('ArrowDown','ArrowDown',40);
        }else for(let i=0;i<catalog.games.filter(g=>g.role==='game').findIndex(g=>g.id===game.id);i++)await press('ArrowDown','ArrowDown',40);
        await press('KeyX','x',88);
        await waitFor(`document.querySelector('#status')?.textContent===${JSON.stringify(game.name)} && new URL(location.href).searchParams.get('game')===${JSON.stringify(game.id)}`);
        assert.equal(await page.eval('document.querySelector("#status").textContent'),game.name);
        for(const ext of ['js','wasm'])assert.equal(responses.get(new URL(`${game.id}.${ext}`,base).href),200);
        await press('Enter','Enter',13);await delay(250);
        const size=await page.eval('(() => {const r=document.querySelector("#screen").getBoundingClientRect();return {width:r.width,height:r.height};})()');
        // DOMRects use floating-point bounds after the subpixel sampling bias.
        // Exact raster pixels are checked from screenshots in the layout suite.
        assert(Math.abs(size.width-Math.round(size.width/320)*320)<.001);
        assert(Math.abs(size.height-size.width*240/320)<.001);
        const shot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(path.join(out,`${label}.png`),Buffer.from(shot.data,'base64'));
        report.checks.push('selected through catalog launcher, JS/WASM HTTP 200, integer canvas scale');report.passed=true;
      } catch(error){report.failure=error.stack;console.error(`${label}: ${error.message}`);}
      finally {
        await page.call('Page.navigate',{url:'about:blank'}).catch(()=>{});page.close();
        await fetch(`${endpoint}/json/close/${target.id}`).catch(()=>{});
        fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(reports,null,2));
        console.log(JSON.stringify(report));
      }
    }
    if(process.argv.includes('--catalog-only')){assert(reports.every(r=>r.passed && !r.errors.length && !r.failedRequests.length),'Catalog checks failed');return;}
    const source = await (await fetch(new URL('player.js',base))).text();
    const readyLine = source.split('\n').findIndex(line => /id=nextId;/.test(line));
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
        await page.call('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:value});await delay(120);
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
        await page.call('Storage.clearDataForOrigin',{origin:base.origin,storageTypes:'local_storage'});
        await page.call('Page.navigate',{url:new URL(`?game=${game}`,base).href});await ready();
        report.initial=await state();assert.equal(report.initial.assetSounds,game==='phosphor-run'?6:7);
        assert(report.initial.callbacks.every(([,type])=>type==='function'));
        const title=await capture('title');assert.equal(title.touchVisible,mobile);
        if(mobile)await click('[data-button="4"]');else{await click('#screen');await key('KeyX','x',88);}
        if(game==='phosphor-run') {
          // The level introduction lasts 90 simulation ticks and consumes input.
          // Allow it to finish and the player to land before testing jump/dash audio.
          const targetTicks=(await state()).ticks+120;
          for(let i=0;i<150 && (await state()).ticks<targetTicks;i++)await delay(100);
          assert((await state()).ticks>=targetTicks,'Level introduction did not advance');
        }
        await delay(250);const playing=await capture('playing');assert.notEqual(playing.hash,title.hash);
        if(mobile){await click('[data-button="1"]',350);await click('[data-button="4"]');await click('[data-button="5"]');}
        else{await key('ArrowRight','ArrowRight',39,350);await key('KeyZ','z',90);await key('KeyX','x',88);}
        await delay(200);await capture('input');report.afterInput=await state();
        assert(report.afterInput.masks.some(mask=>mask&2));assert(report.afterInput.masks.some(mask=>mask&(1<<4)));
        assert.equal(report.afterInput.audioState,'running');assert(report.afterInput.assetPlays>0 && report.afterInput.audit.starts>0);
        report.checks.push('title, gameplay, directional/action input, decoded asset sounds played');
        await click('#screen');await key('Escape','Escape',27);
        assert((await state()).paused);const ticks=(await state()).ticks;await delay(250);assert.equal((await state()).ticks,ticks);
        await key('KeyZ','z',90);await delay(150);assert(!(await state()).paused && (await state()).ticks>ticks);
        report.checks.push('shared pause menu freezes updates and Secondary resumes');
        // Fullscreen is requested inside the shared Settings menu below. Enter
        // it here with the screen gesture to verify pause/return preserve it.
        const point=await page.eval(`(()=>{const r=document.querySelector('#screen').getBoundingClientRect();return {x:r.x+20,y:r.y+20};})()`);
        if(mobile){
          await click('#screen');await delay(60);await click('#screen');
        }else{
          await page.call('Input.dispatchMouseEvent',{type:'mousePressed',...point,button:'left',clickCount:2});
          await page.call('Input.dispatchMouseEvent',{type:'mouseReleased',...point,button:'left',clickCount:2});
        }
        await delay(200);
        assert.equal(await page.eval('document.fullscreenElement?.id'),'player');
        const original=await page.eval('globalThis.__originalCanvas=document.querySelector("#screen");globalThis.__originalGL=__originalCanvas.getContext("webgl");true');assert(original);
        if((await state()).screen===5)await key('KeyZ','z',90);
        await page.eval(`globalThis.__menuPad={id:'Standard test pad',mapping:'standard',buttons:Array.from({length:16},(_,i)=>({pressed:i===8})),axes:[0,0]};Object.defineProperty(navigator,'getGamepads',{configurable:true,value:()=>[__menuPad]});`);await delay(130);
        await page.eval('__menuPad.buttons[8].pressed=false');await delay(130);
        assert.equal((await state()).screen,5);
        await key('ArrowDown','ArrowDown',40);await key('KeyX','x',88);await delay(250);
        assert.equal((await state()).screen,0);assert.equal((await state()).id,'launcher');
        const unloaded=await state();
        assert.equal(unloaded.audit.texturesCreated-unloaded.audit.texturesDeleted,1,'Only the persistent launcher texture should remain');
        assert.equal(await page.eval('document.fullscreenElement?.id'),'player');
        assert(await page.eval('document.querySelector("#screen")===__originalCanvas && __originalCanvas.getContext("webgl")===__originalGL'));
        report.checks.push('in-console return keeps canvas, WebGL context and fullscreen alive');
        await key('Escape','Escape',27);assert.equal((await state()).screen,1);
        await key('KeyX','x',88);assert.equal((await state()).screen,2);
        await key('ArrowDown','ArrowDown',40);await key('KeyX','x',88);assert.equal((await state()).capture,1);
        // Cancel a partial draft. It must not reach browser storage.
        await key('KeyA','a',65);await key('F1','F1',112);assert.equal((await state()).capture,-1);
        assert.equal(await page.eval('localStorage.getItem("chirky.inputs.v1")'),null);
        await key('KeyX','x',88);
        await key('ArrowLeft','ArrowLeft',37);await key('ArrowLeft','ArrowLeft',37); // duplicate rejected
        for(const [code,k,v] of [['ArrowRight','ArrowRight',39],['ArrowUp','ArrowUp',38],['ArrowDown','ArrowDown',40],['KeyQ','q',81],['KeyZ','z',90],['Enter','Enter',13],['Escape','Escape',27]])await key(code,k,v);
        assert.equal((await state()).capture,-1);
        assert.deepEqual(await page.eval('JSON.parse(localStorage.getItem("chirky.inputs.v1")).keys'),['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','KeyQ','KeyZ','Enter','Escape']);
        const settingsShot=await page.call('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
        fs.writeFileSync(path.join(out,`${label}-settings.png`),Buffer.from(settingsShot.data,'base64'));
        // Raw USB gamepad: map button AND axis events through the actual C wizard.
        await key('ArrowUp','ArrowUp',38);await key('KeyQ','q',81);assert.equal((await state()).capture,0);
        await page.eval(`globalThis.__pad={id:'USB SNES test adapter',index:0,connected:true,mapping:'',buttons:Array.from({length:10},()=>({pressed:false,value:0})),axes:[0,0]};Object.defineProperty(navigator,'getGamepads',{configurable:true,value:()=>[__pad]});`);
        for(const [kind,code,direction] of [[2,0,-1],[2,0,1],[2,1,-1],[2,1,1],[1,1,0],[1,0,0],[1,9,0],[1,8,0]]){
          await page.eval(kind===2?`__pad.axes[${code}]=${direction}`:`__pad.buttons[${code}]={pressed:true,value:1}`);await delay(130);
          await page.eval(kind===2?`__pad.axes[${code}]=0`:`__pad.buttons[${code}]={pressed:false,value:0}`);await delay(130);
        }
        assert.equal((await state()).capture,-1);
        assert.equal(await page.eval('Object.values(JSON.parse(localStorage.getItem("chirky.controllers.v1")))[0].length'),8);
        // Secondary on this otherwise-unrecognized adapter returns through menus.
        for(let i=0;i<2;i++){
          await page.eval('__pad.buttons[0]={pressed:true,value:1}');await delay(130);
          await page.eval('__pad.buttons[0]={pressed:false,value:0}');await delay(130);
        }
        assert.equal((await state()).screen,0);
        await page.eval('Object.defineProperty(navigator,"getGamepads",{configurable:true,value:()=>[]})');
        // Settings includes browser capabilities, without Pi display placement.
        await key('Escape','Escape',27);
        const diagnostics=catalog.games.filter(g=>g.role==='diagnostic').length;
        for(let i=0;i<1+diagnostics;i++)await key('ArrowDown','ArrowDown',40);
        if(mobile){
          await click('[data-button="4"]');await delay(200);assert(!await page.eval('!!document.fullscreenElement'));
          // Prior gestures must not lend activation to the next touch-down.
          for(let i=0;i<70 && await page.eval('navigator.userActivation.isActive');i++)await delay(100);
          assert(!await page.eval('navigator.userActivation.isActive'));
          await click('[data-button="4"]',180);await delay(200);
          assert.equal(await page.eval('document.fullscreenElement?.id'),'player','Touch menu fullscreen must work after activation expires');
        }else{
          await key('KeyQ','q',81);await delay(200);assert(!await page.eval('!!document.fullscreenElement'));
          await key('KeyQ','q',81);await delay(200);assert.equal(await page.eval('document.fullscreenElement?.id'),'player');
        }
        await key('ArrowDown','ArrowDown',40);await key('KeyQ','q',81);assert((await state()).muted);
        assert.equal((await state()).activeSounds,0);await key('KeyQ','q',81);assert(!(await state()).muted);
        await key('KeyZ','z',90);
        await key('KeyQ','q',81); // selected game remains selected in persistent launcher
        for(let i=0;i<150 && (await state()).id==='launcher';i++)await delay(100);
        assert.equal((await state()).id,game);
        assert(await page.eval('document.querySelector("#screen")===__originalCanvas && __originalCanvas.getContext("webgl")===__originalGL'));
        assert.equal(await page.eval('document.fullscreenElement?.id'),'player');
        const full=await capture('fullscreen');assert(Math.abs(full.rect.width-Math.round(full.rect.width/320)*320)<.001);
        if(mobile){
          await page.call('Emulation.setDeviceMetricsOverride',{width:844,height:390,deviceScaleFactor:1,mobile:true});await delay(200);
          const landscape=await capture('fullscreen-landscape');assert(Math.abs(landscape.rect.width-Math.round(landscape.rect.width/320)*320)<.001);
          assert(await page.eval('Array.from(document.querySelectorAll(".touch button")).every(e=>{const r=e.getBoundingClientRect();return r.left>=0 && r.top>=0 && r.right<=innerWidth && r.bottom<=innerHeight;})'),'Fullscreen touch controls must stay onscreen');
          const pad=await page.eval(`(()=>{const r=document.querySelector('.dpad').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
          const action=await page.eval(`(()=>{const r=document.querySelector('[data-button="4"]').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,id:2};})()`);
          const thumb=(x,y)=>({x:pad.x+x,y:pad.y+y,id:1});
          await page.call('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[thumb(-70,0)]});
          assert.equal((await state()).inputs,1,'The area just outside the visible cross accepts a thumb');
          await page.call('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[thumb(-70,0),action]});
          await page.eval(`document.querySelector('.dpad').addEventListener('pointermove',event=>{globalThis.__lastDpadMove={x:event.clientX,y:event.clientY};})`);
          async function moveThumb(x,y){
            const point=thumb(x,y);
            await page.eval('globalThis.__lastDpadMove=null');
            await page.call('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[point,action]});
            // CDP can acknowledge before Chrome delivers its coalesced move.
            // Wait for the DOM event, then independently assert the input mask.
            let delivered=false;
            for(let i=0;i<40;i++){
              delivered=await page.eval(`!!globalThis.__lastDpadMove && Math.abs(__lastDpadMove.x-${point.x})<.5 && Math.abs(__lastDpadMove.y-${point.y})<.5`);
              if(delivered)break;
              await delay(50);
            }
            assert(delivered,'Chrome must deliver the requested D-pad pointer move');
          }
          for(const [x,y,bits] of [[-44,-44,5],[0,-44,4],[44,-44,6],[44,0,2],[44,44,10],[0,44,8],[-44,44,9],[-44,0,1],[0,0,0],[100,0,2]]){
            await moveThumb(x,y);
            assert.equal((await state()).inputs,bits|16,'A captured thumb steers while the other holds Primary');
          }
          await moveThumb(-44,-44);
          await capture('diagonal-touch');
          // For a partial touchEnd, CDP takes the contact being released.
          await page.call('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[thumb(-44,-44)]});
          assert.equal((await state()).inputs,16,'Releasing the D-pad preserves the action finger');
          await page.call('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
          assert.equal((await state()).inputs,0);
          await page.call('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[thumb(44,44)]});
          await page.call('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});
          assert.equal((await state()).inputs,0,'Touch cancellation releases both directions');
          report.checks.push('sliding eight-way D-pad, neutral centre, forgiving edge, capture outside pad, concurrent action and cancellation');
          await page.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});await delay(150);
        }
        report.checks.push('shared keyboard/USB mapping wizard, duplicate rejection, cancel, controller navigation, fullscreen setting, mute');
        // A fresh module in the same page has its title/audio/assets restored.
        assert.equal((await state()).assetSounds,report.initial.assetSounds);
        assert.equal(report.initial.audit.programsCreated,2,'The renderer has rectangle and sprite programs');
        assert.equal((await state()).audit.programsCreated,report.initial.audit.programsCreated,'The renderer must be initialized only once');
        assert.equal((await state()).audit.programsDeleted,0,'Game switches must preserve the renderer');
        if(game==='phosphor-run' && !mobile) {
          const pendingGame=catalog.games.find(entry=>entry.role==='game' && entry.id!==game);
          let heldRequest;
          const hold=p=>{heldRequest=p.requestId;};page.on('Fetch.requestPaused',hold);
          await page.call('Fetch.enable',{patterns:[{urlPattern:`*${pendingGame.id}.wasm`,requestStage:'Request'}]});
          await page.eval(`__platformProbe.runtime._web_console_launch(${catalog.games.indexOf(pendingGame)})`);
          for(let i=0;i<100 && !heldRequest;i++)await delay(50);
          assert(heldRequest,'Game WASM download should be pending');
          await key('F1','F1',112);assert.equal((await state()).id,'launcher');
          await page.call('Fetch.continueRequest',{requestId:heldRequest});
          await page.call('Fetch.disable');page.off('Fetch.requestPaused',hold);await delay(300);
          assert.equal((await state()).id,'launcher');assert.equal((await state()).loading,false);
          report.checks.push('cancelled WASM download cannot activate a game later');
          // This test exercises module lifetime without a LAN world director.
          // The director transport has its own integration suite.
          await page.eval('globalThis.__connectDirector=__platformProbe.runtime.onDirectorConnect;__platformProbe.runtime.onDirectorConnect=()=>false');
          // Different C modules export overlapping globals. Exercise all of them
          // twice in one host to catch symbol collisions and stale game resources.
          for(let cycle=0;cycle<2;cycle++)for(const entry of catalog.games) {
            await key('F1','F1',112);
            await page.eval(`__platformProbe.runtime._web_console_launch(${catalog.games.indexOf(entry)})`);
            for(let i=0;i<150 && ((await state()).loading || (await state()).id!==entry.id);i++)await delay(100);
            assert.equal((await state()).id,entry.id);
            assert.equal((await state()).audit.programsCreated,report.initial.audit.programsCreated);
            assert.equal((await state()).audit.programsDeleted,0);
            assert(await page.eval('__platformProbe.runtime===__platformProbe.shell && document.querySelector("#screen")===__originalCanvas'));
            await capture(`switch-${cycle}-${entry.id}`);
            await key('KeyQ','q',81);await delay(100);
            await key('F1','F1',112);
            const destination=entry.role==='diagnostic'?1:0;
            assert.equal((await state()).screen,destination);
            assert.equal((await state()).audit.texturesCreated-(await state()).audit.texturesDeleted,destination===0?1:0,
              'Only the launcher needs a texture; Settings uses rectangles');
          }
          await page.eval('__platformProbe.runtime.onDirectorConnect=__connectDirector');
          await page.eval(`__platformProbe.runtime._web_console_launch(${catalog.games.findIndex(entry=>entry.id===game)})`);
          for(let i=0;i<150 && (await state()).id!==game;i++)await delay(100);
          assert.equal((await state()).id,game);
          report.checks.push('all games twice in one host, isolated module symbols, one renderer and balanced textures');
        }
        await page.call('Page.reload');await delay(200);await ready();
        assert.equal(await page.eval('JSON.parse(localStorage.getItem("chirky.inputs.v1")).keys[4]'),'KeyQ');
        assert.equal(await page.eval('Object.values(JSON.parse(localStorage.getItem("chirky.controllers.v1"))).length'),1);
        await capture('restarted');
        report.lifecycle=await page.eval('JSON.parse(sessionStorage.getItem("platform-browser-lifecycle")||"[]")');
        assert(report.lifecycle.filter(e=>e.event==='destroy-end').length>=2);
        report.checks.push('module teardown and relaunch; keyboard/controller bindings survive reload');
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
if(require.main===module)main().catch(error=>{console.error(error);process.exitCode=1;});
module.exports={CDP,instrumentation,attachProbe};
