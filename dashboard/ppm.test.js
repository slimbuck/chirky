"use strict";
const {test}=require("node:test");
const assert=require("node:assert/strict");
const {decodePpm}=require("./public/ppm");
function fixture(header,pixels) { return Buffer.concat([Buffer.from(header),Buffer.from(pixels)]); }
test("snapshot preserves leading black and whitespace-valued pixel bytes",()=>{
  const pixels=[0,0,0,9,10,13,32,35,0,255,128,64];
  const result=decodePpm(fixture("P6\n2 2\n255\n",pixels));
  assert.equal(result.width,2);assert.equal(result.height,2);
  assert.deepEqual([...result.pixels],pixels);
});
test("entirely black snapshot retains every pixel",()=>{
  assert.deepEqual([...decodePpm(fixture("P6\n1 1\n255\n",[0,0,0])).pixels],[0,0,0]);
});
test("header comments do not consume binary pixels",()=>{
  assert.deepEqual([...decodePpm(fixture("P6\n# capture\n1 1\n255\n",[35,10,32])).pixels],[35,10,32]);
});
test("rejects truncated pixels and invalid dimensions",()=>{
  assert.throws(()=>decodePpm(fixture("P6\n2 1\n255\n",[0,0,0])),/pixels/);
  assert.throws(()=>decodePpm(fixture("P6\n0 1\n255\n",[])),/dimensions/);
});
