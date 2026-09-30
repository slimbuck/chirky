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
  const document={querySelector:element,querySelectorAll:()=>touch,addEventListener:(name,fn)=>{events[name]=fn;}};
  const window={addEventListener:(name,fn)=>{events[name]=fn;}};
  const localStorage={getItem:key=>storage.get(key),setItem:(key,value)=>storage.set(key,value)};
  const context=vm.createContext({document,window,localStorage,location:{search:''},URLSearchParams,console,
    navigator:{getGamepads:()=>[]},fetch:()=>new Promise(()=>{}),AudioContext:class{resume(){return Promise.resolve();}}});
  const run=code=>vm.runInContext(code,context);
  run(fs.readFileSync(require.resolve('../web/player.js'),'utf8'));
  return {run,element,touch,events,storage,document,localStorage};
}
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
  const p=player(),down=id=>({pointerId:id,preventDefault(){}});
  p.touch[0].onpointerdown(down(1));p.touch[4].onpointerdown(down(2));assert.equal(p.run('mask()'),17);
  p.touch[0].onpointercancel({pointerId:1});assert.equal(p.run('mask()'),16);
  p.touch[4].onlostpointercapture({pointerId:2});assert.equal(p.run('mask()'),0);
  p.run('runtime={};keys.add("KeyX");pending=16');p.events.blur();assert.equal(p.run('mask()|pending'),0);assert.equal(p.run('paused'),true);
});
test('fullscreen targets the persistent console container',async()=>{
  const p=player();let entered=0,exited=0;
  p.element('#player').requestFullscreen=async()=>{entered++;p.document.fullscreenElement=p.element('#player');};
  p.document.exitFullscreen=async()=>{exited++;p.document.fullscreenElement=null;};
  await p.run('toggleFullscreen()');await p.run('toggleFullscreen()');assert.equal(entered,1);assert.equal(exited,1);
});
