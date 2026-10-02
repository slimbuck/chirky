"use strict";
const fs=require("node:fs"),path=require("node:path");
const {textHash,writeAtomic}=require("./editors");
const settingsFile=path.join(__dirname,"../games/phosphor-run/assets/models/robot.conf");
const bounds={ambient:[0,1],brightness:[.5,1.8],head_lead:[0,.6],body_lag:[0,.4],idle_look:[0,.5],blink:[0,1]};
function read(file=settingsFile){
  const text=fs.readFileSync(file,"utf8"),values={};
  for(const line of text.split(/\r?\n/)){
    const [key,value]=line.split("=");if(Object.hasOwn(bounds,key))values[key]=Number(value);
  }
  return {hash:textHash(text),values};
}
function save(body,file=settingsFile){
  if(body.hash!==read(file).hash)throw Object.assign(Error("Settings changed on disk. Reload this page before saving."),{status:409});
  if(!body.values || Object.keys(body.values).length!==Object.keys(bounds).length)
    throw Object.assign(Error("Incomplete settings"),{status:400});
  for(const [key,[lo,hi]] of Object.entries(bounds)){
    const value=body.values[key];
    if(!Number.isFinite(value) || value<lo || value>hi || (key==="blink" && !Number.isInteger(value)))
      throw Object.assign(Error("Invalid "+key),{status:400});
  }
  const text=Object.keys(bounds).map(k=>`${k}=${body.values[k]}`).join("\n")+"\n";
  writeAtomic(file,text);return {ok:true,hash:textHash(text)};
}
module.exports={read,save};
