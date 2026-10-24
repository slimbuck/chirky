const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),crypto=require('node:crypto'),zlib=require('node:zlib');
const root='games/circuit-clash/';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function rgba(png) {
  const w=png.readUInt32BE(16),h=png.readUInt32BE(20),stride=w*4,chunks=[];
  assert.equal(png[24],8);assert.equal(png[25],6);assert.equal(png[28],0);
  for(let p=8;p<png.length;) {
    const n=png.readUInt32BE(p);
    if(png.toString('ascii',p+4,p+8)==='IDAT')chunks.push(png.subarray(p+8,p+8+n));
    p+=n+12;
  }
  const raw=zlib.inflateSync(Buffer.concat(chunks)),out=Buffer.alloc(w*h*4);
  let off=0;
  for(let y=0;y<h;y++) {
    const filter=raw[off++];assert(filter<=4);
    for(let x=0;x<stride;x++) {
      const i=y*stride+x,a=x>=4?out[i-4]:0,b=y?out[i-stride]:0,c=y&&x>=4?out[i-stride-4]:0;
      const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);
      out[i]=(raw[off++]+[0,a,b,Math.floor((a+b)/2),pa<=pb&&pa<=pc?a:pb<=pc?b:c][filter])&255;
    }
  }
  return out;
}
test('Arthur runtime copies authored clips exactly, preserving native cells, feet and highlight',()=>{
  const manifest=JSON.parse(fs.readFileSync(root+'art/manifest.json'));
  const layout=JSON.parse(fs.readFileSync(root+'art/clips.json'));
  const width=1536,height=192,columns=16,png=Buffer.alloc(width*height*4);
  let first=0;
  for(const [index,clip] of layout.clips.entries()) {
    const source=fs.readFileSync(root+clip.source),pixels=rgba(source),stride=clip.names.length*96*4;
    assert.equal(hash(source),manifest.clips[index].source_sha256);
    assert.equal(manifest.clips[index].first,first);
    assert.equal(source.readUInt32BE(16),clip.names.length*96);assert.equal(source.readUInt32BE(20),96);
    for(let y=0;y<96;y++)pixels.copy(png,(((Math.floor(first/columns)*96+y)*width)+(first%columns)*96)*4,y*stride,(y+1)*stride);
    first+=clip.names.length;
  }
  assert.equal(first,22);
  assert.deepEqual(manifest.atlas_size,[width,height]);assert.deepEqual(manifest.cell_size,[96,96]);
  assert.deepEqual(manifest.cell_size,manifest.draw_size);
  const pam=fs.readFileSync(root+manifest.runtime),offset=pam.indexOf('ENDHDR\n')+7;
  assert.equal(pam.subarray(0,offset).toString(),`P7\nWIDTH ${width}\nHEIGHT ${height}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n`);
  assert(pam.subarray(offset).equals(png),'Runtime must copy every approved pixel without scaling');
  assert.equal(hash(png),manifest.rgba_sha256);
  const palette=new Set();
  const expectedFeet=layout.clips.flatMap(c=>c.feet_rows);
  for(let f=0;f<22;f++) {
    const soles=[-1,-1];
    for(let y=0;y<96;y++)for(let x=0;x<96;x++) {
      const i=((Math.floor(f/columns)*96+y)*width+(f%columns)*96+x)*4,a=png[i+3];assert(a===0||a===255);
      if(x===0||x===95||y===0||y>=80)assert.equal(a,0,'Transparent gutter');
      if(a){soles[x<48?0:1]=y;palette.add(png.subarray(i,i+3).toString('hex'));}
    }
    assert.deepEqual(soles,expectedFeet[f]);
    assert.deepEqual(manifest.frames[f].feet_rows,soles);
  }
  assert(palette.size<=52);
  for(let y=96;y<height;y++)for(let x=6*96;x<width;x++)assert.equal(png[(y*width+x)*4+3],0,'Unused atlas cells are transparent');
  assert.deepEqual([...png.subarray((34*width+244)*4,(34*width+244)*4+4)],[246,242,240,255]);
  // Attack upper bodies are intentionally reauthored. Freeze the reviewed new
  // pixels above via source hashes; preserve the grounded art below separately.
  for(const id of ['punch','kick']) {
    const clip=manifest.clips.find(c=>c.id===id),reference=rgba(fs.readFileSync(root+clip.planted_reference));
    for(let n=0;n<3;n++) {
      for(let y=clip.edited_region?0:56;y<96;y++)for(let x=0;x<96;x++) {
        const edit=clip.edited_region,box=edit?.bounds;
        if(edit && n===edit.frame && x>=box[0] && x<box[2] && y>=box[1] && y<box[3])continue;
        const a=(y*288+n*96+x)*4,b=(y*width+(clip.first+n)*96+x)*4;
        assert(reference.subarray(a,a+4).equals(png.subarray(b,b+4)),`${id} preserved pixels ${n}:${x},${y}`);
      }
      for(const [x,y] of clip.eye_highlights[n]) {
        const i=(y*width+(clip.first+n)*96+x)*4;
        assert.equal(png[i+3],255);assert(png[i]>=235 && png[i+1]>=235 && png[i+2]>=235,`${id} eye highlight ${n}`);
      }
    }
  }
  const eyes=id=>manifest.clips.find(c=>c.id===id).eye_highlights.map(pair=>pair.reduce((s,p)=>s+p[0],0)/2);
  assert(eyes('kick')[1]<eyes('kick')[0]-4,'Kick head follows the backward torso lean');
  assert(eyes('punch')[1]>eyes('punch')[0]+4,'Punch head follows the shoulder drive');
  const kick=manifest.clips.find(c=>c.id==='kick');
  assert.equal(manifest.frames[kick.first+1].bounds[2]-manifest.anchor[0],kick.attack_extent_px);
  const tuning=fs.readFileSync(root+'assets/characters/arthur.conf','utf8');
  assert.equal(+/^kick_reach_px=(\d+)/m.exec(tuning)[1],kick.attack_extent_px,'Kick reach follows the authored leg');
  for(const id of ['air_punch','air_kick','crouch_punch','crouch_kick']) {
    const clip=manifest.clips.find(c=>c.id===id);
    assert.equal(manifest.frames[clip.first].bounds[2]-48,clip.attack_extent_px);
    assert.equal(+new RegExp('^'+id+'_reach_px=(\\d+)','m').exec(tuning)[1],clip.attack_extent_px);
    if(id.startsWith('air_'))assert(manifest.frames[clip.first].feet_rows.every(y=>y<79),'Air attacks tuck feet above ground');
  }
  // New tucked legs must not drift the approved head, fists, or upper torso.
  for(let y=0;y<54;y++) {
    const a=y*width*4,b=(y*width+11*96)*4;
    assert(png.subarray(a,a+96*4).equals(png.subarray(b,b+96*4)));
  }
  for(let y=16;y<54;y++) {
    const a=y*width*4,b=((y+11)*width+15*96)*4;
    assert(png.subarray(a,a+96*4).equals(png.subarray(b,b+96*4)),'Kneel preserves upper body on an integer offset');
  }
});
test('Tuning schema tracks C validation and all shipped characters and arenas',()=>{
  const schema=JSON.parse(fs.readFileSync(root+'content-schema.json'));
  const header=fs.readFileSync(root+'tuning.h','utf8');
  for(const kind of ['character','stage']) {
    const block=header.split(`#define CC_${kind.toUpperCase()}_FIELDS(X)`)[1].split('\n#define')[0];
    const expected=[...block.matchAll(/X\((\w+),(\d+),(\d+)\)/g)].map(m=>[m[1],+m[2],+m[3]]);
    assert.deepEqual(schema[kind].filter(f=>f.type==='integer').map(f=>[f.key,f.minimum,f.maximum]),expected);
    for(const file of fs.readdirSync(root+`assets/${kind}s`).filter(n=>n.endsWith('.conf'))) {
      const fields=Object.fromEntries(fs.readFileSync(root+`assets/${kind}s/`+file,'utf8').split(/\r?\n/)
        .filter(l=>l && !l.startsWith('#')).map(l=>l.split('=')));
      assert.deepEqual(Object.keys(fields).sort(),schema[kind].map(f=>f.key).sort());
      for(const [key,min,max] of expected)assert(Number.isInteger(+fields[key]) && +fields[key]>=min && +fields[key]<=max,key);
    }
  }
});
