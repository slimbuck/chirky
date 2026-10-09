const $=selector=>document.querySelector(selector);
const params=new URLSearchParams(location.search);
let id=params.get("game") || "launcher";
const canvas=$("#screen"),status=$("#status");
let catalog=[],ids=[],titles={launcher:"Launcher"};
let runtime,shell,displayContext,audio,muted=false,paused=false,leaving=false,last=0,accumulator=0,pending=0,ready=false,loading=false;
let files,configs,loadSequence=0,cancelPressed=false;
let navigation={push:true,level:null};
let returnNavigation=null;
const keys=new Set(),touch=new Map(),sounds=new Map(),sources=new Set();
let dpadPointer=null,dpadBounds=null,dpadPending=0;
const assetSounds=new Map();
let moduleLoadQueue=Promise.resolve(),loadCompletion=null;
let fullscreenQueued=false,fullscreenBusy=false,lastScreenTap=null,screenContact=null,lastTouchFullscreen=-Infinity;
let director=null;
function saveKey(game,key){
  if(!/^[A-Za-z0-9_-]{1,95}$/.test(game) || !/^[A-Za-z0-9_-]{1,95}$/.test(key))throw Error("Invalid save name");
  return `chirky.save.v1.${game}.${key}`;
}
function onSaveRead(game,key){
  try{
    const encoded=localStorage.getItem(saveKey(game,key));
    if(!encoded || encoded.length>87384)return null;
    const value=atob(encoded);if(value.length>65536)return null;
    return Uint8Array.from(value,letter=>letter.charCodeAt(0));
  }catch{return null;}
}
function onSaveWrite(game,key,bytes){
  try{
    if(!bytes.length || bytes.length>65536)return false;
    let value="";for(const byte of bytes)value+=String.fromCharCode(byte);
    localStorage.setItem(saveKey(game,key),btoa(value));return true;
  }catch{return false;}
}
let startupPercent=0;
function startupProgress(percent){
  startupPercent=Math.max(startupPercent,Math.min(100,Math.floor(percent)));
  $("#startup-fill").style.width=`${startupPercent}%`;
  $("#startup-percent").textContent=`${startupPercent}%`;
  $("#startup-progress").setAttribute("aria-valuenow",String(startupPercent));
}
// Give the browser a paint before compilation or synchronous initialization.
const startupPaint=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
async function startupBinary(url){
  const response=await checked(url);
  const length=Number(response.headers.get("Content-Length"));
  // Content-Length may describe compressed bytes, while fetch yields decoded bytes.
  const measurable=length>0 && !response.headers.get("Content-Encoding");
  if(!response.body){const bytes=new Uint8Array(await response.arrayBuffer());startupProgress(75);return bytes;}
  const reader=response.body.getReader(),chunks=[];let size=0;
  for(;;){
    const {done,value}=await reader.read();if(done)break;
    chunks.push(value);size+=value.length;
    if(measurable)startupProgress(20+55*Math.min(size/length,1));
  }
  const bytes=new Uint8Array(size);let offset=0;
  for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}
  startupProgress(75);return bytes;
}
function startupFailed(error){
  status.textContent=error.message;
  const notice=$("#startup-error");notice.textContent="Unable to load Chirky. Reload to try again.";notice.hidden=false;
  console.error(error);
}
function directorSession(){return crypto.randomUUID?.() || `web-${Date.now()}-${Math.random().toString(16).slice(2)}`;}
async function syncDirector(){
  if(!director || director.syncing || leaving)return;
  director.syncing=true;
  try{
    const response=await fetch(`${director.endpoint}/v1/sync`,{
      method:"POST",headers:{"Content-Type":"application/json","Accept":"text/plain"},
      body:JSON.stringify({protocol:1,game:director.game,world:director.world,session:director.session,
        last_revision:director.revision,events:director.events})
    });
    if(!response.ok)throw new Error(`HTTP ${response.status}`);
    const acknowledged=Number(response.headers.get("X-Chirky-Ack"));
    const revision=Number(response.headers.get("X-Chirky-Revision"));
    const state=await response.text();
    if(!Number.isSafeInteger(acknowledged) || !Number.isSafeInteger(revision))throw new Error("Invalid director response");
    director.events=director.events.filter(item=>item.sequence>acknowledged);
    if(revision>director.revision && state){director.revision=revision;director.state=state;}
    director.lastError="";
  }catch(error){
    if(director && director.lastError!==error.message){director.lastError=error.message;console.warn("World director offline:",error.message);}
  }finally{if(director)director.syncing=false;}
}
function onDirectorConnect(endpoint,game,world){
  onDirectorDisconnect();
  try{
    const parsed=new URL(endpoint,location.href);if(parsed.protocol!=="http:")return false;
    director={endpoint:parsed.href.replace(/\/+$/,"").replace(/\/v1\/sync$/,""),game,world,
      session:directorSession(),events:[],nextSequence:1,revision:0,state:"",syncing:false,lastError:"",timer:0};
    director.timer=setInterval(syncDirector,1000);syncDirector();return true;
  }catch{return false;}
}
function onDirectorDisconnect(){if(director?.timer)clearInterval(director.timer);director=null;}
function onDirectorEvent(json){
  if(!director || director.events.length>=32)return false;
  try{const event=JSON.parse(json);if(!event || Array.isArray(event) || typeof event!=="object")return false;
    director.events.push({sequence:director.nextSequence++,event});syncDirector();return true;
  }catch{return false;}
}
function onDirectorState(afterRevision){
  return director && director.revision>afterRevision && director.state?
    {revision:director.revision,text:director.state}:null;
}
function prepareAssetSound(handle,pointer,size,rate,channels){
  if(assetSounds.has(handle))return;
  const frames=size/(2*channels);
  try {
    const buffer=new AudioBuffer({length:frames,numberOfChannels:channels,sampleRate:rate});
    const pcm=runtime.HEAP16.subarray(pointer/2,(pointer+size)/2);
    for(let channel=0;channel<channels;channel++){
      const output=buffer.getChannelData(channel);
      for(let frame=0;frame<frames;frame++)output[frame]=pcm[frame*channels+channel]/32768;
    }
    assetSounds.set(handle,buffer);
  }catch(error){console.warn("Sound unavailable",handle,error);}
}
function playAssetSound(handle){
  if(!audio || muted || paused || leaving)return;
  const buffer=assetSounds.get(handle);if(!buffer)return;
  for(const source of sources)if(source.buffer===buffer)return;
  if(sources.size>=8){const oldest=sources.values().next().value;oldest.stop();sources.delete(oldest);}
  const source=audio.createBufferSource();source.buffer=buffer;source.connect(audio.destination);
  sources.add(source);source.onended=()=>sources.delete(source);source.start();
}
const {playerKeysConflict,inputNames,defaultKeys,settingsKey,validKeys,keyName,renderInputSettings,initialSettings,resizePlayer}=window.ChirkyShell;
let inputSettings=initialSettings,bindings={};
let playerKeys=[inputSettings.keys,inputSettings.p2Keys];
// Presentation follows the most recently pressed device. Held pads and noisy
// axes must not continually steal labels back from keyboard/touch users.
const coarsePointer=typeof matchMedia==="function"?matchMedia("(any-pointer: coarse)"):null;
let labelSource=coarsePointer?.matches?"touch":"keyboard",keyboardUsed=false;
function consoleInputOptions(){
  const touchLayout=inputSettings.touch==="show" || (inputSettings.touch!=="hide" && coarsePointer?.matches);
  // console_input_option flags: hide keyboard mapping (1) and test rows (2).
  // Both keyboard sources remain available to games.
  return touchLayout && !keyboardUsed?3:0;
}
let labelPad=null;
// Physical connection IDs are distinct from mapping profiles: two identical
// controllers may share a profile but must never share a player slot.
let nextDeviceId=3,localKeyPending=[0,0],localTouchPending=0;
const localPads=new Map();
window.addEventListener("gamepaddisconnected",event=>{localPads.delete(event.gamepad.index);});
function localDevices(connected=pads()){
  const present=new Set();
  const result=[{id:1,kind:0,mask:playerKeyboardMask(0),pending:localKeyPending[0]},
    {id:3,kind:0,mask:playerKeyboardMask(1),pending:localKeyPending[1]},
    {id:2,kind:2,mask:[...touch.values()].reduce((a,b)=>a|b,0),pending:localTouchPending|dpadPending}];
  for(const pad of connected){
    const index=pad.index ?? 0,identity=padIdentity(pad);present.add(index);
    let device=localPads.get(index);
    if(!device || device.identity!==identity){device={id:++nextDeviceId,identity};localPads.set(index,device);}
    device.pad=pad;
    result.push({id:device.id,kind:1,mask:padMask(pad),pending:0});
  }
  for(const index of localPads.keys())if(!present.has(index))localPads.delete(index);
  return result.slice(0,8);
}
function onDeviceLabel(id,action){
  if(id===1 || id===3)return keyName(playerKeys[id===1?0:1][action]).toUpperCase();
  if(id===2)return ["←","→","↑","↓","A","B","START","MENU"][action];
  const device=[...localPads.values()].find(d=>d.id===id);
  if(!device)return "UNBOUND";
  return controllerLabel(device.pad,action);
}
function feedLocalInputs(){
  shell._web_input_begin();
  for(const d of localDevices())shell._web_input_device(d.id,d.kind,d.mask,d.pending);
  shell._web_input_end();localKeyPending.fill(0);localTouchPending=0;
}
const labelPadHeld=new Map();
function updateLabelDevice(connected=pads()){
  const present=new Set();
  for(const pad of connected){
    const identity=`${pad.index ?? 0}|${padIdentity(pad)}`;present.add(identity);
    const held=new Set(rawPad(pad).map(v=>`${v.kind}:${v.code}:${v.direction}`));
    const previous=labelPadHeld.get(identity) || new Set();
    if([...held].some(v=>!previous.has(v))){
      labelSource="controller";labelPad=identity;
      const device=[...localPads.values()].find(d=>d.pad===pad);
      if(device)shell?._web_controller_press?.(device.id);
    }
    labelPadHeld.set(identity,held);
  }
  for(const identity of labelPadHeld.keys())if(!present.has(identity))labelPadHeld.delete(identity);
  if(labelSource==="controller" && !present.has(labelPad)){labelSource="keyboard";labelPad=null;}
}
function padBindingLabel(pad,binding,profile="generic"){
  if(!binding)return "UNBOUND";
  if(profile==="snes"){
    if(gp2040Generic(pad) && binding.kind===1 && binding.code>=16 && binding.code<=19)
      return ["↑","↓","←","→"][binding.code-16];
    if(binding.kind===2 && binding.code<2)return ["←","→","↑","↓"][binding.code*2+(binding.direction>0?1:0)];
    const names=pad.mapping==="standard"?["B","A","Y","X","L","R",null,null,"SELECT","START",null,null,"↑","↓","←","→"]:
      ["Y","B","X","A","L","R",null,null,"SELECT","START",null,null,"↑","↓","←","→"];
    if(binding.kind===1 && names[binding.code])return names[binding.code];
  }
  if(binding.kind===2){
    if(pad.mapping==="standard" && binding.code<4)
      return `${binding.code<2?"L":"R"} STICK ${binding.code%2?(binding.direction<0?"UP":"DOWN"):(binding.direction<0?"LEFT":"RIGHT")}`;
    return `AX${binding.code} ${binding.direction<0?"NEG":"POS"}`;
  }
  if(pad.mapping==="standard"){
    // Positions are reliable; a standard Gamepad mapping doesn't specify the
    // printed legends (Xbox, Nintendo and PlayStation all differ).
    const names=["SOUTH","EAST","WEST","NORTH","L1","R1","L2","R2","SELECT","START","L STICK","R STICK","↑","↓","←","→","HOME"];
    return names[binding.code] || `BTN ${binding.code}`;
  }
  return `BTN ${binding.code}`;
}
function onButtonLabel(action){
  if(!Number.isInteger(action) || action<0 || action>=8)return "UNBOUND";
  if(labelSource==="touch")return ["←","→","↑","↓","A","B","START","MENU"][action];
  if(labelSource==="controller"){
    const pad=pads().find(p=>`${p.index ?? 0}|${padIdentity(p)}`===labelPad);
    if(pad)return controllerLabel(pad,action);
  }
  return keyName(inputSettings.keys[action]).toUpperCase();
}
const keyCodes=[...new Set([...defaultKeys,...Array.from({length:26},(_,i)=>"Key"+String.fromCharCode(65+i)),
  ...Array.from({length:10},(_,i)=>"Digit"+i),...Array.from({length:10},(_,i)=>"Numpad"+i),
  "Space","ShiftLeft","ShiftRight","ControlLeft","ControlRight","AltLeft","AltRight","Backspace","Tab",
  "Comma","Period","Slash","Semicolon","Quote","BracketLeft","BracketRight","Backslash","Minus","Equal","Backquote"])];
