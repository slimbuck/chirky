"use strict";
const {test}=require("node:test"),assert=require("node:assert/strict");
const fs=require("node:fs"),os=require("node:os"),path=require("node:path");
const settings=require("./robot-preview");
test("robot settings reject stale and malformed writes without changing the saved file",()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),"chirky-robot-settings-")),file=path.join(dir,"robot.conf");
  try{
    const values={ambient:.78,brightness:1.35,head_lead:.32,body_lag:.22,idle_look:.2,blink:1};
    fs.writeFileSync(file,Object.entries(values).map(([k,v])=>`${k}=${v}`).join("\n")+"\n");
    const original=settings.read(file);
    assert.deepEqual(original.values,values);
    for(const patch of [{brightness:99},{brightness:"1.2"},{blink:.5},{ambient:NaN},{head_lead:-1},{extra:1}]){
      assert.throws(()=>settings.save({hash:original.hash,values:{...values,...patch}},file),{status:400});
      assert.equal(settings.read(file).hash,original.hash);
    }
    const result=settings.save({hash:original.hash,values:{...values,brightness:1.4}},file);
    assert.equal(settings.read(file).values.brightness,1.4);assert.notEqual(result.hash,original.hash);
    assert.throws(()=>settings.save(original,file),{status:409});
  }finally{fs.rmSync(dir,{recursive:true,force:true});}
});
