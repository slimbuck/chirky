const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const test=require('node:test');
function player(saved,controllers={}){
  const elements=new Map(),storage=new Map([...Object.entries(controllers),...(saved?[['chirky.inputs.v1',saved]]:[])]),events={};
  function element(selector){
    if(!elements.has(selector))elements.set(selector,{dataset:{},listeners:{},style:{},
      addEventListener(name,fn){this.listeners[name]=fn;},setAttribute(){},focus(){}});
    return elements.get(selector);
  }
  const touch=Array.from({length:8},(_,i)=>Object.assign(element('touch'+i),{dataset:{button:String(i)},setPointerCapture(){}}));
  Object.assign(element('.dpad'),{setPointerCapture(){},getBoundingClientRect:()=>({left:10,top:10,width:132,height:132})});
  const document={querySelector:element,querySelectorAll:()=>touch,addEventListener:(name,fn)=>{events[name]=fn;}};
  const window={addEventListener:(name,fn)=>{events[name]=fn;}};
  const localStorage={getItem:key=>storage.get(key) ?? null,setItem:(key,value)=>storage.set(key,value)};
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
  assert.equal(p.run('bindings.KeyN'),4);assert.equal(p.run('bindings.KeyM'),5);
  p.run('keys.add("Unknown");keys.add("KeyN")');assert.equal(p.run('mask()'),16);
  p.run('keys.clear();navigator.getGamepads=()=>[{id:"standard",mapping:"standard",buttons:Array.from({length:16},(_,i)=>({pressed:i===0||i===2||i===9})),axes:[0,0]}]');
  assert.equal(p.run('mask()'),(1<<4)|(1<<5)|(1<<6));
  p.run('navigator.getGamepads=()=>[{id:"USB SNES",mapping:"",buttons:Array.from({length:10},(_,i)=>({pressed:i===1})),axes:[-1,0]}]');
  assert.equal(p.run('mask()'),17,'new raw controllers use the SNES preset immediately');
  p.run('localDevices();var controllerId=localDevices()[3].id;var mapping=[{kind:2,code:0,direction:-1},{kind:2,code:0,direction:1},{kind:2,code:1,direction:-1},{kind:2,code:1,direction:1},... [1,0,9,8].map(code=>({kind:1,code,direction:0}))]');
  assert(p.run('onSaveController(controllerId,0,mapping)'));assert.equal(p.run('mask()'),17);
  p.run('navigator.getGamepads=()=>[]');assert.equal(p.run('mask()'),0,'disconnect releases inputs');
  assert.match(p.storage.get('chirky.controllers.v2'),/USB SNES/);
});
test('physical labels follow remaps and the last pressed device without changing logical inputs',()=>{
  const p=player();
  assert.equal(p.run('onButtonLabel(4)'), 'N');
  assert.equal(p.run('onButtonLabel(0)'), '←');
  assert.equal(p.run('onButtonLabel(2)'), '↑');
  p.run('var values=defaultKeys.map(code=>({kind:1,code:keyCodes.indexOf(code),direction:0}));values[4].code=keyCodes.indexOf("KeyQ");onSaveKeyboard(values)');
  assert.equal(p.run('onButtonLabel(4)'), 'Q');
  p.run('var pad={index:0,id:"standard",mapping:"standard",buttons:Array.from({length:16},(_,i)=>({pressed:i===0})),axes:[0,0]};navigator.getGamepads=()=>[pad];updateLabelDevice()');
  assert.equal(p.run('onButtonLabel(4)'), 'B');
  assert.equal(p.run('onButtonLabel(0)'), '←');
  assert.equal(p.run('mask()'),16);
  p.events.keydown({code:'KeyQ',preventDefault(){}});
  p.run('updateLabelDevice()');
  assert.equal(p.run('onButtonLabel(4)'), 'Q', 'a held pad does not steal keyboard prompts');
  p.touch[4].onpointerdown({pointerId:1,preventDefault(){}});
  p.run('updateLabelDevice()');
  assert.equal(p.run('onButtonLabel(4)'), 'A');
  assert.equal(p.run('onButtonLabel(5)'), 'B');
  p.run('pad.buttons[2].pressed=true;updateLabelDevice();localDevices();onSaveController(localDevices()[3].id,0,[14,15,12,13,1,2,9,8].map(code=>({kind:1,code,direction:0})))');
  assert.equal(p.run('onButtonLabel(4)'), 'EAST', 'remapping changes the physical legend');
  p.run('pad.id="unknown adapter";pad.mapping="";updateLabelDevice();localDevices();onSaveController(localDevices()[3].id,0,[14,15,12,13,1,2,9,8].map(code=>({kind:1,code,direction:0})))');
  p.run('pad.buttons[1].pressed=true;updateLabelDevice()');
  assert.equal(p.run('onButtonLabel(4)'), 'BTN 1', 'unknown USB legends are not guessed');
  assert.equal(p.run('onButtonLabel(-1)'), 'UNBOUND');
  p.run('navigator.getGamepads=()=>[];updateLabelDevice()');
  assert.equal(p.run('onButtonLabel(4)'), 'Q');
});

