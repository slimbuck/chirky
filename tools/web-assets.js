"use strict";
const fs=require("fs"),path=require("path");
const ROOT=path.resolve(__dirname,"..");
const games=["phosphor-run","rosey-chop","hardware-test"];
function assets() {
  const files=["assets/launcher/splash.ppm"];
  function walk(relative) {
    for(const entry of fs.readdirSync(path.join(ROOT,relative),{withFileTypes:true})) {
      const name=relative+"/"+entry.name;
      if(entry.isDirectory())walk(name);
      else if(entry.isFile() && /\.(conf|txt|sprite|ppm|wav)$/.test(name))files.push(name);
    }
  }
  games.forEach(id=>walk("games/"+id));
  return files.sort();
}
module.exports={assets,games,ROOT};
if(require.main===module) {
  const destination=path.join(ROOT,"build/web");fs.mkdirSync(destination,{recursive:true});
  for(const name of ["index.html","player.js","style.css"])fs.copyFileSync(path.join(ROOT,"web",name),path.join(destination,name));
  const files=assets();
  for(const name of files) {
    const target=path.join(destination,"runtime",name);fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.copyFileSync(path.join(ROOT,name),target);
  }
  fs.writeFileSync(path.join(destination,"assets.json"),JSON.stringify(files));
}
