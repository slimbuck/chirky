// The simulation owns cue timing. This player only auditions recorded events.
export class WorkbenchAudio {
 constructor(contextFactory=()=>new AudioContext(),request=(...args)=>fetch(...args)){
  this.contextFactory=contextFactory;this.request=request;this.context=null;
  this.buffers=[];this.sources=new Set();this.enabled=false;this.loading=null;this.base='';
 }
 async enable(base){
  this.context??=this.contextFactory();
  await this.context.resume();
  if(this.base!==base){this.base=base;this.loading=null;this.buffers=[];}
  this.loading??=Promise.all(['punch','kick','block','miss'].map(async name=>{
   const response=await this.request(`${base}/${name}.wav`);
   if(!response.ok)throw Error(`Could not load ${name} sound.`);
   return this.context.decodeAudioData(await response.arrayBuffer());
  })).then(buffers=>{this.buffers=[null,...buffers];}).catch(error=>{this.loading=null;throw error;});
  await this.loading;
 }
 stop(){for(const source of this.sources)source.stop();this.sources.clear();}
 play(cue,volume){
  if(!this.enabled || this.context?.state!=='running' || !this.buffers[cue] || !Number.isFinite(volume) || volume<=0)return;
  const buffer=this.buffers[cue];
  for(const source of this.sources)if(source.buffer===buffer)return;
  if(this.sources.size>=8){const oldest=this.sources.values().next().value;oldest.stop();this.sources.delete(oldest);}
  const source=this.context.createBufferSource(),gain=this.context.createGain();
  source.buffer=buffer;gain.gain.value=Math.min(100,volume)/100;source.connect(gain);gain.connect(this.context.destination);
  source.onended=()=>{this.sources.delete(source);source.disconnect();gain.disconnect();};
  this.sources.add(source);source.start();
 }
 sample(sample){for(let p=0;p<2;p++)this.play(sample[85+p*2],sample[86+p*2]);}
}