test('local players use distinct connection IDs, mapped labels and independent short taps',()=>{
  const p=player();
  p.run(`var a={index:0,id:'same',mapping:'standard',buttons:Array.from({length:16},()=>({pressed:false})),axes:[0,0]};
    var b={...a,index:1,buttons:Array.from({length:16},()=>({pressed:false}))};navigator.getGamepads=()=>[a,b];
    var devices=localDevices();var aid=devices[3].id,bid=devices[4].id;`);
  assert.notEqual(p.run('aid'),p.run('bid'));
  p.run('a.buttons[0].pressed=true;b.buttons[14].pressed=true;devices=localDevices()');
  assert.deepEqual(Array.from(p.run('devices.map(d=>d.mask)')),[0,0,0,16,1]);
  assert.equal(p.run('onDeviceLabel(aid,4)'), 'B');
  assert.equal(p.run('onDeviceLabel(1,4)'), 'N');
  assert.equal(p.run('onDeviceLabel(2,4)'), 'A');
  p.events.keydown({code:'KeyN',preventDefault(){}});p.events.keyup({code:'KeyN'});
  assert.equal(p.run('localDevices()[0].pending'),16);
  assert.equal(p.run('localDevices()[0].mask'),0);
  p.run('navigator.getGamepads=()=>[b];localDevices();navigator.getGamepads=()=>[a,b];devices=localDevices()');
  assert.notEqual(p.run('devices[3].id'),p.run('aid'));
  assert.equal(p.run('devices[4].id'),p.run('bid'));
  p.events.gamepaddisconnected({gamepad:{index:1}});
  assert.notEqual(p.run('localDevices()[4].id'),p.run('bid'),'reused browser index gets a new connection ID');
});

test('controller profiles share by model, isolate connections and preserve SNES legends through remapping',()=>{
  const p=player();
  p.run(`var a={index:0,id:'SNES',mapping:'',buttons:Array.from({length:10},()=>({pressed:false})),axes:[0,0]};
    var b={...a,index:1,axes:[0,0],buttons:Array.from({length:10},()=>({pressed:false}))};
    var c={...b,index:2,id:'different',axes:[0,0]};navigator.getGamepads=()=>[a,b,c];
    var controllerIds=localDevices().slice(3).map(d=>d.id);`);
  assert.equal(p.run('onSaveController(controllerIds[0],1,null)'),true);
  assert.equal(p.run('onDeviceLabel(controllerIds[1],4)'),'B');
  assert.equal(p.run('onDeviceLabel(controllerIds[2],4)'),'B');
  p.run('a.buttons[1].pressed=true;b.axes[0]=-1');
  assert.deepEqual(Array.from(p.run('localDevices().slice(3).map(d=>d.mask)')),[16,1,0]);
  p.run('var remap=snesPreset(a);[remap[4],remap[5]]=[remap[5],remap[4]]');
  assert.equal(p.run('onSaveController(controllerIds[0],1,remap)'),true);
  assert.equal(p.run('onDeviceLabel(controllerIds[0],4)'),'Y');
  assert.equal(p.run('onDeviceLabel(controllerIds[1],4)'),'Y');
  const stored=p.storage.get('chirky.controllers.v2');
  p.localStorage.setItem=()=>{throw Error('full');};
  assert.equal(p.run('onSaveController(controllerIds[0],0,snesPreset(a))'),false);
  assert.equal(p.run('onDeviceLabel(controllerIds[0],4)'),'Y');
  p.run('navigator.getGamepads=()=>[b,c];localDevices()');
  assert.equal(p.run('onSaveController(controllerIds[0],1,null)'),false);
  p.run('navigator.getGamepads=()=>[a,b,c];localDevices()');
  assert.notEqual(p.run('localDevices()[3].id'),p.run('controllerIds[0]'));
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,4)'),'Y');
  const restored=player(null,{'chirky.controllers.v2':stored});
  restored.run(`var pad={index:0,id:'SNES',mapping:'',buttons:Array.from({length:10},()=>({pressed:false})),axes:[0,0]};navigator.getGamepads=()=>[pad];localDevices()`);
  assert.equal(restored.run('onDeviceLabel(localDevices()[3].id,4)'),'Y');
});

