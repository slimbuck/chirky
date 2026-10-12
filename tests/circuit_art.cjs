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
test('Arthur runtime pixels equal approved art; four padded native cells, aligned feet and highlight',()=>{
  const manifest=JSON.parse(fs.readFileSync(root+'art/manifest.json'));
  const source=fs.readFileSync(root+manifest.source),png=rgba(source);
  assert.equal(hash(source),manifest.source_sha256);
  assert.deepEqual(manifest.atlas_size,[384,96]);assert.deepEqual(manifest.cell_size,[96,96]);
  assert.deepEqual(manifest.cell_size,manifest.draw_size);
  const pam=fs.readFileSync(root+manifest.runtime),offset=pam.indexOf('ENDHDR\n')+7;
  assert.equal(pam.subarray(0,offset).toString(),'P7\nWIDTH 384\nHEIGHT 96\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n');
  assert(pam.subarray(offset).equals(png),'Runtime must copy every approved pixel without scaling');
  assert.equal(hash(png),manifest.rgba_sha256);
  const palette=new Set();
  for(let f=0;f<4;f++) {
    const soles=[-1,-1];
    for(let y=0;y<96;y++)for(let x=0;x<96;x++) {
      const i=(y*384+f*96+x)*4,a=png[i+3];assert(a===0||a===255);
      if(x===0||x===95||y===0||y>=80)assert.equal(a,0,'Transparent gutter');
      if(a){soles[x<48?0:1]=y;palette.add(png.subarray(i,i+3).toString('hex'));}
    }
    assert.deepEqual(soles,[79,79]);
  }
  assert(palette.size<=52);
  assert.deepEqual([...png.subarray((34*384+244)*4,(34*384+244)*4+4)],[246,242,240,255]);
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
