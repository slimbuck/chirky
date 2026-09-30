const $=selector=>document.querySelector(selector);
const params=new URLSearchParams(location.search);
let id=params.get("game") || "launcher";
const canvas=$("#screen"),status=$("#status");
let catalog=[],ids=[],titles={launcher:"Launcher"};
let runtime,shell,displayContext,audio,muted=false,paused=false,leaving=false,last=0,accumulator=0,pending=0,ready=false,loading=false;
let files,configs,loadSequence=0,cancelPressed=false,mappingPad=null;
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
const inputNames=["Left","Right","Up","Down","Primary","Secondary","Start","Menu"];
const defaultKeys=["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","KeyX","KeyZ","Enter","Escape"];
const settingsKey="chirky.inputs.v1";
function validKeys(value){return Array.isArray(value) && value.length===8 && new Set(value).size===8 && value.every(code=>typeof code==="string" && /^(Arrow(Left|Right|Up|Down)|Key[A-Z]|Digit[0-9]|Numpad[0-9]|Enter|Escape|Space|Shift(Left|Right)|Control(Left|Right)|Alt(Left|Right)|Backspace|Tab|Comma|Period|Slash|Semicolon|Quote|BracketLeft|BracketRight|Backslash|Minus|Equal|Backquote)$/.test(code));}
function loadInputSettings(){
  try{const saved=JSON.parse(localStorage.getItem(settingsKey));if(saved?.version===1 && validKeys(saved.keys) && ["auto","show","hide"].includes(saved.touch))return saved;}catch{}
  return {version:1,keys:[...defaultKeys],touch:"auto"};
}
let inputSettings=loadInputSettings(),bindings={};
const keyCodes=[...new Set([...defaultKeys,...Array.from({length:26},(_,i)=>"Key"+String.fromCharCode(65+i)),
  ...Array.from({length:10},(_,i)=>"Digit"+i),...Array.from({length:10},(_,i)=>"Numpad"+i),
  "Space","ShiftLeft","ShiftRight","ControlLeft","ControlRight","AltLeft","AltRight","Backspace","Tab",
  "Comma","Period","Slash","Semicolon","Quote","BracketLeft","BracketRight","Backslash","Minus","Equal","Backquote"])];
