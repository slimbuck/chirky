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
    navigator:{getGamepads:()=>[]},fetch:()=>new Promise(()=>{}),URL,TextEncoder,AudioContext:class{resume(){return Promise.resolve();}}});
  const run=code=>vm.runInContext(code,context);
  run(fs.readFileSync(require.resolve('../web/player.js'),'utf8'));
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
