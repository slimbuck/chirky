'use strict';
const game=new URLSearchParams(location.search).get('game'),$=s=>document.querySelector(s);
let schema,current,dirty=false,loading=false;
const status=(text,error=false)=>{$('#status').textContent=text;$('#status').dataset.error=error;};
async function api(suffix,options){const r=await fetch(`/api/games/${encodeURIComponent(game)}/characters${suffix}`,options),j=await r.json();if(!r.ok)throw Error(j.error || 'Request failed');return j;}
const label=key=>key.replace(/_/g,' ').replace(/\bfp8\b/g,'(eighths px)').replace(/\bticks\b/g,'(frames)').replace(/\bpx\b/g,'(px)').replace(/^./,c=>c.toUpperCase());
function numeric(){return Object.fromEntries([...$('#fields').elements].filter(e=>e.name).map(e=>[e.name,e.valueAsNumber]));}
function compare(){
 const values=numeric(),body=$('#comparison tbody');body.replaceChildren();
 for(const move of schema.moves || []) {
  const v=key=>values[move.id+'_'+key],total=v('startup_ticks')+v('active_ticks')+v('recovery_ticks');
  const row=document.createElement('tr');
  for(const text of [move.label,v('startup_ticks'),v('active_ticks'),v('recovery_ticks'),`${total} frames`,v('damage'),`${v('reach_px')} px`,v('blockstun_ticks')-v('active_ticks')-v('recovery_ticks')]) {
   const td=document.createElement('td');td.textContent=text;row.append(td);
  }body.append(row);
 }
}
async function load(id){
 loading=true;$('#save').disabled=true;$('#character').disabled=true;
 try {
  current=await api('/'+encodeURIComponent(id));$('#title').textContent=current.values.name;$('#fields').replaceChildren();
  const fields=schema.character.filter(f=>f.type==='integer' && f.minimum!==f.maximum),used=new Set();
  const groups=(schema.moves || []).map(m=>[m.label,fields.filter(f=>f.key.startsWith(m.id+'_'))]);
  groups.push(['Guard, movement and animation',fields.filter(f=>!(schema.moves || []).some(m=>f.key.startsWith(m.id+'_')))]);
  for(const [name,entries] of groups){
   const fieldset=document.createElement('fieldset');fieldset.className='panel';const legend=document.createElement('legend');legend.textContent=name;fieldset.append(legend);
   const grid=document.createElement('div');grid.className='balance-inputs';
   for(const f of entries){
    if(used.has(f.key))continue;used.add(f.key);
    const l=document.createElement('label'),span=document.createElement('span'),input=document.createElement('input'),hint=document.createElement('small');
    span.textContent=label(f.key.replace(/^(crouch_|air_)?(punch|kick)_/,''));input.type='number';input.name=f.key;input.min=f.minimum;input.max=f.maximum;input.step=1;input.required=true;input.value=current.values[f.key];
    hint.textContent=/^walk_\d_ticks$/.test(f.key)?'Frames at full forward speed; actual travel advances the drawing.':
     /^(push_off|stop_plant|stop_sink|stop_rise)_ticks$/.test(f.key)?'Visual frames only; no input lock. Plant lasts through braking.':
     f.key.endsWith('_followthrough_ticks')?'Extra strike-pose frames during recovery; no hitbox.':`${f.minimum}–${f.maximum}`;l.append(span,input,hint);grid.append(l);
   }fieldset.append(grid);$('#fields').append(fieldset);
  }
  dirty=false;compare();status('Saved values loaded.');
 }finally{loading=false;$('#save').disabled=!current;$('#character').disabled=false;}
}
$('#fields').addEventListener('input',()=>{dirty=true;compare();status('Unsaved changes.');});
$('#fields').addEventListener('submit',e=>e.preventDefault());
$('#save').onclick=async()=>{
 if(!$('#fields').reportValidity())return;$('#save').disabled=true;
 try{const j=await api('/'+encodeURIComponent(current.id),{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({hash:current.hash,values:numeric()})});current.hash=j.hash;dirty=false;status('Saved locally. Restart the browser game to load these values.');}
 catch(e){status(e.message,true);}finally{$('#save').disabled=false;}
};
$('#reload').onclick=()=>{if(!loading && (!dirty || confirm('Discard unsaved changes?')))load(current.id).catch(e=>status(e.message,true));};
$('#character').onchange=()=>{const id=$('#character').value;if(dirty && !confirm('Discard unsaved changes?')){$('#character').value=current.id;return;}load(id).catch(e=>status(e.message,true));};
window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
(async()=>{
 const data=await api('');schema=data.schema;$('#back').href='/#edit/'+encodeURIComponent(game);$('#play').href='/play/?game='+encodeURIComponent(game);
 if(data.workbench){$('#workbench').hidden=false;$('#workbench').href='/animation-workbench.html?game='+encodeURIComponent(game);$('#save').parentElement.append($('#workbench'));}
 for(const c of data.characters){const option=document.createElement('option');option.value=c.id;option.textContent=c.name+' ('+c.id+')';$('#character').append(option);}
 if(!data.characters.length)throw Error('No characters found.');await load(data.characters[0].id);
})().catch(e=>status(e.message,true));
