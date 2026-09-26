const $=selector=>document.querySelector(selector);
const ids=["phosphor-run","rosey-chop","hardware-test"];
const titles={"launcher":"Launcher","phosphor-run":"Phosphor Run","rosey-chop":"Rosey Chop","hardware-test":"Hardware Test"};
const params=new URLSearchParams(location.search),id=params.get("game") || "launcher";
const canvas=$("#screen"),status=$("#status");
let runtime,audio,muted=false,paused=false,leaving=false,last=0,accumulator=0,pending=0,selectHeld=false,ready=false;
const keys=new Set(),touch=new Map(),sounds=new Map(),sources=new Set();
const assetSounds=new Map();
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
const bindings={ArrowLeft:0,ArrowRight:1,ArrowUp:2,ArrowDown:3,KeyZ:4,KeyX:5,Enter:5,KeyC:6,KeyV:7,KeyA:8,KeyS:9,Space:10,Escape:11};
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
  const mapping=[5,6,4,7,8,9,-1,-1,11,10,-1,-1,2,3,0,1];
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
  status.textContent=(paused?"Paused · ":"")+titles[id];
  if(paused)stopSounds();else canvas.focus();
}
canvas.addEventListener("keydown",event=>{if(event.code in bindings){event.preventDefault();if(event.repeat && event.code==="Escape")return;if(!event.repeat)pending|=1<<bindings[event.code];keys.add(event.code);unlock();}});
window.addEventListener("keyup",event=>keys.delete(event.code));
canvas.addEventListener("pointerdown",()=>{unlock();canvas.focus();});
window.addEventListener("blur",()=>{if(runtime)setPaused(true);});
document.addEventListener("visibilitychange",()=>{if(document.hidden && runtime)setPaused(true);});
$("#pause").onclick=()=>{unlock();setPaused(!paused);};
$("#restart").onclick=()=>location.reload();
$("#mute").onclick=()=>{muted=!muted;if(muted)stopSounds();$("#mute").textContent=muted?"Unmute":"Mute";$("#mute").setAttribute("aria-pressed",String(muted));};
$("#fullscreen").onclick=()=>{$("#display").requestFullscreen().catch(error=>{status.textContent=error.message;});canvas.focus();unlock();};
document.querySelectorAll("[data-button]").forEach(button=>{
  button.onpointerdown=event=>{event.preventDefault();button.setPointerCapture(event.pointerId);touch.set(event.pointerId,Number(button.dataset.button));pending|=1<<Number(button.dataset.button);unlock();};
  button.onpointerup=button.onpointercancel=button.onlostpointercapture=event=>touch.delete(event.pointerId);
});
function frame(now){
  if(leaving)return;
  const current=mask(),select=((current|pending)&(1<<11))!==0;
  if(id==="hardware-test" && ((current|pending)&(1<<6))){leaving=true;location.href="./";return;}
  if(select && !selectHeld && id!=="hardware-test")setPaused(!paused);
  selectHeld=(current&(1<<11))!==0;
  if(!paused){
    if(last)accumulator+=Math.min(now-last,100);
    while(accumulator>=1000/60 && !leaving){runtime._web_tick(current|pending);pending=0;accumulator-=1000/60;}
    runtime._web_render();
  }
  last=now;requestAnimationFrame(frame);
}
async function checked(url){const response=await fetch(url,{cache:"no-store"});if(!response.ok)throw new Error(`Unable to load ${url} (${response.status})`);return response;}
async function start(){
  if(!Object.hasOwn(titles,id))throw new Error("Unknown game");
  const files=await (await checked("assets.json")).json();
  const configs=id==="launcher"?{}:await (await checked("configs.json")).json();
  const {default:create}=await import(`./${id}.js`);
  runtime=await create({canvas,onSound:playSound,onAssetReady:prepareAssetSound,onAssetSound:playAssetSound,
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
  if(id==="phosphor-run" && level && /^\d+$/.test(level)){
    const text=runtime.FS.readFile(config,{encoding:"utf8"}).replace(/^start_level=.*$/m,"");
    runtime.FS.writeFile(config,text+`\nstart_level=${Number(level)}\n`);
  }
  if(!runtime.ccall("web_init","number",["string"],[config]))throw new Error("Could not initialise the game or WebGL display");
  ready=true;
  status.textContent=titles[id];
  // Embedded games must not steal focus or scroll their parent page.
  if(window===window.top)canvas.focus({preventScroll:true});
  requestAnimationFrame(frame);
}
window.addEventListener("pagehide",()=>{leaving=true;stopSounds();if(ready)runtime._web_destroy();audio?.close();});
window.addEventListener("pageshow",event=>{if(event.persisted)location.reload();});
canvas.addEventListener("webglcontextlost",event=>{event.preventDefault();leaving=true;stopSounds();status.textContent="Display connection lost. Restart to continue.";});
start().catch(error=>{status.textContent=error.message;$("#pause").disabled=true;console.error(error);});
