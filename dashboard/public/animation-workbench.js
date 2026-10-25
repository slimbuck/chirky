const $=id=>document.getElementById(id);
const game=new URLSearchParams(location.search).get('game');
const phaseNames=['Idle','Startup','Active','Follow-through','Recovery'];
const phaseClasses=['idle','startup','active','followthrough','recovery'];
const motionNames=['Idle','Push-off','Walk','Plant / brake','Sink','Rise'];
const groups=[
 ['Punch contact',[
  ['punch_hitstop_ticks','Hit-stop','Extra frozen frames after a hit; both fighters'],
  ['punch_blockstop_ticks','Block hit-stop','Extra frozen frames on guarded contact'],
  ['punch_knockback_px','Knockback','Total travel in pixels at normal weight'],
  ['punch_recoil_ticks','Recoil duration','Ease-out travel; 0 = instant. Capped below stun length'],
  ['punch_hitstun_ticks','Hitstun','Recovery frames for the opponent, excluding hit-stop'],
  ['punch_blockstun_ticks','Blockstun','Opponent’s guard recovery, excluding hit-stop']]],
 ['Hit reaction artwork',[
  ['hit_reaction_style','Reaction style','Grounded hits only; movement and stun stay unchanged'],
  ['hit_impact_ticks','Impact pose','Frames after hit-stop; short stuns truncate the drawing holds'],
  ['hit_recover_ticks','Recovery pose','Final stun frames; reserves a recoil frame when possible']]],
 ['Ground movement',[
  ['walk_speed_fp8','Forward speed','Eighths of a pixel / frame'],['back_speed_fp8','Retreat speed','Eighths of a pixel / frame'],
  ['ground_accel_fp8','Acceleration','Eighths of a pixel / frame²'],['ground_brake_fp8','Braking','Eighths of a pixel / frame²']]],
 ['Start & stop',[
  ['push_off_ticks','Push-off','Planted preparation before the passing foot (frames)'],
  ['stop_plant_ticks','Plant','Minimum frames; held longer while braking'],
  ['stop_sink_ticks','Sink','Absorb the stop (frames)'],
  ['stop_rise_ticks','Rise to idle','Return to neutral (frames)']]],
 ['Punch rhythm',[
  ['punch_startup_ticks','Wind-up','Frames before the hit window'],['punch_active_ticks','Active hitbox','Frames that can connect'],
  ['punch_followthrough_ticks','Extended follow-through','Recovery frames holding the strike pose'],['punch_recovery_ticks','Total recovery','Includes follow-through; at least 1 retract frame']]],
 ['Idle drawings',[0,1,2,3].map((n)=>[`idle_${n}_ticks`,['Neutral','Descend','Low','Rise'][n],'Hold in frames'])],
 ['Walk drawings',[0,1,2,3].map((n)=>[`walk_${n}_ticks`,['Stance','Lead lift','Wide contact','Rear follow'][n],'Frames at full forward speed; slower travel stretches the hold'])]
];
let schema,descriptor,saved,baseline,draft,reference,hash,character,library=[],factory,atlas;
let engines=[],samples=[[],[]],inputFrames=[],frame=0,facing=1,playing=true,ready=false,valid=true;
let last=0,accumulator=0,keyboard=0,pointers=new Map(),takesKey='';
let renderPending=false;
const canvas=[$('screen-a'),$('screen-b')],contexts=canvas.map(c=>c.getContext('2d'));
const status=(text,error=false)=>{$('status').textContent=text;$('status').dataset.error=error;};
async function json(url,options){const r=await fetch(url,options),body=await r.json();if(!r.ok)throw Error(body.error||r.statusText);return body;}
const api=(suffix='',options)=>json(`/api/games/${encodeURIComponent(game)}/characters${suffix}`,options);
function validate(values){
 for(const f of schema.character.filter(f=>f.type==='integer'))if(!Number.isInteger(+values[f.key]) || +values[f.key]<f.minimum || +values[f.key]>f.maximum)throw Error(`${f.key.replace(/_ticks$/,' (frames)').replaceAll('_',' ')} must be ${f.minimum}–${f.maximum}.`);
 for(const rule of schema.constraints){const [,a,op,b]=/^(\w+) (<=|<) (\w+)$/.exec(rule);if(!(op==='<'?+values[a]<+values[b]:+values[a]<=+values[b]))throw Error(`${a.replace(/_ticks$/,'').replaceAll('_',' ')} must be ${op==='<'?'less than':'no greater than'} ${b.replace(/_ticks$/,'').replaceAll('_',' ')}.`);}
}
function config(values){return schema.character.map(f=>`${f.key}=${values[f.key]}`).join('\n')+'\n';}
function numeric(values){return Object.fromEntries(schema.character.filter(f=>f.type==='integer' && f.minimum!==f.maximum).map(f=>[f.key,+values[f.key]]));}
function same(a,b){return JSON.stringify(numeric(a))===JSON.stringify(numeric(b));}
function snapshot(engine){const p=engine._wb_state()/4;return Array.from(engine.HEAP32.subarray(p,p+85));}
function setupEngine(engine,values){engine.FS.writeFile('/character.conf',config(values));engine.FS.writeFile('/stage.conf',descriptor.stage);if(!engine._wb_load())throw Error('The game rejected this character configuration.');engine._wb_configure(+$('opponent').value,+$('gap').value);engine._wb_reset(facing);}
function sequence(){
 const names=['idle','step','stop','tail'],v={};
 for(const name of names){const el=$('seq-'+name);if(!el.checkValidity())throw Error('Sequence lengths must be whole frames within the displayed limits.');v[name]=+el.value;}
 const attackLength=values=>+values.punch_startup_ticks + +values.punch_active_ticks + Math.max(+values.punch_recovery_ticks,+values.punch_hitstun_ticks,+values.punch_blockstun_ticks) + Math.max(+values.punch_hitstop_ticks,+values.punch_blockstop_ticks);
 const punch=v.idle+v.step+v.stop,total=punch+Math.max(attackLength(draft),attackLength(reference))+v.tail;
 return Array.from({length:total},(_,t)=>t>=v.idle&&t<v.idle+v.step?(facing>0?2:1):t===punch?16:0);
}
function rebuild(keepFrame=true){
 if(!ready)return;
 try{
  validate(draft);validate(reference);
  if(!$('gap').checkValidity())throw Error('Starting gap must be 40–160 whole pixels.');
  const nextInput=$('live').checked?inputFrames:sequence();
  engines.forEach((engine,i)=>setupEngine(engine,i===0?reference:draft));
  samples=engines.map(engine=>[snapshot(engine)]);inputFrames=nextInput;
  for(const mask of inputFrames)engines.forEach((engine,i)=>{engine._wb_step(mask);samples[i].push(snapshot(engine));});
  frame=keepFrame?Math.min(frame,inputFrames.length):0;valid=true;
  $('save').disabled=same(draft,saved);$('keep-take').disabled=false;
  $('draft-badge').textContent=same(draft,saved)?'Saved values':'Unsaved draft';
  renderTimeline();render();
 }catch(e){valid=false;pause();$('save').disabled=true;$('keep-take').disabled=true;status(e.message+' Preview retains the last valid take.',true);}
}
function pause(){playing=false;accumulator=0;$('play').textContent='Play';}
function seek(n){pause();frame=Math.max(0,Math.min(inputFrames.length,n));render();}
function activeMask(){let mask=keyboard;for(const v of pointers.values())mask|=v;return mask;}
function advance(){
 if($('live').checked){
  if(frame<inputFrames.length){frame++;return;}
  if(inputFrames.length>=600){pause();status('600 frames recorded. Scrub to review, or Restart for a new recording.');return;}
  const mask=activeMask();inputFrames.push(mask);
  engines.forEach((engine,i)=>{engine._wb_step(mask);samples[i].push(snapshot(engine));});frame++;
  renderTimeline();
 }else frame=(frame+1)%samples[1].length;
}
function labelFor(s){return s[34]<=0?'KO':s[35]?(s[36]?'Blockstun':s[37]<s[38]?'Recoil':'Hit recovery'):s[36]?'Guard':s[9]?phaseNames[s[9]]:s[32]?motionNames[s[32]]:s[1]?'Airborne':descriptor.manifest.frames[s[4]].clip.replaceAll('_',' ');}
function motionClass(s){return s[36]?'guard':s[35]?'hit':s[9]?phaseClasses[s[9]]:s[32]>=3?'stop':s[32]?'walk':'idle';}
function drawSprite(ctx,s,alpha=1){
 const [x,y,w,h,sx,sy,sw,sh,flip]=s.slice(14,23),dx=s[26]+x,dy=s[29]-s[27]-y-h;
 ctx.save();ctx.globalAlpha=alpha;ctx.imageSmoothingEnabled=false;
 if(flip){ctx.translate(dx+w,dy);ctx.scale(-1,1);ctx.drawImage(atlas,sx,sy,sw,sh,0,0,w,h);}
 else ctx.drawImage(atlas,sx,sy,sw,sh,dx,dy,w,h);
 ctx.restore();
}
function drawView(i){
 const ctx=contexts[i],s=samples[i][frame];if(!s || !atlas)return;
 const w=s[28],h=s[29],safeX=s[26],safeY=s[27],ground=h-safeY-s[13];
 if(canvas[i].width!==w || canvas[i].height!==h){canvas[i].width=w;canvas[i].height=h;}
 ctx.fillStyle='#080c12';ctx.fillRect(0,0,w,h);ctx.save();ctx.beginPath();ctx.rect(safeX,safeY,s[30],s[31]);ctx.clip();
 ctx.fillStyle='#111c2a';ctx.fillRect(safeX,safeY,s[30],s[31]);
 if($('guides').checked){
  ctx.fillStyle='#1e2c3e';for(let x=safeX;x< w-safeX;x+=16)ctx.fillRect(x,safeY,1,s[31]);
  for(let y=ground;y>safeY;y-=16)ctx.fillRect(safeX,y,s[30],1);
  ctx.fillStyle='#627e8d';ctx.fillRect(safeX,ground,s[30],1);
  const origin=safeX+Math.trunc(samples[i][0][0]/8);ctx.fillStyle='#405768';ctx.fillRect(origin,ground-6,1,13);
 }
 for(const p of (s[83]?[0,1]:[0])) {
  const f=s.slice(p*40,p*40+40);
  if($('onion').checked){for(const n of [frame-2,frame+2])if(samples[i][n])drawSprite(ctx,samples[i][n].slice(p*40,p*40+40),.2);}
  drawSprite(ctx,f);
  const x=safeX+Math.trunc(f[0]/8),y=ground-Math.trunc(f[1]/8);
  ctx.fillStyle=p?'#ff795c':'#52dccb';ctx.fillRect(x-8,ground+3,16,2);
  if($('guides').checked){ctx.fillRect(x-3,y,7,1);ctx.fillRect(x,y-3,1,7);}
  if($('boxes').checked){
   ctx.lineWidth=1;ctx.strokeStyle=p?'#ff795c':'#6ca3c5';ctx.strokeRect(x-f[12]+.5,y-f[11]+.5,f[12]*2,f[11]);
   if(f[8]){ctx.strokeStyle='#f3b845';ctx.strokeRect((f[5]>0?x:x-f[23])+.5,y-f[25]+.5,f[23],f[25]-f[24]);}
  }
 }
 ctx.restore();
 const pose=descriptor.manifest.frames[s[4]],dx=(s[0]-samples[i][0][0])/8;
 const opponent=s.slice(40,80),contact=s[82]?(s[82]&3?'HIT':'BLOCK'):s[81]?'HIT-STOP':'';
 $('readout-'+(i?'b':'a')).textContent=`${contact?contact+' · ':''}P1 ${labelFor(s)} / ${pose.name} · x ${dx.toFixed(2)} px · v ${(s[2]/8).toFixed(2)} px/f`+
  (s[83]?` | P2 ${labelFor(opponent)} · HP ${opponent[34]} · recoil ${opponent[37]}/${opponent[38]} · stun ${opponent[35]} · freeze left ${s[80]}`:'');
}
function render(){
 if(!ready || !samples[1].length)return;
 drawView(1);if($('compare').checked)drawView(0);
 $('scrub').max=inputFrames.length;$('scrub').value=frame;$('frame-number').value=`${frame} / ${inputFrames.length}`;
 document.querySelectorAll('.timeline-cursor').forEach(el=>el.style.left=`${(frame+.5)/(inputFrames.length+1)*100}%`);
}
function track(name,entries){
 const row=document.createElement('div');row.className='track';const title=document.createElement('span');title.className='track-name';title.textContent=name;
 const strip=document.createElement('div');strip.className='track-frames';strip.title='Click to seek to a frame';
 // One equal-width column per sample, including frame zero. Label lengths and
 // one-frame segments must never stretch a row or shrink neighbouring frames.
 strip.style.gridTemplateColumns=`repeat(${entries.length},minmax(0,1fr))`;
 let begin=0;
 for(let n=1;n<=entries.length;n++)if(n===entries.length || entries[n][0]!==entries[begin][0] || entries[n][1]!==entries[begin][1]){
  const segment=document.createElement('span');segment.className='segment '+entries[begin][1];segment.style.gridColumn=`${begin+1} / ${n+1}`;
  segment.textContent=entries[begin][0];segment.title=`${entries[begin][0]} · frames ${begin}–${n-1} (${n-begin})`;strip.append(segment);begin=n;
 }
 const cursor=document.createElement('i');cursor.className='timeline-cursor';strip.append(cursor);
 strip.onclick=e=>{const r=strip.getBoundingClientRect();seek(Math.floor((e.clientX-r.left)/r.width*entries.length));};row.append(title,strip);return row;
}
function renderTimeline(){
 const rows=[track('Input',[["Ready",'idle'],...inputFrames.map((m,n)=>{
  const seqStop=+$('seq-idle').value + +$('seq-step').value;
  return m&16?['Punch','active']:m&3?['Step','walk']:m?['Input '+m,'walk']:
   !$('live').checked&&n>=seqStop&&n<seqStop + +$('seq-stop').value?['Stop','stop']:['Idle','idle'];
 })])];
 for(const i of ($('compare').checked?[0,1]:[1])){
  const name=i?'B':'A';
  rows.push(track(name+' contact',samples[i].map(s=>s[82]?[s[82]&3?'Hit':'Blocked','freeze']:s[81]?['Hit-stop','freeze']:['','idle'])));
  for(const p of (samples[i][0][83]?[0,1]:[0])) {
   rows.push(track(name+' P'+(p+1)+' motion',samples[i].map(s=>{const f=s.slice(p*40,p*40+40);return [labelFor(f),motionClass(f)];})));
   rows.push(track(name+' P'+(p+1)+' pose',samples[i].map(s=>{const f=s.slice(p*40,p*40+40);return [descriptor.manifest.frames[f[4]].name,motionClass(f)];})));
  }
 }
 $('timeline').replaceChildren(...rows);
}
function fit(){
 for(const c of canvas){if(c.closest('[hidden]'))continue;const box=c.parentElement,dpr=devicePixelRatio||1,space=Math.max(1,box.clientWidth-18);
  const scale=space*dpr>=c.width?Math.max(1,Math.floor(space*dpr/c.width)):space*dpr/c.width;
  c.style.width=c.width*scale/dpr+'px';c.style.height=c.height*scale/dpr+'px';
  c.style.transform='none';const r=c.getBoundingClientRect();
  c.style.transform=`translate(${(Math.round(r.left*dpr)-r.left*dpr)/dpr}px,${(Math.round(r.top*dpr)-r.top*dpr)/dpr}px)`;
 }
}
function fields(){
 $('tuning').replaceChildren();
 for(const [name,items] of groups){const group=document.createElement('fieldset');group.className='tuning-group';const legend=document.createElement('legend');legend.textContent=name;group.append(legend);
  for(const [key,label,hint] of items){const f=schema.character.find(f=>f.key===key);if(!f)continue;
   const row=document.createElement('label');row.className='tuning-field';const span=document.createElement('span');span.textContent=label;const small=document.createElement('small');small.textContent=hint;span.append(small);
   const input=document.createElement(key==='hit_reaction_style'?'select':'input');input.name=key;
   if(key==='hit_reaction_style')for(const [value,text] of ['Borrowed poses','Restrained','Exaggerated'].entries()){const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);}
   else Object.assign(input,{type:'number',min:f.minimum,max:f.maximum,step:1});
   input.value=draft[key];input.required=true;
   input.oninput=()=>{draft[key]=+input.value;status('Unsaved draft. Preview updated; saved character unchanged.');rebuild();};row.append(span,input);group.append(row);
  }$('tuning').append(group);
 }
}
function loadLibrary(){
 try{library=JSON.parse(localStorage.getItem(takesKey)||'[]');if(!Array.isArray(library))library=[];}catch{library=[];}
 // Older takes inherit newly introduced fields from the current character.
 library=library.filter(t=>typeof t.name==='string'&&typeof t.id==='string'&&
  (+t.version>=7 && +t.version<=+saved.schema_version)&&
  t.values&&typeof t.values==='object').slice(-20);
}
function takeValues(base,take){return {...base,...(+take.version<10?{hit_reaction_style:0}:{}),...take.values};}
function libraryOptions(){
 const refValue=$('reference').value,takeValue=$('takes').value;
 for(const [id,label] of [['reference','Saved at load'],['takes','Choose a take']]){const select=$(id);select.replaceChildren();const option=document.createElement('option');option.value=id==='reference'?'saved':'';option.textContent=label;select.append(option);
  for(const take of library){const option=document.createElement('option');option.value=take.id;option.textContent=take.name;select.append(option);}
 }$('reference').value=library.some(t=>t.id===refValue)?refValue:'saved';$('takes').value=library.some(t=>t.id===takeValue)?takeValue:'';
 $('load-take').disabled=!$('takes').value;
}
async function loadAtlas(path){
 const r=await fetch(`/play/runtime/games/${encodeURIComponent(game)}/${path}`);if(!r.ok)throw Error('Could not load the character atlas.');
 const bytes=new Uint8Array(await r.arrayBuffer()),header=new TextDecoder().decode(bytes.subarray(0,200));
 const end=header.indexOf('ENDHDR\n')+7,w=Number(/WIDTH (\d+)/.exec(header)?.[1]),h=Number(/HEIGHT (\d+)/.exec(header)?.[1]);
 if(end<7||!header.startsWith('P7\n')||!header.includes('DEPTH 4\n')||!header.includes('MAXVAL 255\n')||w!==descriptor.manifest.atlas_size[0]||h!==descriptor.manifest.atlas_size[1]||bytes.length-end!==w*h*4)throw Error('Unsupported runtime sprite atlas.');
 const image=document.createElement('canvas');image.width=w;image.height=h;image.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(bytes.slice(end)),w,h),0,0);return image;
}
async function loadCharacter(id){
 ready=false;$('character').disabled=true;
 try{
  const data=await api('/'+encodeURIComponent(id));saved=data.values;baseline={...saved};draft={...saved};reference={...baseline};hash=data.hash;character=id;
  atlas=await loadAtlas(saved.atlas);takesKey=`chirky.workbench.v1.${game}.${id}`;loadLibrary();libraryOptions();$('reference').value='saved';
  fields();inputFrames=[];frame=0;ready=true;valid=true;rebuild(false);fit();status('Saved character loaded. Edits stay in draft until you save.');
  for(const id of ['play','restart','previous','next','scrub','keep-take','reset-draft'])$(id).disabled=false;
 }finally{$('character').disabled=false;}
}
$('play').onclick=()=>{if(!ready||!valid)return;playing=!playing;accumulator=0;$('play').textContent=playing?'Pause':'Play';};
$('restart').onclick=()=>{keyboard=0;pointers.clear();if($('live').checked)inputFrames=[];frame=0;rebuild(false);accumulator=0;};
$('previous').onclick=()=>seek(frame-1);
$('next').onclick=()=>{pause();if(ready&&valid){if($('live').checked&&frame===inputFrames.length)advance();else frame=Math.min(inputFrames.length,frame+1);render();}};
$('scrub').oninput=()=>seek(+$('scrub').value);
$('facing').onclick=()=>{facing=-facing;$('facing').textContent=facing===1?'Face left':'Face right';if($('live').checked)inputFrames=[];rebuild(false);};
$('compare').onchange=()=>{$('reference-view').hidden=!$('compare').checked;$('views').classList.toggle('compare',$('compare').checked);fit();renderTimeline();render();};
for(const id of ['onion','guides','boxes'])$(id).onchange=render;
for(const id of ['idle','step','stop','tail'])$('seq-'+id).oninput=()=>{rebuild();};
for(const id of ['opponent','gap'])$(id).onchange=()=>rebuild(false);
$('miss').onclick=()=>{$('opponent').value='1';$('gap').value=120;rebuild(false);};
$('contact').onclick=()=>{const n=samples[1].findIndex(s=>s[82]);if(n>=0)seek(n);else status('No contact in this recording. Reduce the gap or step farther.');};
$('live').onchange=()=>{keyboard=0;pointers.clear();inputFrames=[];$('live-pad').hidden=!$('live').checked;document.querySelectorAll('.sequence-fields input').forEach(e=>e.disabled=$('live').checked);rebuild(false);$('views').focus();};
const keys={ArrowLeft:1,ArrowRight:2,ArrowUp:4,ArrowDown:8,KeyN:16,KeyM:32};
window.addEventListener('keydown',e=>{if(!$('live').checked||!keys[e.code]||/INPUT|SELECT|TEXTAREA/.test(e.target.tagName))return;e.preventDefault();keyboard|=keys[e.code];});
window.addEventListener('keyup',e=>{if(keys[e.code])keyboard&=~keys[e.code];});
window.addEventListener('blur',()=>{keyboard=0;pointers.clear();});
document.querySelectorAll('[data-mask]').forEach(button=>{
 button.onpointerdown=e=>{e.preventDefault();button.setPointerCapture(e.pointerId);pointers.set(e.pointerId,+button.dataset.mask);$('views').focus();};
 button.onpointerup=button.onpointercancel=button.onlostpointercapture=e=>pointers.delete(e.pointerId);
});
$('reference').onchange=()=>{const take=library.find(t=>t.id===$('reference').value);reference=take?takeValues(baseline,take):{...baseline};rebuild();};
$('takes').onchange=()=>{$('load-take').disabled=!$('takes').value;};
$('keep-take').onclick=()=>{
 try{validate(draft);const name=$('take-name').value.trim();if(!name)throw Error('Give this take a name first.');
  const take={id:crypto.randomUUID(),name,version:saved.schema_version,values:numeric(draft)};const next=[...library,take].slice(-20);
  localStorage.setItem(takesKey,JSON.stringify(next));library=next;const previousReference=$('reference').value;libraryOptions();
  if(previousReference!==$('reference').value){reference={...baseline};rebuild();}
  $('takes').value=take.id;$('load-take').disabled=false;status(`Kept “${name}” in this browser. Select it as reference A to compare.`);
 }catch(e){status(e.message,true);}
};
$('load-take').onclick=()=>{const take=library.find(t=>t.id===$('takes').value);if(!take)return;draft=takeValues(saved,take);fields();rebuild();if(valid)status(`Loaded “${take.name}” into draft B.`);};
$('reset-draft').onclick=()=>{draft={...saved};fields();rebuild();status('Draft B restored to the saved character.');};
$('save').onclick=async()=>{
 $('save').disabled=true;
 try{validate(draft);const values=numeric(draft),result=await api('/'+encodeURIComponent(character),{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({hash,values})});hash=result.hash;saved={...saved,...values};$('draft-badge').textContent=same(draft,saved)?'Saved values':'Unsaved draft';
  status('Character saved locally. Reference A is kept for comparison; reload the page to refresh it.');
 }catch(e){status(e.message,true);}finally{$('save').disabled=!valid||same(draft,saved);}
};
$('character').onchange=()=>{if(ready&&!same(draft,saved)&&!confirm('Discard the unsaved draft? Keep it as a named take first to preserve it.')){$('character').value=character;return;}loadCharacter($('character').value).catch(e=>status(e.message,true));};
window.addEventListener('beforeunload',e=>{if(ready&&!same(draft,saved)){e.preventDefault();e.returnValue='';}});
new ResizeObserver(()=>{if(!renderPending){renderPending=true;requestAnimationFrame(()=>{renderPending=false;fit();});}}).observe($('views'));
window.addEventListener('resize',fit);
function animate(now){
 const dt=last?Math.min(.1,(now-last)/1000):0;last=now;
 if(ready&&valid&&playing&&!document.hidden){accumulator+=dt*60*+$('speed').value;while(accumulator>=1&&playing){advance();accumulator-=1;}render();}
 requestAnimationFrame(animate);
}
// Read-only diagnostics for integration checks and graphics experiments.
window.animationWorkbench=Object.freeze({get ready(){return ready;},get frame(){return frame;},get samples(){return samples.map(list=>list.map(s=>s.slice()));},get inputs(){return inputFrames.slice();},get valid(){return valid;}});
try{
 const data=await api();schema=data.schema;if(!data.workbench)throw Error('This game has no animation workbench.');
 descriptor=await json(`/api/games/${encodeURIComponent(game)}/animation-workbench`);
 $('balance-link').href='/balance.html?game='+encodeURIComponent(game);$('game-link').href='/play/?game='+encodeURIComponent(game);
 const response=await fetch(descriptor.module,{method:'GET'});if(!response.ok)throw Error('Workbench module is missing. Run: wsl make animation-workbench NODE=node.exe');
 factory=(await import(descriptor.module)).default;engines=await Promise.all([factory(),factory()]);
 for(const item of data.characters){const option=document.createElement('option');option.value=item.id;option.textContent=item.name;$('character').append(option);}
 if(!data.characters.length)throw Error('No characters available.');await loadCharacter(data.characters[0].id);requestAnimationFrame(animate);
}catch(e){status(e.message,true);}
