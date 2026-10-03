const $=id=>document.getElementById(id),canvas=$("screen");
const fields={ambient:["Fill light",0,1,.01],brightness:["Colour brightness",.5,1.8,.01],head_lead:["Jump head lead",0,.6,.01],body_lag:["Jump body delay",0,.4,.01],idle_look:["Idle glance",0,.5,.01],blink:["Blink",0,1,1]};
const periods=[192,120,48,36,24,48];
const period=()=>periods[Number($("clip").value)];
let runtime,saved,values,hash,playing=true,tick=0,facing=1,last=0,ready=false;
let faceFraction=0,blinkRequested=0,faceStep=0;
for(const [key,[label,min,max,step]] of Object.entries(fields)){
  const row=document.createElement("label");row.htmlFor=key;row.textContent=label;
  const out=document.createElement("output");out.id=key+"-value";row.append(out);
  const input=document.createElement("input");Object.assign(input,{id:key,type:"range",min,max,step});
  input.oninput=()=>{values[key]=Number(input.value);out.value=input.value;};$("tuning").append(row,input);
}
function showValues(){for(const key of Object.keys(fields)){$(key).value=values[key];$(key+"-value").value=values[key];}}
function status(text,error=false){$("status").textContent=text;$("status").classList.toggle("error",error);}
async function request(url,options){const r=await fetch(url,options);if(!r.ok)throw Error((await r.json()).error||r.statusText);return r;}
async function model(){const bytes=await (await request("/play/runtime/games/phosphor-run/assets/models/player.robot")).arrayBuffer();runtime.FS.writeFile("games/phosphor-run/assets/models/player.robot",new Uint8Array(bytes));}
function fit(){const display=canvas.parentElement,stage=display.parentElement,density=window.devicePixelRatio||1;
  const scale=Math.max(1,Math.floor((stage.clientWidth-16)*density/320))/density;
  display.style.width=320*scale+"px";display.style.height=240*scale+"px";
  canvas.style.width="320px";canvas.style.height="240px";canvas.style.transformOrigin="top left";canvas.style.transform=`scale(${scale})`;
}
new ResizeObserver(fit).observe(canvas.parentElement.parentElement);
$("play").onclick=()=>{playing=!playing;$("play").textContent=playing?"Pause":"Play";};
function pause(){playing=false;$("play").textContent="Play";}
$("step").onclick=()=>{pause();tick=(Math.floor(tick)+1)%period();};
$("restart").onclick=()=>{tick=0;};$("clip").onchange=()=>{tick=0;$("tick").max=period()-1;};
$("tick").oninput=()=>{pause();tick=Number($("tick").value);};
$("face-blink").onclick=()=>{blinkRequested=1;};
$("face-step").onclick=()=>{$("face-playing").checked=false;faceStep++;};
$("facing").onclick=()=>{facing=-facing;$("facing").textContent=facing===1?"Face left":"Face right";};
$("scale").oninput=()=>{$("scale-value").value=$("scale").value+"×";};
$("reset").onclick=()=>{values={...saved};showValues();status("Restored saved settings.");};
$("save").onclick=async()=>{try{const next={...values};const result=await (await request("/api/robot-preview",{method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify({values:next,hash})})).json();hash=result.hash;saved=next;status("Saved to game. Refresh the browser player to see your changes.");}catch(e){status(e.message,true);}};
$("reload").onclick=async()=>{try{await model();if(!runtime._preview_reload())throw Error("Model is invalid. Check your export.");status("Loaded latest exported model.");}catch(e){status(e.message,true);}};
function frame(now){if(!ready)return;const dt=last?Math.min((now-last)/1000,.1):0;last=now;
  if(playing)tick=(tick+dt*60*Number($("speed").value))%period();
  $("tick").value=Math.floor(tick);$("tick-value").value=Math.floor(tick);
  runtime._preview_style(...Object.keys(fields).map(key=>values[key]));
  if($("face-playing").checked)faceFraction+=dt*60*Number($("speed").value);
  const faceSteps=Math.floor(faceFraction)+faceStep;faceFraction-=Math.floor(faceFraction);faceStep=0;
  $("face-tick").value=runtime._preview_face(faceSteps,Number($("expression").value),blinkRequested,Number($("clip").value));blinkRequested=0;
  if(!runtime._preview_render(Number($("clip").value),tick,facing,Number($("scale").value))){status("GPU preview failed.",true);ready=false;return;}
  requestAnimationFrame(frame);
}
try{
  const settings=await (await request("/api/robot-preview")).json();saved=settings.values;values={...saved};hash=settings.hash;showValues();
  const {default:createPreview}=await import("/robot-preview/preview.js").catch(()=>{throw Error("Build the viewer first: wsl make robot-preview NODE=node.exe");});
  runtime=await createPreview({canvas,locateFile:file=>"/robot-preview/"+file});
  runtime.FS.mkdirTree("games/phosphor-run/assets/models");await model();
  if(!runtime._preview_init())throw Error("Unable to initialize GPU preview.");
  ready=true;for(const id of ["save","reset","reload"])$(id).disabled=false;
  status("Live GPU preview · local changes appear immediately");fit();requestAnimationFrame(frame);
}catch(e){status(e.message,true);}

