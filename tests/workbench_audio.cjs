const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const moduleUrl='data:text/javascript;base64,'+fs.readFileSync('dashboard/public/workbench-audio.js').toString('base64');
async function player(){
 const {WorkbenchAudio}=await import(moduleUrl),created=[];
 const context={state:'running',destination:{},resume:async()=>{},decodeAudioData:async bytes=>({bytes}),
  createGain(){return {gain:{value:1},connect(){},disconnect(){}};},
  createBufferSource(){const source={connect(gain){this.gain=gain.gain;},disconnect(){},start(){this.started=true;},stop(){this.stopped=true;}};created.push(source);return source;}};
 const player=new WorkbenchAudio(()=>context,async()=>({ok:true,arrayBuffer:async()=>new ArrayBuffer(8)}));
 await player.enable('/sounds');return {player,created,context};
}
test('workbench auditions event gain; frozen frames are silent; duplicates and stop match the host',async()=>{
 const {player:p,created,context}=await player();
 const sample=Array(89).fill(0);sample[85]=1;sample[86]=70;
 p.sample(sample);assert.equal(created.length,0);
 p.enabled=true;p.sample(sample);assert.equal(created.length,1);assert.equal(created[0].gain.value,.7);
 p.sample(sample);assert.equal(created.length,1);
 const frozen=sample.slice();frozen[81]=1;frozen[85]=frozen[86]=0;p.sample(frozen);assert.equal(created.length,1);
 p.play(2,80);assert.equal(created.length,2);
 p.play(3,0);assert.equal(created.length,2);
 p.stop();assert(created.every(s=>s.stopped));assert.equal(p.sources.size,0);
 p.sample(sample);created[0].onended();assert.equal(p.sources.size,1);
 context.state='suspended';p.play(4,35);assert.equal(created.length,3);
});
test('audio fetch failure is retryable and never starts playback by itself',async()=>{
 const {WorkbenchAudio}=await import(moduleUrl);let works=false;
 const p=new WorkbenchAudio(()=>({resume:async()=>{},decodeAudioData:async()=>({})}),async()=>({ok:works,arrayBuffer:async()=>new ArrayBuffer(0)}));
 await assert.rejects(p.enable('/sounds'),/Could not load/);works=true;await p.enable('/sounds');
 assert.equal(p.buffers.length,5);assert.equal(p.enabled,false);
});
test('actual workbench transport only auditions running playback, never rebuild or seek',()=>{
 const source=fs.readFileSync('dashboard/public/animation-workbench.js','utf8');
 const section=source.slice(source.indexOf('function rebuild('),source.indexOf('function labelFor('));
 const elements={live:{checked:false},listen:{value:'1'},play:{},gap:{checkValidity:()=>true},'counter-delay':{checkValidity:()=>true}};
 const event=Array(89).fill(0);event[85]=1;event[86]=70;
 const played=[];let stops=0;
 const context=vm.createContext({sound:{stop(){stops++;},sample(s){if(s[85])played.push(s[85]);}},
  document:{hidden:false},playing:true,frame:0,ready:true,valid:true,accumulator:0,
  inputFrames:[0,16,0],samples:[[event],[Array(89).fill(0),event,Array(89).fill(0)]],
  draft:{},reference:{},saved:{},engines:[{},{}],setupEngine(){},snapshot:()=>event.slice(),
  sequence:()=>[],validate(){},same:()=>true,renderTimeline(){},render(){},status(){},
  $:id=>elements[id]??={},keyboard:0,pointers:new Map()});
 vm.runInContext(section,context);
 vm.runInContext('advance();advance();seek(1);',context);assert.deepEqual(played,[1]);
 vm.runInContext('rebuild();',context);assert.deepEqual(played,[1]);assert(stops>=2);
});
