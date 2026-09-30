const $=selector=>document.querySelector(selector);
const params=new URLSearchParams(location.search);
let id=params.get("game") || "launcher";
const canvas=$("#screen"),status=$("#status");
let catalog=[],ids=[],titles={launcher:"Launcher"};
let runtime,shell,displayContext,audio,muted=false,paused=false,leaving=false,last=0,accumulator=0,pending=0,ready=false,loading=false;
let files,configs,loadSequence=0,cancelPressed=false,mappingPad=null;
const keys=new Set(),touch=new Map(),sounds=new Map(),sources=new Set();
const assetSounds=new Map();
let director=null;
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
function refreshInputSettings(){
  bindings=Object.fromEntries(inputSettings.keys.map((code,index)=>[code,index]));
  $("#player").setAttribute("data-touch",inputSettings.touch);
  $("#control-help").textContent=inputNames.map((name,index)=>`${name}: ${keyName(inputSettings.keys[index])}`).join(" · ")+" · F1: recovery / cancel mapping · Double-click screen: fullscreen";
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
function mask(){let value=keyboardMask()|controllerMask();for(const button of touch.values())value|=1<<button;return value;}
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
  paused=value;keys.clear();touch.clear();pending=0;last=0;accumulator=0;
  if(value){shell?._web_console_pause();stopSounds();}
}
function toggleMute(){muted=!muted;if(muted)stopSounds();}
async function toggleFullscreen(){
  unlock();try{if(document.fullscreenElement)await document.exitFullscreen();else if($("#player").requestFullscreen)await $("#player").requestFullscreen();else throw new Error("Fullscreen is unavailable in this browser.");}
  catch{status.textContent="Press a keyboard key to select Full Screen, or double-click the screen.";}canvas.focus();
}
window.addEventListener("keydown",event=>{
  const capturing=shell?._web_capture_keyboard()===1;
  if(event.code==="F1"){
    event.preventDefault();if(!event.repeat)cancelPressed=true;unlock();return;
  }
  if(capturing || event.code in bindings){
    event.preventDefault();if(event.repeat)return;
    keys.add(event.code);unlock();
    if(capturing){const code=keyCodes.indexOf(event.code);if(code>=0)shell._web_capture(1,code,0);}
    else pending|=1<<bindings[event.code];
  }
});
window.addEventListener("keyup",event=>keys.delete(event.code));
canvas.addEventListener("pointerdown",()=>{unlock();canvas.focus();});
canvas.addEventListener("dblclick",toggleFullscreen);
window.addEventListener("blur",()=>{if(runtime)setPaused(true);});
document.addEventListener("visibilitychange",()=>{if(document.hidden && runtime)setPaused(true);});
function resizePlayer(){
  if(typeof innerWidth!=="number")return;
  const fullscreen=!!document.fullscreenElement,player=$("#player"),display=$("#display");
  const landscape=fullscreen && innerWidth>innerHeight && getComputedStyle($(".touch")).display!=="none";
  player.classList.toggle("landscape",landscape);
  const touchHeight=getComputedStyle($(".touch")).display==="none"?0:$(".touch").getBoundingClientRect().height+12;
  const touchWidth=landscape?$(".touch").getBoundingClientRect().width+12:0;
  const availableWidth=Math.max(1,fullscreen?innerWidth-18-touchWidth:player.clientWidth-4);
  const availableHeight=fullscreen?Math.max(1,innerHeight-(landscape?0:touchHeight)-24):Infinity;
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
}
document.addEventListener("fullscreenchange",resizePlayer);
window.addEventListener("resize",resizePlayer);
// Moving between monitors can change density without changing the CSS viewport.
function watchPixelDensity(){
  if(typeof matchMedia!=="function")return;
  matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`).addEventListener("change",()=>{resizePlayer();watchPixelDensity();},{once:true});
}
watchPixelDensity();
resizePlayer();
document.querySelectorAll("[data-button]").forEach(button=>{
  button.onpointerdown=event=>{event.preventDefault();button.setPointerCapture(event.pointerId);touch.set(event.pointerId,Number(button.dataset.button));pending|=1<<Number(button.dataset.button);unlock();};
  button.onpointerup=button.onpointercancel=button.onlostpointercapture=event=>touch.delete(event.pointerId);
});
function frame(now){
  if(leaving)return;
  if(loading && cancelPressed){cancelPressed=false;switchGame("launcher");}
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
    if(!loading){
      const gameTick=shell._web_console_tick(current|pending,keyboardMask(),controllerMask(),keys.size,
        connected.some(p=>rawPad(p).length),cancelPressed,Math.max(0,...connected.map(p=>p.buttons.filter(b=>b.pressed).length)));
      if(gameTick && runtime!==shell)runtime._web_tick(current|pending);
    }
    pending=0;cancelPressed=false;accumulator-=1000/60;
  }
  const state=shell._web_console_state(),nextPaused=state!==4;
  if(nextPaused && !paused)stopSounds();paused=nextPaused;
  if(!loading && runtime!==shell && (state===4 || state===5))runtime._web_render();
  if(loading || state!==4)shell._web_render();
  last=now;requestAnimationFrame(frame);
}
async function checked(url){const response=await fetch(url,{cache:"no-store"});if(!response.ok)throw new Error(`Unable to load ${url} (${response.status})`);return response;}
async function start(){
  const catalogDocument=await (await checked("catalog.json")).json();
  if(catalogDocument?.version!==1 || !Array.isArray(catalogDocument.games))throw new Error("Invalid game catalog");
  catalog=catalogDocument.games;
  if(!catalog.length || catalog.some(game=>!game || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(game.id) ||
      typeof game.name!=="string" || !["game","diagnostic"].includes(game.role)))throw new Error("Invalid game catalog");
  ids=catalog.map(game=>game.id);
  if(new Set(ids).size!==ids.length)throw new Error("Invalid game catalog");
  titles={launcher:"Launcher",...Object.fromEntries(catalog.map(game=>[game.id,game.name]))};
  files=await (await checked("assets.json")).json();
  configs=await (await checked("configs.json")).json();
  displayContext=canvas.getContext("webgl",{alpha:false,depth:false,stencil:false,antialias:false});
  if(!displayContext)throw new Error("WebGL is unavailable");
  shell=await createModule("launcher");runtime=shell;
  await loadFiles(shell,"launcher");
  if(!shell.ccall("web_init","number",["string"],[""]))throw new Error("Could not initialise the console");
  ready=true;
  resizePlayer();status.textContent="Launcher";
  if(window===window.top)canvas.focus({preventScroll:true});
  requestAnimationFrame(frame);
  if(id!=="launcher")await switchGame(id,false,params.get("level"));
}
async function createModule(gameId){
  const {default:create}=await import(`./${gameId}.js`);
  return create({canvas,preinitializedWebGLContext:displayContext,
    onSound:playSound,onAssetReady:prepareAssetSound,onAssetSound:playAssetSound,
    onDirectorConnect,onDirectorDisconnect,onDirectorEvent,onDirectorState,
    onLauncherCount:()=>ids.length,onLauncherName:index=>titles[ids[index]],
    onDiagnostic:index=>catalog[index].role==="diagnostic",
    onLaunch:index=>switchGame(ids[index]),onReturn:()=>switchGame("launcher"),
    onOption:option=>{if(option===-4)toggleFullscreen();else if(option===-5)toggleMute();},
    onSaveMapping,onRawNames,printErr:message=>console.warn(message)});
}
async function loadFiles(module,gameId){
  const loadedSounds=new Map();
  await Promise.all(files.filter(file=>gameId==="launcher"?file.startsWith("assets/launcher/"):file.startsWith(`games/${gameId}/`)).map(async file=>{
    let bytes;
    if(file.endsWith(".conf")){
      if(typeof configs[file]!=="string")throw new Error(`Missing game configuration: ${file}`);
      bytes=new TextEncoder().encode(configs[file]);
    }else bytes=new Uint8Array(await (await checked("runtime/"+file)).arrayBuffer());
    module.FS.mkdirTree("/"+file.slice(0,file.lastIndexOf("/")));module.FS.writeFile("/"+file,bytes);
    if(file.endsWith(".wav"))loadedSounds.set(file,bytes);
  }));
  return loadedSounds;
}
async function switchGame(nextId,push=true,level=null){
  const game=catalog.find(game=>game.id===nextId);
  if(nextId!=="launcher" && !game){status.textContent="Unknown game";return;}
  const sequence=++loadSequence;loading=true;stopSounds();onDirectorDisconnect();
  if(runtime && runtime!==shell)runtime._web_destroy();
  runtime=shell;assetSounds.clear();sounds.clear();keys.clear();touch.clear();pending=0;
  shell._web_console_game(0,0,0);status.textContent=nextId==="launcher"?"Launcher":`Loading ${game.name}…`;
  let module;
  try{
    if(nextId!=="launcher"){
      module=await createModule(nextId);const loadedSounds=await loadFiles(module,nextId);
      // An abandoned download has no GL resources or running game to destroy.
      // Calling its teardown would disconnect the new game's director session.
      if(sequence!==loadSequence)return;
      runtime=module;
      for(const [path,bytes] of loadedSounds)sounds.set(path,bytes);
      const config=`games/${nextId}/game.conf`;
      if(game.levelSetting && level && /^\d+$/.test(level)){
        const text=runtime.FS.readFile(config,{encoding:"utf8"}),setting=game.levelSetting;
        runtime.FS.writeFile(config,text.replace(new RegExp(`^${setting}=.*$`,"m"),"")+`\n${setting}=${Number(level)}\n`);
      }
      if(!runtime.ccall("web_init","number",["string"],[config]))throw new Error("Could not initialise the game");
      shell._web_console_game(1,game.role==="diagnostic"?1:0,catalog.indexOf(game));
    }
    id=nextId;
    if(push){const url=new URL(location.href);url.search=nextId==="launcher"?"":`?game=${nextId}`;history.pushState(null,"",url);}
    status.textContent=titles[id];
  }catch(error){
    if(sequence!==loadSequence)return;
    if(module)module._web_destroy();runtime=shell;shell._web_console_game(0,0,0);
    status.textContent=error.message;console.error(error);
  }finally{
    if(sequence===loadSequence){loading=false;last=0;accumulator=0;canvas.focus({preventScroll:true});}
  }
}
window.addEventListener("popstate",()=>{const query=new URLSearchParams(location.search);switchGame(query.get("game") || "launcher",false,query.get("level"));});
window.addEventListener("pagehide",()=>{leaving=true;++loadSequence;stopSounds();if(ready){if(runtime!==shell)runtime._web_destroy();shell._web_destroy();}audio?.close();});
window.addEventListener("pageshow",event=>{if(event.persisted)location.reload();});
canvas.addEventListener("webglcontextlost",event=>{event.preventDefault();leaving=true;stopSounds();status.textContent="Display connection lost. Reload to continue.";});
start().catch(error=>{status.textContent=error.message;console.error(error);});
