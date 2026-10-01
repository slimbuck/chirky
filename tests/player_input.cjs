const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const test=require('node:test');
function player(saved){
  const elements=new Map(),storage=new Map(saved?[['chirky.inputs.v1',saved]]:[]),events={};
  function element(selector){
    if(!elements.has(selector))elements.set(selector,{dataset:{},listeners:{},style:{},
      addEventListener(name,fn){this.listeners[name]=fn;},setAttribute(){},focus(){}});
    return elements.get(selector);
  }
  const touch=Array.from({length:8},(_,i)=>Object.assign(element('touch'+i),{dataset:{button:String(i)},setPointerCapture(){}}));
  Object.assign(element('.dpad'),{setPointerCapture(){},getBoundingClientRect:()=>({left:10,top:10,width:132,height:132})});
  const document={querySelector:element,querySelectorAll:()=>touch,addEventListener:(name,fn)=>{events[name]=fn;}};
  const window={addEventListener:(name,fn)=>{events[name]=fn;}};
  const localStorage={getItem:key=>storage.get(key),setItem:(key,value)=>storage.set(key,value)};
  const context=vm.createContext({document,window,localStorage,location:{search:''},URLSearchParams,console,
    navigator:{getGamepads:()=>[]},fetch:()=>new Promise(()=>{}),URL,TextEncoder,atob,btoa,AudioContext:class{resume(){return Promise.resolve();}}});
  const run=code=>vm.runInContext(code,context);
  run(fs.readFileSync(require.resolve('../web/shell.js'),'utf8'));
  run(fs.readFileSync(require.resolve('../web/player.js'),'utf8').replace('start().catch(startupFailed);',''));
  return {run,element,touch,events,storage,document,localStorage};
}
test('cancelled browser loads cannot activate after a newer request, and errors return to the console',async()=>{
  const p=player();
  p.run(`
    catalog=[{id:'sample',name:'Sample'}];files=[];configs={};
    navigation={push:false,level:null};
    globalThis.calls=[];
    gameDescriptor=async()=>({module:'sample.wasm',config:'games/sample/game.conf'});
    checked=async()=>({arrayBuffer:async()=>new ArrayBuffer(8)});
    runtime=shell={FS:{writeFile(){}},
      ccall:(...args)=>{calls.push(args);},_web_load_failed:()=>{calls.push('failed');gameStopped();}};
    console={warn(){},error(){}};
  `);
  const first=p.run('loadGame("sample")');
  for(let i=0;i<10 && !p.run('calls.length');i++)await new Promise(resolve=>setImmediate(resolve));
  assert.equal(p.run('calls.length'),1);
  p.run('gameStopped();navigation={push:false,level:null}');
  const second=p.run('loadGame("sample")');
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(p.run('calls.length'),1,'A second request waits for the cancelled loader callback');
  p.run('onModuleLoaded(false)');await first;
  for(let i=0;i<10 && p.run('calls.length')<2;i++)await new Promise(resolve=>setImmediate(resolve));
  assert.equal(p.run('id'),'launcher');
  p.run('onModuleLoaded(true)');await second;
  assert.equal(p.run('calls.length'),2);assert.equal(p.run('id'), 'sample');assert.equal(p.run('loading'),false);
  p.run('gameStopped();gameDescriptor=async()=>{throw Error("Download failed");}');
  await p.run('loadGame("sample")');
  assert.equal(p.run('calls.at(-1)'),'failed');assert.equal(p.run('id'),'launcher');
  assert.equal(p.run('loading'),false);assert.equal(p.element('#status').textContent,'Download failed');
});
test('logical keys, standard gamepads, raw USB axes and buttons',()=>{
  const p=player('{bad json');assert.equal(p.run('Object.keys(bindings).length'),8);
  assert.equal(p.run('bindings.KeyX'),4);assert.equal(p.run('bindings.KeyZ'),5);
  p.run('keys.add("Unknown");keys.add("KeyX")');assert.equal(p.run('mask()'),16);
  p.run('keys.clear();navigator.getGamepads=()=>[{id:"standard",mapping:"standard",buttons:Array.from({length:16},(_,i)=>({pressed:i===0||i===2||i===9})),axes:[0,0]}]');
  assert.equal(p.run('mask()'),(1<<4)|(1<<5)|(1<<6));
  p.run('navigator.getGamepads=()=>[{id:"USB SNES",mapping:"",buttons:Array.from({length:10},(_,i)=>({pressed:i===1})),axes:[-1,0]}]');
  assert.equal(p.run('mask()'),0,'unrecognized pads require explicit mapping');
  p.run('mappingPad="USB SNES|";var mapping=[{kind:2,code:0,direction:-1},{kind:2,code:0,direction:1},{kind:2,code:1,direction:-1},{kind:2,code:1,direction:1},... [1,0,9,8].map(code=>({kind:1,code,direction:0}))]');
  assert(p.run('onSaveMapping(false,mapping)'));assert.equal(p.run('mask()'),17);
  p.run('navigator.getGamepads=()=>[]');assert.equal(p.run('mask()'),0,'disconnect releases inputs');
  assert.match(p.storage.get('chirky.controllers.v1'),/USB SNES/);
});
test('mapping commits atomically, rejects duplicates and preserves old bindings if storage fails',()=>{
  const p=player();
  p.run('var values=defaultKeys.map(code=>({kind:1,code:keyCodes.indexOf(code),direction:0}));values[4].code=keyCodes.indexOf("KeyQ")');
  assert(p.run('onSaveMapping(true,values)'));assert.equal(p.run('bindings.KeyQ'),4);
  const next=player(p.storage.get('chirky.inputs.v1'));assert.equal(next.run('bindings.KeyQ'),4);
  assert.equal(p.run('onSaveMapping(true,values.map(()=>values[0]))'),false);
  p.localStorage.setItem=()=>{throw Error('blocked');};
  assert.equal(p.run('values[4].code=keyCodes.indexOf("KeyR");onSaveMapping(true,values)'),false);
  assert.equal(p.run('bindings.KeyQ'),4);assert.equal(p.run('bindings.KeyR'),undefined);
});
test('touch, release and focus loss do not leave stuck buttons',()=>{
  const p=player(),pad=p.element('.dpad'),down=id=>({pointerId:id,clientX:10,clientY:76,preventDefault(){}});
  pad.onpointerdown(down(1));p.touch[4].onpointerdown(down(2));assert.equal(p.run('mask()'),17);
  assert.equal(p.touch[0].dataset.pressed,'true');assert.equal(p.touch[4].dataset.pressed,'true');
  pad.onpointercancel({pointerId:1});assert.equal(p.run('mask()'),16);
  assert.equal(p.touch[0].dataset.pressed,'false');assert.equal(p.touch[4].dataset.pressed,'true');
  p.touch[4].onlostpointercapture({pointerId:2});assert.equal(p.run('mask()'),0);
  p.touch[4].onpointerdown(down(3));p.touch[4].onpointerdown(down(4));
  p.touch[4].onpointerup({pointerId:3});assert.equal(p.touch[4].dataset.pressed,'true');
  pad.onpointerdown(down(5));
  p.run('runtime={};keys.add("KeyX");pending=16');p.events.blur();assert.equal(p.run('mask()|pending|dpadPending'),0);assert.equal(p.run('paused'),true);
  pad.onpointermove({...down(5),clientX:150});assert.equal(p.run('mask()'),0);
  assert.equal(p.touch[4].dataset.pressed,'false');
});
test('game transitions preserve held raw inputs for the shared release gate',()=>{
  const p=player();
  p.run('keys.add("KeyX");touch.set(1,1);pending=16;gameStopped()');
  assert.equal(p.run('mask()'),17);assert.equal(p.run('pending'),0);
});

