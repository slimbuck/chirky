"use strict";
const {test}=require("node:test"),assert=require("node:assert/strict");
const {games}=require("../tools/web-assets");
const {isBrowserPlayerFile}=require("./server");

test("dashboard play route serves every packaged browser game",()=>{
  assert.equal(isBrowserPlayerFile("catalog.json"),true);
  for(const icon of ["favicon.svg","favicon.ico"])assert.equal(isBrowserPlayerFile(icon),true);
  for(const id of ["launcher",...games]) {
    assert.equal(isBrowserPlayerFile(`${id}.js`),true,id);
    assert.equal(isBrowserPlayerFile(`${id}.wasm`),true,id);
  }
  for(const name of ["unknown.wasm","../player.js","bramble-hollow.conf"])
    assert.equal(isBrowserPlayerFile(name),false,name);
});
