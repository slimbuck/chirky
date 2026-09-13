"use strict";
function defaults(games) {
  return [...games.filter(g=>g.id!=="hardware-test").sort((a,b)=>a.name.localeCompare(b.name)).map(g=>({section:"root",id:g.id,label:g.name.slice(0,24),visible:true})),
    {section:"root",id:"settings",label:"Settings",visible:true},{section:"root",id:"power",label:"Power Down",visible:true},
    {section:"settings",id:"input",label:"Input Settings",visible:true},{section:"settings",id:"display",label:"Display Area",visible:true},{section:"settings",id:"hardware",label:"Hardware Test",visible:true}];
}
function validate(items,games) {
  const expected=defaults(games);const keys=new Set(expected.map(e=>`${e.section}/${e.id}`));
  if(!Array.isArray(items) || items.length!==expected.length)throw new Error("Include every launcher item exactly once.");
  for(const item of items) {
    const key=`${item.section}/${item.id}`;
    if(!keys.delete(key))throw new Error("Unknown or duplicate launcher item.");
    if(typeof item.visible!=="boolean" || typeof item.label!=="string" || !/^[A-Za-z0-9 .?/-]{1,24}$/.test(item.label) || !item.label.trim())
      throw new Error("Use a name of 1–24 letters, numbers, spaces, dots, slashes, hyphens, or question marks.");
  }
  for(const section of ["root","settings"])if(!items.some(e=>e.section===section && e.visible))throw new Error("Keep at least one visible item in each menu.");
  return items.map(({section,id,label,visible})=>({section,id,label,visible}));
}
function parse(text,games) {
  const items=text.split(/\r?\n/).filter(line=>line && !line.startsWith("#")).map(line=>{
    const parts=line.split("|");if(parts.length!==4 || !["0","1"].includes(parts[2]))throw new Error("Invalid launcher file.");
    return {section:parts[0],id:parts[1],visible:parts[2]==="1",label:parts[3]};
  });return validate(items,games);
}
function serialize(items,games) {return validate(items,games).map(e=>`${e.section}|${e.id}|${e.visible?1:0}|${e.label}`).join("\n")+"\n";}
module.exports={defaults,validate,parse,serialize};