test('sliding D-pad supports eight directions, neutral, nearby starts and concurrent actions',()=>{
  const p=player(),pad=p.element('.dpad');
  const at=(x,y,id=1)=>({pointerId:id,clientX:76+x,clientY:76+y,preventDefault(){}});
  pad.onpointerdown(at(-70,0));assert.equal(p.run('mask()'),1,'Start in the forgiving margin');
  p.touch[4].onpointerdown(at(0,0,2));
  for(const [x,y,bits] of [[-44,-44,5],[0,-44,4],[44,-44,6],[44,0,2],[44,44,10],[0,44,8],[-44,44,9],[-44,0,1],[0,0,0],[6,6,0],[200,0,2]]){
    pad.onpointermove(at(x,y));assert.equal(p.run('mask()'),bits|16);
    assert.equal(p.run('dpadPending'),bits,'Sliding replaces previous directions between frames');
    for(let i=0;i<4;i++)assert.equal(p.touch[i].dataset.pressed,String(!!(bits&(1<<i))));
  }
  pad.onpointerdown(at(-44,0,3));pad.onpointermove(at(-44,0,3));pad.onpointerup(at(-44,0,3));
  assert.equal(p.run('mask()'),18,'Another finger must not steal steering');
  pad.onpointerup(at(200,0));assert.equal(p.run('mask()'),16);
  pad.onlostpointercapture(at(200,0));assert.equal(p.run('mask()'),16);
  p.touch[4].onpointerup(at(0,0,2));assert.equal(p.run('mask()'),0);
});