const controllerSettingsKey="chirky.controllers.v2";
let padSettings={};
try{
  const saved=JSON.parse(localStorage.getItem(controllerSettingsKey));
  if(saved && typeof saved==="object" && !Array.isArray(saved))padSettings=saved;
  else {
    const legacy=JSON.parse(localStorage.getItem("chirky.controllers.v1"));
    if(legacy && typeof legacy==="object")for(const [model,bindings] of Object.entries(legacy))
      if(validPadMap(bindings))padSettings[model]={type:"generic",bindings};
  }
}catch{}
const buttonBinding=code=>({kind:1,code,direction:0});
function gp2040Generic(pad){
  return pad.mapping!=="standard" && pad.buttons.length>=20 &&
    /GP2040.*Vendor: 10c4 Product: 82c0/i.test(pad.id);
}
function snesPreset(pad){
  // GP2040 Generic HID exposes Up/Down/Left/Right as buttons 16..19.
  const directions=gp2040Generic(pad)?[18,19,16,17].map(buttonBinding):pad.mapping==="standard" || pad.buttons.length>=16?[14,15,12,13].map(buttonBinding):
    [{kind:2,code:0,direction:-1},{kind:2,code:0,direction:1},{kind:2,code:1,direction:-1},{kind:2,code:1,direction:1}];
  return [...directions,...(pad.mapping==="standard"?[0,2,9,8]:[1,0,9,8]).map(buttonBinding)];
}
function padProfile(pad){
  const saved=padSettings[padIdentity(pad)];
  if(saved && ["snes","generic"].includes(saved.type) && validPadMap(saved.bindings)){
    // Repair only the old unmodified preset. Generic and custom maps stay explicit.
    if(saved.type==="snes" && gp2040Generic(pad) && saved.bindings.every((b,i)=>
      b.kind===1 && b.direction===0 && b.code===[14,15,12,13,1,0,9,8][i]))
      return {type:"snes",bindings:snesPreset(pad)};
    return saved;
  }
  return {type:"snes",bindings:snesPreset(pad)};
}
function controllerLabel(pad,action){
  const profile=padProfile(pad);return padBindingLabel(pad,profile.bindings[action],profile.type);
}
function connectedController(id){
  const device=[...localPads.values()].find(d=>d.id===id);
  return device && pads().find(p=>(p.index ?? 0)===(device.pad.index ?? 0) && padIdentity(p)===device.identity);
}
function onControllerInfo(id){
  const pad=connectedController(id);
  return pad?{name:pad.id,profile:padProfile(pad).type==="snes"?1:0}:null;
}
function onSaveController(id,profile,values){
  const pad=connectedController(id);
  if(!pad || ![0,1].includes(profile))return false;
  const bindings=values || snesPreset(pad);
  if(!validPadMap(bindings))return false;
  const next={...padSettings,[padIdentity(pad)]:{type:profile===1?"snes":"generic",bindings}};
  try{localStorage.setItem(controllerSettingsKey,JSON.stringify(next));padSettings=next;return true;}catch{return false;}
}
function refreshKeyFeedback(){
  inputSettings.keys.forEach((code,index)=>$("#key-help-"+index).dataset.pressed=String(keys.has(code)));
}
function refreshInputSettings(){
  bindings=Object.fromEntries(inputSettings.keys.map((code,index)=>[code,index]));
  renderInputSettings(inputSettings);
  refreshKeyFeedback();
}
function validPadMap(values){
  return Array.isArray(values) && values.length===8 && new Set(values.map(v=>JSON.stringify(v))).size===8 &&
    values.every(v=>v && Number.isInteger(v.code) && v.code>=0 && v.code<256 &&
      ((v.kind===1 && v.direction===0) || (v.kind===2 && [-1,1].includes(v.direction))));
}
function padIdentity(pad){return `${pad.id}|${pad.mapping}`;}
function rawPad(pad){
  const held=[];
  pad.buttons.forEach((b,code)=>{if(b.pressed)held.push({kind:1,code,direction:0});});
  pad.axes.forEach((value,code)=>{if(Math.abs(value)>.55 && Math.abs(value)<=1.01)held.push({kind:2,code,direction:Math.sign(value)});});
  return held;
}
function padMask(pad){
  const profile=padProfile(pad),held=rawPad(pad);
  let result=profile.bindings.reduce((mask,b,i)=>mask|(held.some(v=>v.kind===b.kind && v.code===b.code && v.direction===b.direction)?1<<i:0),0);
  if(profile.type==="snes" && pad.mapping==="standard"){
    // Standard SNES directions accept the matching left-stick direction too,
    // including saved presets. Explicitly remapped directions keep their binding.
    for(const value of held)if(value.kind===2 && value.code<2){
      const action=value.code*2+(value.direction>0?1:0),binding=profile.bindings[action];
      if(binding.kind===1 && binding.code===[14,15,12,13][action])result|=1<<action;
    }
  }
  return result;
}
function pads(){return Array.from(navigator.getGamepads?.() || []).filter(Boolean);}
function playerKeyboardMask(player){return playerKeys[player].reduce((value,key,b)=>value|(keys.has(key)?1<<b:0),0);}
function keyboardMask(){
  let value=0;for(const key of keys)if(key in bindings)value|=1<<bindings[key];
  return value|((playerKeyboardMask(0)|playerKeyboardMask(1))&192);
}
function controllerMask(){return pads().reduce((value,pad)=>value|padMask(pad),0);}
function mask(){let value=keyboardMask()|controllerMask();for(const buttons of touch.values())value|=buttons;return value;}
function onSaveKeyboard(values,profile=1){
  try{
    if(profile<0 || profile>2)return false;
    const mapped=values.map(v=>keyCodes[v.code]),player=profile===2?1:0;
    if(!validKeys(mapped))return false;
    if(playerKeysConflict(mapped,playerKeys[1-player]))return -1;
    const next={...inputSettings,[player?"p2Keys":"keys"]:mapped};
    localStorage.setItem(settingsKey,JSON.stringify(next));inputSettings=next;
    playerKeys=[next.keys,next.p2Keys];refreshInputSettings();
    return true;
  }catch{return false;}
}