let padSettings={};
try{const saved=JSON.parse(localStorage.getItem("chirky.controllers.v1"));if(saved && typeof saved==="object" && !Array.isArray(saved))padSettings=saved;}catch{}
function keyName(code){return code.replace(/^Key|^Digit/,"").replace(/^Arrow/,"").replace("Escape","Esc");}
function keycapName(code){
  const names={ArrowLeft:"←",ArrowRight:"→",ArrowUp:"↑",ArrowDown:"↓",Enter:"Enter ↵",Escape:"Esc",
    ShiftLeft:"Shift L",ShiftRight:"Shift R",ControlLeft:"Ctrl L",ControlRight:"Ctrl R",AltLeft:"Alt L",AltRight:"Alt R",
    Backspace:"Backspace",Comma:",",Period:".",Slash:"/",Semicolon:";",Quote:"'",BracketLeft:"[",BracketRight:"]",Backslash:"\\",Minus:"−",Equal:"=",Backquote:"`"};
  return names[code] || keyName(code).replace(/^Numpad/,"Num ");
}
function refreshKeyFeedback(){
  inputSettings.keys.forEach((code,index)=>$("#key-help-"+index).dataset.pressed=String(keys.has(code)));
}
function refreshInputSettings(){
  bindings=Object.fromEntries(inputSettings.keys.map((code,index)=>[code,index]));
  $("#player").setAttribute("data-touch",inputSettings.touch);
  inputSettings.keys.forEach((code,index)=>{
    const key=$("#key-help-"+index);key.textContent=keycapName(code);
    key.setAttribute("aria-label",`${inputNames[index]}: ${keyName(code)}`);
  });
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
  const saved=padSettings[padIdentity(pad)],held=rawPad(pad);
  if(validPadMap(saved))return saved.reduce((mask,b,i)=>mask|(held.some(v=>v.kind===b.kind && v.code===b.code && v.direction===b.direction)?1<<i:0),0);
  let result=0;
  // Standard pads work immediately. Raw USB adapters can be mapped using the
  // keyboard; there is no universal button order for unrecognized SNES pads.
  if(pad.mapping==="standard"){
    const mapping=[4,-1,5,-1,-1,-1,-1,-1,7,6,-1,-1,2,3,0,1];
    pad.buttons.forEach((button,index)=>{if(button.pressed && mapping[index]>=0)result|=1<<mapping[index];});
    if(pad.axes[0]<-.55)result|=1;if(pad.axes[0]>.55)result|=2;
    if(pad.axes[1]<-.55)result|=4;if(pad.axes[1]>.55)result|=8;
  }
  return result;
}
function pads(){return Array.from(navigator.getGamepads?.() || []).filter(Boolean);}
function keyboardMask(){let value=0;for(const key of keys)if(key in bindings)value|=1<<bindings[key];return value;}
function controllerMask(){return pads().reduce((value,pad)=>value|padMask(pad),0);}
function mask(){let value=keyboardMask()|controllerMask();for(const buttons of touch.values())value|=buttons;return value;}
function onRawNames(keyboard){
  if(keyboard)return "KEY "+([...keys].map(keyName).join(" ") || "NONE");
  return "PAD "+(pads().flatMap(p=>rawPad(p).map(v=>v.kind===1?`B${v.code}`:`AX${v.code}${v.direction<0?"NEG":"POS"}`)).join(" ") || "NONE");
}
function onSaveMapping(keyboard,values){
  try{
    if(keyboard){
      const next={...inputSettings,keys:values.map(v=>keyCodes[v.code])};
      if(!validKeys(next.keys))return false;
      localStorage.setItem(settingsKey,JSON.stringify(next));inputSettings=next;refreshInputSettings();
    }else{
      if(!mappingPad || !validPadMap(values))return false;
      const next={...padSettings,[mappingPad]:values};
      localStorage.setItem("chirky.controllers.v1",JSON.stringify(next));padSettings=next;
    }
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
    shell._web_console_tick(mask()|pending|dpadPending,keyboardMask(),controllerMask(),keys.size,
      connected.some(p=>rawPad(p).length),cancelPressed,Math.max(0,...connected.map(p=>p.buttons.filter(b=>b.pressed).length)));
    pending=0;dpadPending=0;cancelPressed=false;
  }
  if(fullscreenQueued)void toggleFullscreen();
}
window.addEventListener("keydown",event=>{
  const capturing=shell?._web_capture_keyboard()===1;
  if(event.code==="F1"){
    event.preventDefault();if(!event.repeat)cancelPressed=true;unlock();return;
  }
  if(capturing || event.code in bindings){
    event.preventDefault();if(event.repeat)return;
    keys.add(event.code);refreshKeyFeedback();unlock();
    if(capturing){const code=keyCodes.indexOf(event.code);if(code>=0)shell._web_capture(1,code,0);}
    else {pending|=1<<bindings[event.code];consoleGesture();}
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
function preventPageZoom(event){if(event.cancelable)event.preventDefault();}
for(const name of ["gesturestart","gesturechange"])
  document.addEventListener(name,preventPageZoom,{passive:false});
document.addEventListener("touchmove",event=>{
  if(event.touches.length>1)preventPageZoom(event);
},{passive:false});
window.addEventListener("blur",()=>{if(runtime)setPaused(true);});
document.addEventListener("visibilitychange",()=>{if(document.hidden && runtime)setPaused(true);});
function resizePlayer(){
  if(typeof innerWidth!=="number")return;
  const fullscreen=!!(document.fullscreenElement || document.webkitFullscreenElement),player=$("#player"),display=$("#display"),slot=$("#screen-slot");
  player.classList.toggle("is-fullscreen",fullscreen);
  const touchVisible=getComputedStyle($(".touch")).display!=="none";
  const sideControls=touchVisible && innerWidth>innerHeight;
  const bottomControls=touchVisible && !sideControls;
  player.classList.toggle("side-controls",sideControls);
  player.classList.toggle("bottom-controls",bottomControls);
  document.body.classList.toggle("controller-wide",sideControls && !fullscreen);
  document.body.classList.toggle("controller-portrait",bottomControls && !fullscreen);
  player.style.setProperty("--player-height",`${window.visualViewport?.height || innerHeight}px`);
  const touchHeight=touchVisible?$(".touch").getBoundingClientRect().height+12:0;
  const availableWidth=Math.max(1,slot.clientWidth-2);
  const availableHeight=sideControls || bottomControls?Math.max(1,slot.clientHeight-2):
    fullscreen?Math.max(1,innerHeight-touchHeight-58):Infinity;
  // CSS pixels can be fractional physical pixels at browser/OS zoom. Quantize
  // the framebuffer's physical scale, then convert back to CSS dimensions.
  const density=window.devicePixelRatio || 1;
  const fit=Math.min(availableWidth/320,availableHeight/240)*density;
  const scale=(fit>=1?Math.floor(fit+1e-6):fit)/density;
  // A fractional CSS width is rounded to layout units before compositing and
  // can still produce uneven pixels. Scale the native-size canvas directly.
  canvas.style.width="320px";canvas.style.height="240px";
  canvas.style.transformOrigin="top left";canvas.style.transform=`scale(${scale})`;
  display.style.width=`${320*scale+2}px`;display.style.height=`${240*scale+2}px`;
  // Centering and borders can place even an integer-size image between pixels.
  display.style.position="relative";display.style.left="0px";display.style.top="0px";
  const rect=canvas.getBoundingClientRect();
  display.style.left=`${Math.round(rect.left*density)/density-rect.left}px`;
  display.style.top=`${Math.round(rect.top*density)/density-rect.top}px`;
  // Relative positioning rounds to CSS layout units. Correct the remaining
  // error in the transform, biasing a third of a physical pixel before the edge.
  // This stays in the same raster pixel while avoiding nearest-neighbour
  // rounding ties that produce alternating widths at fractional Android DPRs.
  const aligned=canvas.getBoundingClientRect();
  canvas.style.transform=`translate(${(Math.round(aligned.left*density)-1/3)/density-aligned.left}px,${(Math.round(aligned.top*density)-1/3)/density-aligned.top}px) scale(${scale})`;
}
document.addEventListener("fullscreenchange",resizePlayer);
document.addEventListener("webkitfullscreenchange",resizePlayer);
window.addEventListener("resize",resizePlayer);
window.visualViewport?.addEventListener("resize",resizePlayer);
if(typeof matchMedia==="function")matchMedia("(any-pointer: coarse)").addEventListener("change",resizePlayer);
// Moving between monitors can change density without changing the CSS viewport.
function watchPixelDensity(){
  if(typeof matchMedia!=="function")return;
  matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`).addEventListener("change",()=>{resizePlayer();watchPixelDensity();},{once:true});
}
watchPixelDensity();
resizePlayer();
function refreshTouchFeedback(){
  let held=0;for(const buttons of touch.values())held|=buttons;
  document.querySelectorAll("[data-button]").forEach(button=>{button.dataset.pressed=String(!!(held&(1<<Number(button.dataset.button))));});
}
const dpad=$(".dpad");
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
  touch.set(dpadPointer,value);dpadPending=value;refreshTouchFeedback();
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
  button.onpointerdown=event=>{if(event.button>0)return;event.preventDefault();button.setPointerCapture(event.pointerId);touch.set(event.pointerId,1<<Number(button.dataset.button));pending|=1<<Number(button.dataset.button);refreshTouchFeedback();unlock();};
  button.onpointerup=event=>{if(touch.has(event.pointerId))consoleGesture();touch.delete(event.pointerId);refreshTouchFeedback();};
  button.onpointercancel=button.onlostpointercapture=event=>{touch.delete(event.pointerId);fullscreenQueued=false;refreshTouchFeedback();};
  button.oncontextmenu=event=>event.preventDefault();
});
function frame(now){
  if(leaving)return;
  const current=mask(),connected=pads();
  const capturing=shell._web_capture_keyboard();
  if(capturing!==0)mappingPad=null;
  if(capturing===0){
    for(const pad of connected){
      const held=rawPad(pad),identity=padIdentity(pad);
      if(!mappingPad && held.length)mappingPad=identity;
      if(mappingPad===identity && held.length){const v=held[0];shell._web_capture(v.kind,v.code,v.direction);break;}
    }
    if(mappingPad && !connected.some(p=>padIdentity(p)===mappingPad))cancelPressed=true;
  }
  if(last)accumulator+=Math.min(now-last,100);
  while(accumulator>=1000/60){
    {
      const gameTick=shell._web_console_tick(current|pending|dpadPending,keyboardMask(),controllerMask(),keys.size,
        connected.some(p=>rawPad(p).length),cancelPressed,Math.max(0,...connected.map(p=>p.buttons.filter(b=>b.pressed).length)));
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
async function checked(url){const response=await fetch(url,{cache:"no-store"});if(!response.ok)throw new Error(`Unable to load ${url} (${response.status})`);return response;}
async function start(){
  const catalogDocument=await (await checked("catalog.json")).json();
  startupProgress(5);
  if(catalogDocument?.version!==1 || !Array.isArray(catalogDocument.games))throw new Error("Invalid game catalog");
  catalog=catalogDocument.games;
  if(!catalog.length || catalog.some(game=>!game || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(game.id) ||
      typeof game.name!=="string" || !["game","diagnostic"].includes(game.role)))throw new Error("Invalid game catalog");
  ids=catalog.map(game=>game.id);
  if(new Set(ids).size!==ids.length)throw new Error("Invalid game catalog");
  titles={launcher:"Launcher",...Object.fromEntries(catalog.map(game=>[game.id,game.name]))};
  files=await (await checked("assets.json")).json();
  configs=await (await checked("configs.json")).json();
  startupProgress(10);
  displayContext=canvas.getContext("webgl",{alpha:false,depth:false,stencil:false,antialias:false});
  if(!displayContext)throw new Error("WebGL is unavailable");
  shell=await createModule("launcher");runtime=shell;
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
async function createModule(gameId){
  const {default:create}=await import(`./${gameId}.js`);
  startupProgress(20);
  const wasmBinary=await startupBinary(`${gameId}.wasm`);
  await startupPaint();
  return create({canvas,wasmBinary,preinitializedWebGLContext:displayContext,
    onSound:playSound,onAssetReady:prepareAssetSound,onAssetSound:playAssetSound,
    onDirectorConnect,onDirectorDisconnect,onDirectorEvent,onDirectorState,
    onLauncherCount:()=>ids.length,onLauncherName:index=>titles[ids[index]],onLauncherId:index=>ids[index],
    onDiagnostic:index=>catalog[index].role==="diagnostic",
    onLaunch:index=>loadGame(ids[index]),onStopped:gameStopped,onLoaded:onModuleLoaded,
    onOption:option=>{if(option===-6)requestConsoleFullscreen();else if(option===-7)toggleMute();},
    onSaveMapping,onRawNames,onSaveRead,onSaveWrite,printErr:message=>console.warn(message)});
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