test('standard SNES preset and legacy generic profiles use their respective physical labels',()=>{
  const map=[14,15,12,13,0,2,9,8].map(code=>({kind:1,code,direction:0}));
  const p=player(null,{'chirky.controllers.v1':JSON.stringify({'standard|standard':map})});
  p.run(`var pad={index:0,id:'standard',mapping:'standard',buttons:Array.from({length:16},()=>({pressed:false})),axes:[0,0]};navigator.getGamepads=()=>[pad];localDevices()`);
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,4)'),'SOUTH');
  assert.equal(p.run('onSaveController(localDevices()[3].id,1,null)'),true);
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,4)'),'B');
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,5)'),'Y');
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,0)'),'←');
  assert.equal(p.run('onSaveController(localDevices()[3].id,0,padProfile(pad).bindings)'),true);
  assert.equal(p.run('onDeviceLabel(localDevices()[3].id,4)'),'SOUTH');
});

test('new controllers default to SNES and can be selected before saving any settings',()=>{
  const p=player();
  p.run(`var pad={index:0,id:'new adapter',mapping:'',buttons:Array.from({length:16},(_,i)=>({pressed:i===1||i===14})),axes:[0,0]};
    navigator.getGamepads=()=>[pad];var selected=0;shell={_web_controller_press:id=>selected=id};
    localDevices();updateLabelDevice()`);
  assert(p.run('selected>0'));assert.equal(p.run('onButtonLabel(4)'),'B');
  assert.equal(p.run('onButtonLabel(5)'),'Y');assert.equal(p.run('onButtonLabel(0)'),'←');
  assert.equal(p.run('mask()'),17);assert.equal(p.run('onControllerInfo(selected).profile'),1);
  assert.equal(p.storage.has('chirky.controllers.v2'),false);
});