refreshInputSettings();
function stopSounds(){for(const source of sources)source.stop();sources.clear();}
function unlock(){if(!audio)audio=new AudioContext();audio.resume().catch(()=>{});}
async function playSound(path){
  if(!audio || muted || paused || leaving)return;
  try {
    let entry=sounds.get(path);if(!entry)return;
    if(entry instanceof Uint8Array){entry=audio.decodeAudioData(entry.slice().buffer);sounds.set(path,entry);}
    const buffer=await entry;if(muted || paused || leaving)return;
    const source=audio.createBufferSource();source.buffer=buffer;source.connect(audio.destination);
    sources.add(source);source.onended=()=>sources.delete(source);source.start();
  }catch(error){console.warn("Sound unavailable",path,error);}
}
function setPaused(value){
  localKeyPending.fill(0);localTouchPending=0;
  paused=value;keys.clear();touch.clear();dpadPointer=null;dpadBounds=null;dpadPending=0;fullscreenQueued=false;refreshTouchFeedback();pending=0;last=0;accumulator=0;
  refreshKeyFeedback();
  if(value){shell?._web_console_pause();stopSounds();}
}
function toggleMute(){muted=!muted;if(muted)stopSounds();}
function fullscreenUnavailable(blocked=false){
  const appleMobile=/iPad|iPhone|iPod/.test(navigator.userAgent || "") ||
    (navigator.platform==="MacIntel" && navigator.maxTouchPoints>1);
  const notice=$("#fullscreen-message");
  notice.textContent=appleMobile?
    "For a view without Safari’s toolbar, use Share → Add to Home Screen (Open as Web App), then open Chirky from its icon.":
    blocked?"Fullscreen was blocked. Double-tap the display to try again.":
    "This browser does not support fullscreen for games.";
  notice.hidden=false;
}
async function toggleFullscreen(){
  if(fullscreenBusy)return;
  fullscreenQueued=false;fullscreenBusy=true;
  const player=$("#player"),notice=$("#fullscreen-message");notice.hidden=true;
  try{
    if(document.fullscreenElement || document.webkitFullscreenElement){
      await (document.exitFullscreen || document.webkitExitFullscreen).call(document);
    }else if(navigator.standalone || (typeof matchMedia==="function" && matchMedia("(display-mode: standalone)").matches)){
      notice.textContent="Chirky is already running as a Home Screen app.";notice.hidden=false;
    }else{
      const request=player.requestFullscreen || player.webkitRequestFullscreen;
      if(!request){fullscreenUnavailable();return;}
      // Invoke the protected API before any await, within the trusted gesture.
      await request.call(player);
    }
  }catch{fullscreenUnavailable(true);}
  finally{fullscreenBusy=false;canvas.focus();}
}
function requestConsoleFullscreen(){
  // Touch activation is granted on release, not on pointer-down. The console
  // can select this action while a finger is still held between animation frames.
  if(touch.size){fullscreenQueued=true;return;}
  void toggleFullscreen();
}
function consoleGesture(){
  // Deliver short menu taps synchronously too, so their platform actions retain
  // the browser's user gesture. Gameplay stays on the fixed simulation clock.
  if(shell && !loading && shell._web_console_state()!==4){
    const connected=pads();
    if(shell._web_input_begin)feedLocalInputs();
    shell._web_console_tick(mask()|pending|dpadPending,keyboardMask(),controllerMask(),keys.size,
      connected.some(p=>rawPad(p).length),cancelPressed,Math.max(0,...connected.map(p=>p.buttons.filter(b=>b.pressed).length)),consoleInputOptions());
    pending=0;dpadPending=0;cancelPressed=false;
  }
  if(fullscreenQueued)void toggleFullscreen();
}
window.addEventListener("keydown",event=>{
  if(event.code && event.code!=="Unidentified")keyboardUsed=true;
  const capturing=shell?._web_capture_keyboard()===1;
  if(event.code==="F1"){
    labelSource="keyboard";event.preventDefault();if(!event.repeat)cancelPressed=true;unlock();return;
  }
  if(capturing || event.code in bindings || playerKeys.some(map=>map.includes(event.code))){
    event.preventDefault();if(event.repeat)return;
    labelSource="keyboard";keys.add(event.code);refreshKeyFeedback();unlock();
    if(capturing){const code=keyCodes.indexOf(event.code);if(code>=0)shell._web_capture(1,code,0);}
    else {
      if(event.code in bindings)pending|=1<<bindings[event.code];
      playerKeys.forEach((map,p)=>{
        const b=map.indexOf(event.code);if(b<0)return;
        localKeyPending[p]|=1<<b;if(b>=6)pending|=1<<b;
      });
      consoleGesture();
    }
  }
});
window.addEventListener("keyup",event=>{keys.delete(event.code);refreshKeyFeedback();});
canvas.addEventListener("pointerdown",event=>{
  unlock();canvas.focus();
  if(event.pointerType!=="mouse"){
    if(event.isPrimary===false){screenContact=lastScreenTap=null;return;}
    screenContact={id:event.pointerId,x:event.clientX,y:event.clientY,time:event.timeStamp};
  }
});
canvas.addEventListener("pointerup",event=>{
  const contact=screenContact;screenContact=null;
  if(!contact || contact.id!==event.pointerId || event.timeStamp-contact.time>350 ||
    Math.hypot(event.clientX-contact.x,event.clientY-contact.y)>24){lastScreenTap=null;return;}
  if(lastScreenTap && event.timeStamp-lastScreenTap.time<350 &&
    Math.hypot(event.clientX-lastScreenTap.x,event.clientY-lastScreenTap.y)<24){
    lastScreenTap=null;lastTouchFullscreen=event.timeStamp;event.preventDefault();void toggleFullscreen();
  }else lastScreenTap={x:event.clientX,y:event.clientY,time:event.timeStamp};
});
canvas.addEventListener("pointercancel",()=>{screenContact=lastScreenTap=null;});
canvas.addEventListener("dblclick",event=>{if(event.timeStamp-lastTouchFullscreen>700)void toggleFullscreen();});
// Safari can ignore viewport zoom limits. Cancel its page-scale gestures too,
// without stopping pointer events used by the D-pad and simultaneous buttons.
function preventBrowserGesture(event){if(event.cancelable)event.preventDefault();}
for(const name of ["gesturestart","gesturechange"])
  document.addEventListener(name,preventBrowserGesture,{passive:false});