test('D-pad cancellation clears queued direction and capture loss permits a fresh gesture',()=>{
  const p=player(),pad=p.element('.dpad');
  const down={pointerId:1,clientX:20,clientY:20,preventDefault(){}};
  pad.onpointerdown(down);assert.equal(p.run('mask()|dpadPending'),5);
  pad.onlostpointercapture(down);assert.equal(p.run('mask()|dpadPending'),0);
  pad.onpointerdown({...down,pointerId:2});assert.equal(p.run('mask()'),5);
  pad.onpointercancel({pointerId:2});assert.equal(p.run('mask()|dpadPending'),0);
});
test('fullscreen targets the persistent console container',async()=>{
  const p=player();let entered=0,exited=0;
  p.element('#player').requestFullscreen=async()=>{entered++;p.document.fullscreenElement=p.element('#player');};
  p.document.exitFullscreen=async()=>{exited++;p.document.fullscreenElement=null;};
  await p.run('toggleFullscreen()');await p.run('toggleFullscreen()');assert.equal(entered,1);assert.equal(exited,1);
});

test('touch fullscreen waits for release, handles quick taps and cancels interrupted gestures',async()=>{
  const p=player();let calls=0;
  p.element('#player').requestFullscreen=async()=>{calls++;};
  const down=id=>({pointerId:id,preventDefault(){}});
  p.touch[4].onpointerdown(down(1));p.run('requestConsoleFullscreen()');assert.equal(calls,0);
  p.touch[4].onpointerup({pointerId:1});assert.equal(calls,1,'Held menu action runs during release');
  await new Promise(resolve=>setImmediate(resolve));
  p.touch[4].onpointerdown(down(2));p.run('requestConsoleFullscreen()');
  p.touch[4].onpointercancel({pointerId:2});p.run('consoleGesture()');assert.equal(calls,1);
  p.run('loading=false;shell={_web_console_state:()=>1,_web_console_tick:()=>requestConsoleFullscreen()}');
  p.touch[4].onpointerdown(down(3));p.touch[4].onpointerup({pointerId:3});
  assert.equal(calls,2,'A tap between animation frames runs the shared menu inside the gesture');
  await new Promise(resolve=>setImmediate(resolve));
  p.run('shell={_web_console_state:()=>4,_web_console_tick:()=>{throw Error("Unexpected game tick")}}');
  p.touch[4].onpointerdown(down(4));p.touch[4].onpointerup({pointerId:4});assert.equal(calls,2);
});

