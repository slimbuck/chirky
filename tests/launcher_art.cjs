const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),crypto=require('node:crypto'),zlib=require('node:zlib');
test('launcher approved assets preserve native dimensions, palette, identity colours and gutters',()=>{
  const manifest=JSON.parse(fs.readFileSync('assets/launcher/manifest.json'));
  for(const [file,hash] of Object.entries(manifest.hashes))assert.equal(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),hash,file);
  assert.deepEqual(manifest.cell,[64,64]);assert.equal(manifest.renderScale,1);
  const ppm=fs.readFileSync('assets/launcher/mascot.ppm'),header=Buffer.from('P6\n64 64\n255\n');
  assert(ppm.subarray(0,header.length).equals(header));assert.equal(ppm.length,header.length+64*64*3);
  const palette=new Set(),rgb=[0,0,0];
  for(let p=header.length;p<ppm.length;p+=3){const [r,g,b]=ppm.subarray(p,p+3);palette.add(`${r},${g},${b}`);if(r>g*1.5&&r>b*1.5)rgb[0]++;if(g>r*1.3&&g>b*1.2)rgb[1]++;if(b>r*1.5&&b>g*1.3)rgb[2]++;}
  assert(palette.size<=manifest.paletteMax);assert(rgb.every(count=>count>64*64*.02),'red, green and blue feathers survive palette reduction');
  const png=fs.readFileSync('assets/launcher/mascot.png');assert.equal(png.readUInt32BE(16),64);assert.equal(png.readUInt32BE(20),64);assert.equal(png[24],8);assert.equal(png[25],6);
  const chunks=[];for(let p=8;p<png.length;){const n=png.readUInt32BE(p);if(png.toString('ascii',p+4,p+8)==='IDAT')chunks.push(png.subarray(p+8,p+8+n));p+=n+12;}
  const raw=zlib.inflateSync(Buffer.concat(chunks));let previous=Buffer.alloc(256),offset=0,baseline=-1;
  for(let y=0;y<64;y++){
    const filter=raw[offset++],row=Buffer.alloc(256);
    for(let x=0;x<256;x++){const a=x>=4?row[x-4]:0,b=previous[x],c=x>=4?previous[x-4]:0,p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);row[x]=(raw[offset++]+[0,a,b,Math.floor((a+b)/2),pa<=pb&&pa<=pc?a:pb<=pc?b:c][filter])&255;}
    for(let x=0;x<64;x++){const alpha=row[x*4+3];assert(alpha===0||alpha===255);if(alpha)baseline=y;if(x<2||x>=62||y<2||y>=62)assert.equal(alpha,0);}
    previous=row;
  }
  assert.equal(baseline,manifest.baseline);
});
