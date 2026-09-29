const $=selector=>document.querySelector(selector);
const params=new URLSearchParams(location.search),id=params.get("game") || "launcher";
const canvas=$("#screen"),status=$("#status");
let catalog=[],ids=[],titles={launcher:"Launcher"},selectedGame=null;
let runtime,audio,muted=false,paused=false,leaving=false,last=0,accumulator=0,pending=0,selectHeld=false,ready=false;
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
let inputSettings=loadInputSettings(),bindings={},draftSettings=null,captureButton=null,settingsOpen=false;
function keyName(code){return code.replace(/^Key|^Digit/,"").replace(/^Arrow/,"").replace("Escape","Esc");}
function refreshInputSettings(){
  bindings=Object.fromEntries(inputSettings.keys.map((code,index)=>[code,index]));
  $("#player").setAttribute("data-touch",inputSettings.touch);
  $("#control-help").textContent=inputNames.map((name,index)=>`${name}: ${keyName(inputSettings.keys[index])}`).join(" · ");
}
function refreshDraft(){
  document.querySelectorAll("[data-bind]").forEach(button=>{const index=Number(button.dataset.bind);button.textContent=`${inputNames[index]}: ${keyName(draftSettings.keys[index])}`;});
  $("#touch-mode").value=draftSettings.touch;
}
function cancelCapture(){captureButton=null;$("#cancel-binding").hidden=true;$("#binding-message").textContent="";}
function closeSettings(){cancelCapture();settingsOpen=false;draftSettings=null;$("#settings").close();canvas.focus();resizePlayer();}
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
function mask(){
  let value=0;for(const key of keys)value|=1<<bindings[key];for(const button of touch.values())value|=1<<button;
  const mapping=[4,-1,5,-1,-1,-1,-1,-1,7,6,-1,-1,2,3,0,1];
  for(const pad of navigator.getGamepads?.() || [])if(pad?.mapping==="standard"){
    pad.buttons.forEach((button,index)=>{if(button.pressed && mapping[index]>=0)value|=1<<mapping[index];});
    if(pad.axes[0]<-.45)value|=1;if(pad.axes[0]>.45)value|=2;
    if(pad.axes[1]<-.45)value|=4;if(pad.axes[1]>.45)value|=8;
  }
  return value;
}
function setPaused(value){
  paused=value;keys.clear();touch.clear();pending=0;last=0;accumulator=0;
  $("#pause").textContent=paused?"Resume":"Pause";
  status.textContent=(paused?"Paused · ":"")+(titles[id] || id);
  if(paused)stopSounds();else canvas.focus();
}
canvas.addEventListener("keydown",event=>{if(!settingsOpen && event.code in bindings){event.preventDefault();if(event.repeat && bindings[event.code]===7)return;if(!event.repeat)pending|=1<<bindings[event.code];keys.add(event.code);unlock();}});
window.addEventListener("keyup",event=>keys.delete(event.code));
canvas.addEventListener("pointerdown",()=>{unlock();canvas.focus();});
window.addEventListener("blur",()=>{if(runtime)setPaused(true);});
document.addEventListener("visibilitychange",()=>{if(document.hidden && runtime)setPaused(true);});
$("#pause").onclick=()=>{unlock();setPaused(!paused);};
$("#restart").onclick=()=>location.reload();
$("#mute").onclick=()=>{muted=!muted;if(muted)stopSounds();$("#mute").textContent=muted?"Unmute":"Mute";$("#mute").setAttribute("aria-pressed",String(muted));};
$("#fullscreen").onclick=async()=>{
  unlock();try{if(document.fullscreenElement)await document.exitFullscreen();else if($("#player").requestFullscreen)await $("#player").requestFullscreen();else throw new Error("Fullscreen is unavailable in this browser.");}catch(error){status.textContent=error.message;}canvas.focus();
};
function resizePlayer(){
  if(typeof innerWidth!=="number")return;
  const fullscreen=!!document.fullscreenElement,player=$("#player"),display=$("#display");
  const landscape=fullscreen && innerWidth>innerHeight && getComputedStyle($(".touch")).display!=="none";
  player.classList.toggle("landscape",landscape);
  const touchHeight=getComputedStyle($(".touch")).display==="none"?0:$(".touch").getBoundingClientRect().height+12;
  const touchWidth=landscape?$(".touch").getBoundingClientRect().width+12:0;
  const availableWidth=Math.max(1,fullscreen?innerWidth-18-touchWidth:player.clientWidth-4);
  const availableHeight=fullscreen?Math.max(1,innerHeight-player.querySelector("nav").getBoundingClientRect().height-(landscape?0:touchHeight)-32):Infinity;
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
document.addEventListener("fullscreenchange",()=>{const full=!!document.fullscreenElement;$("#fullscreen").textContent=full?"Exit full screen":"Full screen";$("#fullscreen").setAttribute("aria-pressed",String(full));resizePlayer();});
window.addEventListener("resize",resizePlayer);
// Moving between monitors can change density without changing the CSS viewport.
function watchPixelDensity(){
  if(typeof matchMedia!=="function")return;
  matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`).addEventListener("change",()=>{resizePlayer();watchPixelDensity();},{once:true});
}
watchPixelDensity();
resizePlayer();
$("#input-settings").onclick=()=>{setPaused(true);settingsOpen=true;draftSettings={...inputSettings,keys:[...inputSettings.keys]};cancelCapture();refreshDraft();$("#settings").showModal();};
document.querySelectorAll("[data-bind]").forEach(button=>{button.onclick=()=>{captureButton=Number(button.dataset.bind);$("#binding-message").textContent=`Press a key for ${inputNames[captureButton]}.` ;$("#cancel-binding").hidden=false;};});
$("#settings").addEventListener("keydown",event=>{
  if(captureButton===null)return;
  event.preventDefault();event.stopPropagation();if(event.repeat)return;
  const next=[...draftSettings.keys];next[captureButton]=event.code;
  if(!validKeys(next)){$("#binding-message").textContent=draftSettings.keys.includes(event.code)?"That key is already assigned. Choose another key.":"Choose a letter, number, arrow, or control key.";return;}
  draftSettings.keys=next;cancelCapture();refreshDraft();
});
$("#settings").addEventListener("cancel",event=>{event.preventDefault();if(captureButton!==null)cancelCapture();else closeSettings();});
$("#cancel-binding").onclick=cancelCapture;
$("#cancel-settings").onclick=closeSettings;
$("#reset-bindings").onclick=()=>{cancelCapture();draftSettings={version:1,keys:[...defaultKeys],touch:"auto"};refreshDraft();};
$("#touch-mode").onchange=()=>{draftSettings.touch=$("#touch-mode").value;};
$("#save-settings").onclick=()=>{
  if(captureButton!==null)return;
  try{localStorage.setItem(settingsKey,JSON.stringify(draftSettings));}catch{$("#binding-message").textContent="Could not save settings. Allow browser storage and try again.";return;}
  inputSettings=draftSettings;refreshInputSettings();closeSettings();
};
document.querySelectorAll("[data-button]").forEach(button=>{
  button.onpointerdown=event=>{event.preventDefault();if(settingsOpen)return;button.setPointerCapture(event.pointerId);touch.set(event.pointerId,Number(button.dataset.button));pending|=1<<Number(button.dataset.button);unlock();};
  button.onpointerup=button.onpointercancel=button.onlostpointercapture=event=>touch.delete(event.pointerId);
});
function frame(now){
  if(leaving)return;
  const current=settingsOpen?0:mask(),select=!settingsOpen && ((current|pending)&(1<<7))!==0;
  if(selectedGame?.role==="diagnostic" && select){leaving=true;location.href="./";return;}
  if(select && !selectHeld && selectedGame?.role!=="diagnostic")setPaused(!paused);
  selectHeld=(current&(1<<7))!==0;
  if(paused)pending=0;
  if(!paused){
    if(last)accumulator+=Math.min(now-last,100);
    while(accumulator>=1000/60 && !leaving){runtime._web_tick(current|pending);pending=0;accumulator-=1000/60;}
    runtime._web_render();
  }
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
  selectedGame=catalog.find(game=>game.id===id) || null;
  if(id!=="launcher" && !selectedGame)throw new Error("Unknown game");
  const files=await (await checked("assets.json")).json();
  const configs=id==="launcher"?{}:await (await checked("configs.json")).json();
  const {default:create}=await import(`./${id}.js`);
  runtime=await create({canvas,onSound:playSound,onAssetReady:prepareAssetSound,onAssetSound:playAssetSound,
    onDirectorConnect,onDirectorDisconnect,onDirectorEvent,onDirectorState,
    onLauncherCount:()=>ids.length,onLauncherName:index=>titles[ids[index]],
    onLaunch:index=>{leaving=true;location.href=`?game=${ids[index]}`;},printErr:message=>console.warn(message)});
  await Promise.all(files.filter(file=>id==="launcher"?file.startsWith("assets/launcher/"):file.startsWith(`games/${id}/`)).map(async file=>{
    let bytes;
    if(file.endsWith(".conf")){
      if(typeof configs[file]!=="string")throw new Error(`Missing game configuration: ${file}`);
      bytes=new TextEncoder().encode(configs[file]);
    }else bytes=new Uint8Array(await (await checked("runtime/"+file)).arrayBuffer());
    runtime.FS.mkdirTree("/"+file.slice(0,file.lastIndexOf("/")));runtime.FS.writeFile("/"+file,bytes);
    if(file.endsWith(".wav"))sounds.set(file,bytes);
  }));
  const config=`games/${id}/game.conf`;
  const level=params.get("level");
  if(selectedGame?.levelSetting && level && /^\d+$/.test(level)){
    const text=runtime.FS.readFile(config,{encoding:"utf8"});
    const setting=selectedGame.levelSetting;
    runtime.FS.writeFile(config,text.replace(new RegExp(`^${setting}=.*$`,"m"),"")+`\n${setting}=${Number(level)}\n`);
  }
  if(!runtime.ccall("web_init","number",["string"],[config]))throw new Error("Could not initialise the game or WebGL display");
  ready=true;
  resizePlayer();
  status.textContent=titles[id];
  // Embedded games must not steal focus or scroll their parent page.
  if(window===window.top)canvas.focus({preventScroll:true});
  requestAnimationFrame(frame);
}
window.addEventListener("pagehide",()=>{leaving=true;stopSounds();if(ready)runtime._web_destroy();audio?.close();});
window.addEventListener("pageshow",event=>{if(event.persisted)location.reload();});
canvas.addEventListener("webglcontextlost",event=>{event.preventDefault();leaving=true;stopSounds();status.textContent="Display connection lost. Restart to continue.";});
start().catch(error=>{status.textContent=error.message;$("#pause").disabled=true;console.error(error);});