document.addEventListener("touchmove",event=>{
  if(event.touches.length>1)preventBrowserGesture(event);
},{passive:false});
window.addEventListener("blur",()=>{if(runtime)setPaused(true);});
document.addEventListener("visibilitychange",()=>{if(document.hidden && runtime)setPaused(true);});
function refreshTouchFeedback(){
  let held=0;for(const buttons of touch.values())held|=buttons;
  document.querySelectorAll("[data-button]").forEach(button=>{button.dataset.pressed=String(!!(held&(1<<Number(button.dataset.button))));});
}
const dpad=$(".dpad");
// Cancel touchstart before Chrome recognises a long press and supplies haptic
// feedback. Cancelling contextmenu alone is too late; pointer input stays active.
dpad.addEventListener("touchstart",preventBrowserGesture,{passive:false});
function moveDpad(event){
  if(event.pointerId!==dpadPointer)return;
  const x=event.clientX-dpadBounds.left-dpadBounds.width/2,y=event.clientY-dpadBounds.top-dpadBounds.height/2;
  let value=0;
  // Eight equal angular sectors, with a neutral centre. Capture keeps a thumb
  // dragging outside the pad engaged until release, like a physical D-pad.
  if(Math.hypot(x,y)>Math.min(dpadBounds.width,dpadBounds.height)*.12){
    const diagonal=Math.SQRT2-1;
    if(Math.abs(x)>Math.abs(y)*diagonal)value|=x<0?1:2;
    if(Math.abs(y)>Math.abs(x)*diagonal)value|=y<0?4:8;
  }
  labelSource="touch";touch.set(dpadPointer,value);dpadPending=value;refreshTouchFeedback();
}
dpad.onpointerdown=event=>{
  if(event.button>0 || dpadPointer!==null)return;
  event.preventDefault();dpadPointer=event.pointerId;dpadBounds=dpad.getBoundingClientRect();
  dpad.setPointerCapture(event.pointerId);moveDpad(event);unlock();
};
dpad.onpointermove=moveDpad;
dpad.onpointerup=event=>{
  if(event.pointerId!==dpadPointer)return;
  consoleGesture();touch.delete(dpadPointer);dpadPointer=null;dpadBounds=null;refreshTouchFeedback();
};
dpad.onpointercancel=dpad.onlostpointercapture=event=>{
  if(event.pointerId!==dpadPointer)return;
  touch.delete(dpadPointer);dpadPointer=null;dpadBounds=null;dpadPending=0;fullscreenQueued=false;refreshTouchFeedback();
};
dpad.oncontextmenu=event=>event.preventDefault();
document.querySelectorAll("[data-button]").forEach(button=>{
  if(Number(button.dataset.button)<4)return; // Directions share the sliding pad.
  button.addEventListener("touchstart",preventBrowserGesture,{passive:false});
  button.onpointerdown=event=>{if(event.button>0)return;event.preventDefault();labelSource="touch";button.setPointerCapture(event.pointerId);touch.set(event.pointerId,1<<Number(button.dataset.button));pending|=1<<Number(button.dataset.button);localTouchPending|=1<<Number(button.dataset.button);refreshTouchFeedback();unlock();};
  button.onpointerup=event=>{if(touch.has(event.pointerId))consoleGesture();touch.delete(event.pointerId);refreshTouchFeedback();};
  button.onpointercancel=button.onlostpointercapture=event=>{touch.delete(event.pointerId);fullscreenQueued=false;refreshTouchFeedback();};
  button.oncontextmenu=event=>event.preventDefault();
});
function frame(now){
  if(leaving)return;
  const connected=pads();localDevices(connected);updateLabelDevice(connected);
  const current=mask(),target=shell._web_mapping_controller();
  const mapping=target?connectedController(target):null;
  if(mapping){
    const held=rawPad(mapping);if(held.length){const v=held[0];shell._web_capture_controller(target,v.kind,v.code,v.direction);}
  }
  const observed=target?(mapping?[mapping]:[]):connected;
  if(last)accumulator+=Math.min(now-last,100);
  while(accumulator>=1000/60){
    {
      feedLocalInputs();
      const gameTick=shell._web_console_tick(current|pending|dpadPending,keyboardMask(),controllerMask(),keys.size,
        observed.some(p=>rawPad(p).length),cancelPressed,Math.max(0,...observed.map(p=>p.buttons.filter(b=>b.pressed).length)),consoleInputOptions());
      if(gameTick)runtime._web_tick(current|pending|dpadPending);
    }
    pending=0;dpadPending=0;cancelPressed=false;accumulator-=1000/60;
  }
  const state=shell._web_console_state(),nextPaused=state!==4;
  if(returnNavigation!==null && !loading){
    if(returnNavigation && location.search){const url=new URL(location.href);url.search="";history.pushState(null,"",url);}
    returnNavigation=null;
  }
  if(nextPaused && !paused)stopSounds();paused=nextPaused;
  runtime._web_render();
  last=now;requestAnimationFrame(frame);
}
// Published URLs belong to one immutable release; development servers send no-store.
async function checked(url){const response=await fetch(url);if(!response.ok)throw new Error(`Unable to load ${url} (${response.status})`);return response;}
async function start(){
  startupProgress(5);
  // All independent downloads start together. HTML preloads the large module
  // and launcher artwork before this script has even finished downloading.
  const [catalogDocument,assetFiles,gameConfigs,launcher,wasmBinary]=await Promise.all([
    checked("catalog.json").then(response=>response.json()),
    checked("assets.json").then(response=>response.json()),
    checked("configs.json").then(response=>response.json()),
    import("./launcher.js"),startupBinary("launcher.wasm")]);
  startupProgress(5);
  if(catalogDocument?.version!==1 || !Array.isArray(catalogDocument.games))throw new Error("Invalid game catalog");
  catalog=catalogDocument.games;
  if(!catalog.length || catalog.some(game=>!game || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(game.id) ||
      typeof game.name!=="string" || !["game","diagnostic"].includes(game.role)))throw new Error("Invalid game catalog");
  ids=catalog.map(game=>game.id);
  if(new Set(ids).size!==ids.length)throw new Error("Invalid game catalog");
  titles={launcher:"Launcher",...Object.fromEntries(catalog.map(game=>[game.id,game.name]))};
  files=assetFiles;configs=gameConfigs;
  startupProgress(10);
  displayContext=canvas.getContext("webgl",{alpha:false,depth:true,stencil:false,antialias:false});
  if(!displayContext)throw new Error("WebGL is unavailable");
  shell=await createModule(launcher,wasmBinary);runtime=shell;
  startupProgress(85);
  await loadFiles(shell,"launcher",()=>true,fraction=>startupProgress(85+10*fraction));
  startupProgress(95);await startupPaint();
  if(!shell.ccall("web_init","number",["string"],[""]))throw new Error("Could not initialise the console");
  ready=true;
  resizePlayer();status.textContent="Launcher";
  shell._web_render();startupProgress(100);await startupPaint();
  $("#startup").hidden=true;
  if(window===window.top)canvas.focus({preventScroll:true});
  requestAnimationFrame(frame);
  if(id!=="launcher")await switchGame(id,false,params.get("level"));
}
async function createModule({default:create},wasmBinary){
  await startupPaint();
  return create({canvas,wasmBinary,preinitializedWebGLContext:displayContext,
    onSound:playSound,onAssetReady:prepareAssetSound,onAssetSound:playAssetSound,
    onDirectorConnect,onDirectorDisconnect,onDirectorEvent,onDirectorState,
    onLauncherCount:()=>ids.length,onLauncherName:index=>titles[ids[index]],onLauncherId:index=>ids[index],
    onDiagnostic:index=>catalog[index].role==="diagnostic",
    onLaunch:index=>loadGame(ids[index]),onStopped:gameStopped,onLoaded:onModuleLoaded,
    onOption:option=>{if(option===-6)requestConsoleFullscreen();else if(option===-7)toggleMute();},
    onButtonLabel,onDeviceLabel,onSaveKeyboard,onControllerInfo,onSaveController,onSaveRead,onSaveWrite,printErr:message=>console.warn(message)});
}
async function loadFiles(module,gameId,isCurrent=()=>true,onProgress=()=>{}){
  const loadedSounds=new Map();
  const selected=files.filter(file=>gameId==="launcher"?file.startsWith("assets/launcher/"):file.startsWith(`games/${gameId}/`));let completed=0;
  await Promise.all(selected.map(async file=>{
    let bytes;
    if(file.endsWith(".conf")){
      if(typeof configs[file]!=="string")throw new Error(`Missing game configuration: ${file}`);
      bytes=new TextEncoder().encode(configs[file]);
    }else bytes=new Uint8Array(await (await checked("runtime/"+file)).arrayBuffer());
    if(!isCurrent())return;
    module.FS.mkdirTree("/"+file.slice(0,file.lastIndexOf("/")));module.FS.writeFile("/"+file,bytes);
    if(file.endsWith(".wav"))loadedSounds.set(file,bytes);
    onProgress(++completed/selected.length);
  }));
  return loadedSounds;
}
function gameStopped(){
  ++loadSequence;loading=false;stopSounds();onDirectorDisconnect();
  assetSounds.clear();sounds.clear();pending=0;dpadPending=0;
  id="launcher";status.textContent="Launcher";
  // The console owns the destination screen, including returning to Settings.
  returnNavigation=navigation.push;
}
function switchGame(nextId,push=true,level=null){
  const index=ids.indexOf(nextId);
  if(nextId!=="launcher" && index<0){status.textContent="Unknown game";return;}
  navigation={push,level};shell._web_console_launch(index);
}
async function loadGame(nextId){
  const sequence=++loadSequence,game=catalog.find(game=>game.id===nextId),options=navigation;
  navigation={push:true,level:null};returnNavigation=null;loading=true;status.textContent=`Loading ${game.name}…`;
  try{
    const descriptor=await gameDescriptor(nextId);
    const [bytes,loadedSounds]=await Promise.all([
      checked(descriptor.module).then(response=>response.arrayBuffer()),loadFiles(runtime,nextId,()=>sequence===loadSequence)]);
    if(sequence!==loadSequence)return;
    runtime.FS.writeFile("/"+descriptor.module,new Uint8Array(bytes));
    for(const [path,data] of loadedSounds)sounds.set(path,data);
    const config=descriptor.config;
    if(game.levelSetting && options.level && /^\d+$/.test(options.level)){
      const text=runtime.FS.readFile(config,{encoding:"utf8"}),setting=game.levelSetting;
      runtime.FS.writeFile(config,text.replace(new RegExp(`^${setting}=.*$`,"m"),"")+`\n${setting}=${Number(options.level)}\n`);
    }
    const ok=await initializeModule(descriptor.module,config,sequence);
    if(sequence!==loadSequence)return;
    if(!ok)throw new Error("Could not initialise the game");
    id=nextId;
    if(options.push){const url=new URL(location.href);url.search=`?game=${nextId}`;history.pushState(null,"",url);}
    status.textContent=titles[id];
  }catch(error){
    if(sequence!==loadSequence)return;
    shell._web_load_failed();status.textContent=error.message;console.error(error);
  }finally{
    if(sequence===loadSequence){loading=false;last=0;accumulator=0;canvas.focus({preventScroll:true});}
  }
}
async function gameDescriptor(gameId){return (await import(`./${gameId}.js`)).default;}
function onModuleLoaded(ok){const complete=loadCompletion;loadCompletion=null;complete?.(ok);}
function initializeModule(module,config,sequence){
  // Serialize the loader even if a previous download was cancelled. Its C
  // callback discards stale generations before game init; this queue prevents
  // another request from observing a half-compiled copy of the same module.
  const work=moduleLoadQueue.then(()=>{
    if(sequence!==loadSequence)return false;
    return new Promise((resolve,reject)=>{
      loadCompletion=resolve;
      try{runtime.ccall("web_load",null,["string","string"],[module,config]);}
      catch(error){loadCompletion=null;reject(error);}
    });
  });
  moduleLoadQueue=work.catch(()=>{});return work;
}
window.addEventListener("popstate",()=>{const query=new URLSearchParams(location.search);switchGame(query.get("game") || "launcher",false,query.get("level"));});
window.addEventListener("pagehide",()=>{leaving=true;++loadSequence;stopSounds();if(ready)shell._web_destroy();audio?.close();});
window.addEventListener("pageshow",event=>{if(event.persisted)location.reload();});
canvas.addEventListener("webglcontextlost",event=>{event.preventDefault();leaving=true;stopSounds();status.textContent="Display connection lost. Reload to continue.";});
start().catch(startupFailed);