test('screen double-tap toggles once and ignores the compatibility double-click',async()=>{
  const p=player(),screen=p.element('#screen');let calls=0;
  p.element('#player').requestFullscreen=async()=>{calls++;};
  const event=timeStamp=>({pointerId:1,pointerType:'touch',isPrimary:true,clientX:20,clientY:20,timeStamp,preventDefault(){}});
  screen.listeners.pointerdown(event(100));screen.listeners.pointerup(event(150));assert.equal(calls,0);
  screen.listeners.pointerdown(event(250));screen.listeners.pointerup(event(300));assert.equal(calls,1);
  await new Promise(resolve=>setImmediate(resolve));screen.listeners.dblclick(event(310));assert.equal(calls,1);
  screen.listeners.pointerdown(event(1200));screen.listeners.pointercancel();screen.listeners.pointerup(event(1250));
  screen.listeners.pointerdown(event(1350));screen.listeners.pointerup(event(1400));assert.equal(calls,1);
});

test('prefixed fullscreen and rejection messages preserve the game title',async()=>{
  const p=player();let entered=0,exited=0;
  p.element('#status').textContent='Launcher';
  p.element('#player').webkitRequestFullscreen=()=>{entered++;p.document.webkitFullscreenElement=p.element('#player');};
  p.document.webkitExitFullscreen=()=>{exited++;p.document.webkitFullscreenElement=null;};
  await p.run('toggleFullscreen()');await p.run('toggleFullscreen()');assert.equal(entered,1);assert.equal(exited,1);
  p.element('#player').webkitRequestFullscreen=undefined;
  await p.run('toggleFullscreen()');assert.equal(p.element('#fullscreen-message').hidden,false);
  assert.match(p.element('#fullscreen-message').textContent,/does not support/);
  p.element('#player').requestFullscreen=async()=>{throw Error('denied');};
  await p.run('toggleFullscreen()');assert.match(p.element('#fullscreen-message').textContent,/Double-tap/);
  assert.equal(p.element('#status').textContent,'Launcher');
});

test('iOS fullscreen failures offer Home Screen launch and standalone avoids a failing request',async()=>{
  const p=player();
  p.run('navigator.userAgent="Mozilla/5.0 (iPhone; CPU iPhone OS 18_6 like Mac OS X)"');
  await p.run('toggleFullscreen()');
  assert.match(p.element('#fullscreen-message').textContent,/Add to Home Screen/);
  let calls=0;
  p.element('#player').requestFullscreen=async()=>{calls++;throw Error('unsupported');};
  await p.run('toggleFullscreen()');assert.equal(calls,1);
  assert.match(p.element('#fullscreen-message').textContent,/Add to Home Screen/);
  p.run('navigator.standalone=true');
  await p.run('toggleFullscreen()');assert.equal(calls,1);
  assert.match(p.element('#fullscreen-message').textContent,/already running/);
  p.run('navigator.standalone=false;navigator.userAgent="Macintosh";navigator.platform="MacIntel";navigator.maxTouchPoints=5');
  await p.run('toggleFullscreen()');
  assert.match(p.element('#fullscreen-message').textContent,/Add to Home Screen/,'iPad desktop user agents get the same guidance');
});

test('page-scale gestures are cancelled without consuming single-touch movement or held inputs',()=>{
  const p=player();let prevented=0;
  const event={cancelable:true,preventDefault(){prevented++;}};
  p.touch[4].onpointerdown({pointerId:1,preventDefault(){}});
  p.events.gesturestart(event);p.events.gesturechange(event);
  p.events.touchmove({...event,touches:[{},{}]});assert.equal(prevented,3);
  p.events.touchmove({...event,touches:[{}]});assert.equal(prevented,3);
  p.events.gesturechange({...event,cancelable:false});assert.equal(prevented,3);
  assert.equal(p.run('mask()'),16);
  p.touch[4].onpointerup({pointerId:1});assert.equal(p.run('mask()'),0);
});

