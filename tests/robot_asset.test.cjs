const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
test('Pocket CRT exports a bounded mesh with an orthonormal face basis and explicit eye/mouth/neck parts',()=>{
  const root='games/phosphor-run/assets/models/';
  const data=fs.readFileSync(root+'player.robot'),info=JSON.parse(fs.readFileSync(root+'model-info.json'));
  assert.equal(data.toString('ascii',0,4),'PRB2');
  const [vertices,triangles,bones,materials,clips,frames]=Array.from({length:6},(_,i)=>data.readUInt32LE(4+i*4));
  assert.equal(vertices,info.vertices);assert.equal(triangles,info.triangles);
  assert(vertices<=2048 && triangles<=4096);assert.equal(bones,3);assert.equal(clips,6);
  assert.equal(data.length,28+84+materials*4+vertices*20+triangles*8+clips*16+frames*bones*48);
  assert.equal(data.length,info.runtime_bytes);
  const rig=info.face_rig,axes=[rig.u,rig.v,rig.normal];
  for(let a=0;a<3;a++)for(let b=0;b<3;b++)assert(Math.abs(axes[a].reduce((v,x,i)=>v+x*axes[b][i],0)-(a===b?1:0))<1e-6);
  const expected=[rig.body_radius,rig.head_pivot_z,rig.neck_min_z,rig.neck_max_z,...rig.origin,...rig.u,...rig.v,...rig.normal,
    rig.half_width,rig.half_height,rig.bulge,rig.eye_spacing,rig.eye_v];
  expected.forEach((value,i)=>assert(Math.abs(value-data.readFloatLE(28+i*4))<1e-6));
  const parts=Array.from({length:5},()=>[]),start=28+84+materials*4;
  for(let i=0;i<vertices;i++){
    const p=start+i*20,part=data.readUInt32LE(p+16),bone=data.readUInt32LE(p+12);
    assert(part<5 && bone<3);if(part)assert.equal(bone,2);
    parts[part].push([data.readFloatLE(p),data.readFloatLE(p+4),data.readFloatLE(p+8)]);
  }
  assert(parts.every(p=>p.length>0));
  // Two wide eyes with a visible gap; the smile is not classified as an eyelid.
  assert(Math.max(...parts[1].map(p=>p[0]))<-.07);
  assert(Math.min(...parts[2].map(p=>p[0]))>.07);
  for(const eye of [parts[1],parts[2]])assert(Math.max(...eye.map(p=>p[2]))-Math.min(...eye.map(p=>p[2]))>.39);
  assert(Math.max(...parts[3].map(p=>p[2]))<Math.min(...parts[1].map(p=>p[2])));
  const objects=info.vertex_parts;assert.equal(objects.mouth,3);assert.equal(objects.neck,4);
});