test('mapping commits atomically, rejects duplicates and preserves old bindings if storage fails',()=>{
  const p=player();
  p.run('var values=defaultKeys.map(code=>({kind:1,code:keyCodes.indexOf(code),direction:0}));values[4].code=keyCodes.indexOf("KeyQ")');
  assert(p.run('onSaveKeyboard(values)'));assert.equal(p.run('bindings.KeyQ'),4);
  const next=player(p.storage.get('chirky.inputs.v1'));assert.equal(next.run('bindings.KeyQ'),4);
  assert.equal(p.run('onSaveKeyboard(values.map(()=>values[0]))'),false);
  p.localStorage.setItem=()=>{throw Error('blocked');};
  assert.equal(p.run('values[4].code=keyCodes.indexOf("KeyR");onSaveKeyboard(values)'),false);
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
  p.run('runtime={};keys.add("KeyN");pending=16');p.events.blur();assert.equal(p.run('mask()|pending|dpadPending'),0);assert.equal(p.run('paused'),true);
  pad.onpointermove({...down(5),clientX:150});assert.equal(p.run('mask()'),0);
  assert.equal(p.touch[4].dataset.pressed,'false');
});
test('game transitions preserve held raw inputs for the shared release gate',()=>{
  const p=player();
  p.run('keys.add("KeyN");touch.set(1,1);pending=16;gameStopped()');
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
  const p=player(JSON.stringify({version:1,keys:['KeyH','KeyL','KeyI','KeyK','Space','ControlRight','Enter','Escape'],touch:'auto'}));
  assert.equal(p.element('#key-help-2').textContent,'I');
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

test('two keyboard layouts isolate movement and actions, share Start/Menu and use P1 for solo play',()=>{
  const p=player();
  for(const code of ['ArrowRight','KeyN','KeyA','KeyG'])p.events.keydown({code,preventDefault(){}});
  assert.deepEqual(Array.from(p.run('localDevices().slice(0,2).map(d=>d.mask)')),[18,33]);
  assert.equal(p.run('keyboardMask()'),18,'P1 drives combined input too; P2 stays independent');
  for(const code of ['ArrowRight','KeyN','KeyA','KeyG'])p.events.keyup({code});
  assert.deepEqual(Array.from(p.run('localDevices().slice(0,2).map(d=>d.pending)')),[18,33]);
  p.run('localKeyPending.fill(0)');
  for(const code of ['Enter','Escape']){p.events.keydown({code,preventDefault(){}});p.events.keyup({code});}
  assert.deepEqual(Array.from(p.run('localDevices().slice(0,2).map(d=>d.pending)')),[192,192]);
  p.run('setPaused(true)');
  assert.deepEqual(Array.from(p.run('localKeyPending')),[0,0]);
});

test('player mappings persist independently, reject cross-player conflicts and update labels atomically',()=>{
  const p=player();
  p.run('var values=playerKeys[0].map(code=>({kind:1,code:keyCodes.indexOf(code),direction:0}));values[4].code=keyCodes.indexOf("KeyQ")');
  assert.equal(p.run('onSaveKeyboard(values,1)'),true);
  assert.equal(p.run('onDeviceLabel(1,4)'),'Q');assert.equal(p.run('onDeviceLabel(3,4)'),'F');
  assert.equal(p.run('onButtonLabel(4)'),'Q');
  assert.equal(p.element('#key-help-4').textContent,'Q');
  const next=player(p.storage.get('chirky.inputs.v1'));
  assert.equal(next.run('onDeviceLabel(1,4)'),'Q');
  assert.equal(p.run('values[4].code=keyCodes.indexOf("KeyF");onSaveKeyboard(values,1)'),-1);
  assert.equal(p.run('values[4].code=keyCodes.indexOf("Enter");onSaveKeyboard(values,1)'),false);
  p.localStorage.setItem=()=>{throw Error('blocked');};
  assert.equal(p.run('values[4].code=keyCodes.indexOf("KeyR");onSaveKeyboard(values,1)'),false);
  assert.equal(p.run('onDeviceLabel(1,4)'),'Q');
  const broken=JSON.stringify({version:1,keys:[Array(8).fill('KeyF'),Array(8).fill('KeyF')]});
  assert.equal(player(broken).run('onDeviceLabel(1,4)'),'N');
});

test('old default keyboard mappings upgrade to the shared P1 layout',()=>{
  const p=player(JSON.stringify({version:1,keys:['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','KeyX','KeyZ','Enter','Escape'],touch:'hide'}));
  assert.equal(p.run('onButtonLabel(4)'),'N');assert.equal(p.run('onDeviceLabel(1,5)'),'M');
  assert.equal(p.run('inputSettings.touch'),'hide');
  p.run('var map=playerKeys[1].map(code=>({kind:1,code:keyCodes.indexOf(code),direction:0}));map[4].code=keyCodes.indexOf("KeyR");map[5].code=keyCodes.indexOf("KeyT")');
  assert.equal(p.run('onSaveKeyboard(map,2)'),true);
  const next=player(p.storage.get('chirky.inputs.v1'));
  assert.equal(next.run('onDeviceLabel(3,4)'),'R');assert.equal(next.run('onDeviceLabel(3,5)'),'T');
  assert.equal(next.run('onButtonLabel(4)'),'N');
});
