const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const test=require('node:test');

function player(saved){
  const elements=new Map(),storage=new Map(saved?[['chirky.inputs.v1',saved]]:[]),events={};
  function element(selector){
    if(!elements.has(selector))elements.set(selector,{dataset:{},listeners:{},style:{},
      addEventListener(name,fn){this.listeners[name]=fn;},setAttribute(){},focus(){},
      showModal(){this.open=true;},close(){this.open=false;}});
    return elements.get(selector);
  }
  const binds=Array.from({length:8},(_,i)=>Object.assign(element('bind'+i),{dataset:{bind:String(i)}}));
  const touch=Array.from({length:8},(_,i)=>Object.assign(element('touch'+i),{dataset:{button:String(i)},setPointerCapture(){}}));
  const document={querySelector:element,querySelectorAll:s=>s==='[data-bind]'?binds:touch,addEventListener:(name,fn)=>{events[name]=fn;}};
  const window={addEventListener:(name,fn)=>{events[name]=fn;}};
  const localStorage={getItem:key=>storage.get(key),setItem:(key,value)=>storage.set(key,value)};
  const context=vm.createContext({document,window,localStorage,location:{search:''},URLSearchParams,console,
    navigator:{getGamepads:()=>[]},fetch:()=>new Promise(()=>{}),AudioContext:class{resume(){return Promise.resolve();}}});
  const run=code=>vm.runInContext(code,context);
  run(fs.readFileSync(require.resolve('../web/player.js'),'utf8'));
  const key=code=>element('#settings').listeners.keydown({code,preventDefault(){},stopPropagation(){}});
  return {run,element,binds,touch,key,events,storage,document,localStorage};
}
test('eight defaults, invalid stored settings and gamepad mappings',()=>{
  const p=player('{bad json');assert.equal(p.run('Object.keys(bindings).length'),8);
  assert.equal(p.run('bindings.KeyX'),4);assert.equal(p.run('bindings.KeyZ'),5);assert.equal(p.run('bindings.Enter'),6);assert.equal(p.run('bindings.Escape'),7);
  assert.equal(p.run('validKeys([...defaultKeys.slice(0,7),"KeyX"])'),false);
  p.run('keys.add("ArrowLeft");keys.add("KeyX")');assert.equal(p.run('mask()'),17);
  p.run('keys.clear();navigator.getGamepads=()=>[{mapping:"standard",buttons:Array.from({length:16},(_,i)=>({pressed:i===0||i===2||i===9})),axes:[0,0]}]');
  assert.equal(p.run('mask()'),(1<<4)|(1<<5)|(1<<6));
});
test('duplicate keys are rejected, cancellation preserves live settings and save survives reload',()=>{
  const p=player();p.element('#input-settings').onclick();assert.equal(p.run('paused'),true);
  p.binds[4].onclick();p.key('KeyZ');assert.match(p.element('#binding-message').textContent,/already assigned/);
  p.key('KeyQ');assert.equal(p.run('bindings.KeyX'),4);
  p.element('#cancel-settings').onclick();assert.equal(p.run('bindings.KeyX'),4);assert.equal(p.storage.size,0);
  p.element('#input-settings').onclick();p.binds[4].onclick();p.key('KeyQ');
  p.element('#touch-mode').value='show';p.element('#touch-mode').onchange();p.element('#save-settings').onclick();
  assert.equal(p.run('bindings.KeyQ'),4);assert.equal(p.run('bindings.KeyX'),undefined);
  const next=player(p.storage.get('chirky.inputs.v1'));assert.equal(next.run('bindings.KeyQ'),4);assert.equal(next.run('inputSettings.touch'),'show');
  next.element('#input-settings').onclick();next.element('#reset-bindings').onclick();next.element('#save-settings').onclick();assert.equal(next.run('bindings.KeyX'),4);
});
test('storage failure keeps current bindings, Escape can be captured, and cancellation is explicit',()=>{
  const p=player();p.element('#input-settings').onclick();p.binds[7].onclick();p.key('Escape');assert.equal(p.run('captureButton'),null);
  p.binds[4].onclick();p.key('KeyQ');p.localStorage.setItem=()=>{throw new Error('blocked');};p.element('#save-settings').onclick();
  assert.equal(p.run('bindings.KeyX'),4);assert.equal(p.run('settingsOpen'),true);assert.match(p.element('#binding-message').textContent,/Could not save/);
  p.binds[3].onclick();p.element('#cancel-binding').onclick();assert.equal(p.run('captureButton'),null);
});
test('simultaneous touch inputs release on pointer cancellation and focus loss',()=>{
  const p=player(),down=id=>({pointerId:id,preventDefault(){}});
  p.touch[0].onpointerdown(down(1));p.touch[4].onpointerdown(down(2));assert.equal(p.run('mask()'),17);
  p.touch[0].onpointercancel({pointerId:1});assert.equal(p.run('mask()'),16);
  p.touch[4].onlostpointercapture({pointerId:2});assert.equal(p.run('mask()'),0);
  p.run('runtime={};keys.add("KeyX");pending=16');p.events.blur();assert.equal(p.run('mask()|pending'),0);assert.equal(p.run('paused'),true);
});
test('fullscreen enters the whole player and exits through the same button',async()=>{
  const p=player();let entered=0,exited=0;
  p.element('#player').requestFullscreen=async()=>{entered++;p.document.fullscreenElement=p.element('#player');};
  p.document.exitFullscreen=async()=>{exited++;p.document.fullscreenElement=null;};
  await p.element('#fullscreen').onclick();await p.element('#fullscreen').onclick();assert.equal(entered,1);assert.equal(exited,1);
});