test('keyboard keycaps follow saved mappings and release their pressed state',()=>{
  const p=player(JSON.stringify({version:1,keys:['KeyA','KeyD','KeyW','KeyS','Space','ControlRight','Enter','Escape'],touch:'auto'}));
  assert.equal(p.element('#key-help-2').textContent,'W');
  assert.equal(p.element('#key-help-4').textContent,'Space');
  assert.equal(p.element('#key-help-5').textContent,'Ctrl R');
  p.events.keydown({code:'Space',preventDefault(){}});
  assert.equal(p.element('#key-help-4').dataset.pressed,'true');
  p.events.keyup({code:'Space'});
  assert.equal(p.element('#key-help-4').dataset.pressed,'false');
  p.events.keydown({code:'Space',preventDefault(){}});p.run('setPaused(true)');
  assert.equal(p.element('#key-help-4').dataset.pressed,'false');
  p.run('inputSettings.keys[4]="KeyQ";refreshInputSettings()');
  assert.equal(p.element('#key-help-4').textContent,'Q');
});

test('startup percentage follows downloaded bytes, reserves initialization, and never goes backwards',async()=>{
  const p=player();
  p.run(`
    globalThis.progress=[];const reportProgress=startupProgress;
    startupProgress=value=>{reportProgress(value);progress.push(startupPercent);};
    checked=async()=>({headers:{get:name=>name==='Content-Length'?'8':null},body:{getReader(){
      let part=0;return {async read(){return ++part<=2?{value:new Uint8Array(4).fill(part),done:false}:{done:true};}};
    }}});
  `);
  const bytes=await p.run('startupBinary("launcher.wasm")');
  assert.deepEqual(Array.from(bytes),[1,1,1,1,2,2,2,2]);
  assert.deepEqual(Array.from(p.run('progress')),[47,75,75]);
  assert.equal(p.element('#startup-percent').textContent,'75%');
  p.run('startupProgress(20)');assert.equal(p.run('startupPercent'),75);
  p.run('startupProgress(100)');assert.equal(p.element('#startup-percent').textContent,'100%');
});

test('compressed or unknown byte totals do not produce invented download percentages',async()=>{
  for(const encoded of [false,true]){
    const p=player();
    p.run(`
      startupProgress(20);globalThis.during=[];
      checked=async()=>({headers:{get:name=>name==='Content-Length'?${encoded?"'2'":"null"}:${encoded?"'gzip'":"null"}},body:{getReader(){
        let part=0;return {async read(){during.push(startupPercent);return ++part<=2?{value:new Uint8Array(4),done:false}:{done:true};}};
      }}});
    `);
    await p.run('startupBinary("launcher.wasm")');
    assert.deepEqual(Array.from(p.run('during')),[20,20,20]);
    assert.equal(p.run('startupPercent'),75);
  }
});

test('startup failure keeps progress unfinished and explains how to retry inside the display',()=>{
  const p=player();p.run('console={error(){}};startupProgress(20);startupFailed(new Error("HTTP 503"))');
  assert.equal(p.run('startupPercent'),20);
  assert.equal(p.element('#startup-error').hidden,false);
  assert.match(p.element('#startup-error').textContent,/Reload/);
  assert.equal(p.element('#status').textContent,'HTTP 503');
});

test('game saves retain bytes across player instances and reject invalid or unavailable storage',()=>{
  const p=player();assert(p.run('onSaveWrite("phosphor-run","scores-relay-shaft",new Uint8Array([0,127,255]))'));
  const next=player();for(const [key,value] of p.storage)next.storage.set(key,value);
  assert.deepEqual(Array.from(next.run('onSaveRead("phosphor-run","scores-relay-shaft")')),[0,127,255]);
  assert.equal(next.run('onSaveRead("another-game","scores-relay-shaft")'),null);
  assert.equal(p.run('onSaveWrite("../bad","scores",new Uint8Array([1]))'),false);
  assert.equal(p.run('onSaveWrite("phosphor-run","scores",new Uint8Array(65537))'),false);
  next.localStorage.setItem=()=>{throw Error('quota');};
  assert.equal(next.run('onSaveWrite("phosphor-run","scores",new Uint8Array([1]))'),false);
  p.storage.set('chirky.save.v1.phosphor-run.bad','not base64!');assert.equal(p.run('onSaveRead("phosphor-run","bad")'),null);
});
