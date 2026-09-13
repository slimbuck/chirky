"use strict";
const {test}=require("node:test"),assert=require("node:assert/strict");
const {defaults,parse,serialize,validate}=require("./launcher");
const games=[{id:"hardware-test",name:"Hardware Test"},{id:"rosey-chop",name:"Rosey Chop"},{id:"phosphor-run",name:"Phosphor Run"}];
test("shared launcher roundtrips reordered, renamed, and hidden entries",()=>{
  const items=defaults(games);[items[0],items[1]]=[items[1],items[0]];items[0].label="Garden";items[1].visible=false;
  assert.deepEqual(parse(serialize(items,games),games),items);
  assert.equal(items[0].id,"rosey-chop");assert.equal(items.length,7);
});
test("rejects duplicate, missing, unsupported labels and empty menus",()=>{
  let items=defaults(games);items[0]=items[1];assert.throws(()=>validate(items,games),/duplicate/);
  assert.throws(()=>validate(defaults(games).slice(1),games),/every/);
  items=defaults(games);items[0].label="Bad|label";assert.throws(()=>validate(items,games),/name/);
  items=defaults(games).map(e=>({...e,visible:e.section!=="settings"}));assert.throws(()=>validate(items,games),/visible/);
});
test("rejects malformed visibility and extra file fields",()=>{
  assert.throws(()=>parse("root|settings|true|Settings",games),/Invalid/);
  assert.throws(()=>parse("root|settings|1|Settings|extra",games),/Invalid/);
});
