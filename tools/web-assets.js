"use strict";
const fs=require("fs"),path=require("path");
const ROOT=path.resolve(__dirname,"..");
const games=["phosphor-run","rosey-chop","hardware-test"];
function assets(root=ROOT) {
  const files=["assets/launcher/splash.ppm"];
  function walk(relative) {
    for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true})) {
      const name=relative+"/"+entry.name;
      if(entry.isDirectory())walk(name);
      else if(entry.isFile() && /\.(conf|txt|sprite|ppm|wav|robot)$/.test(name))files.push(name);
    }
  }
  games.forEach(id=>walk("games/"+id));
  return files.sort();
}
function configs(root=ROOT,files=assets(root)) {
  return Object.fromEntries(files.filter(file=>file.endsWith(".conf"))
    .map(file=>[file,fs.readFileSync(path.join(root,file),"utf8")]));
}
module.exports={assets,configs,games,ROOT};
if(require.main===module) {
  const destination=path.join(ROOT,"build/web");fs.mkdirSync(destination,{recursive:true});
  for(const name of ["index.html","player.js","style.css"])fs.copyFileSync(path.join(ROOT,"web",name),path.join(destination,name));
  const files=assets();
  for(const name of files) {
    const target=path.join(destination,"runtime",name);fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.copyFileSync(path.join(ROOT,name),target);
  }
  fs.writeFileSync(path.join(destination,"assets.json"),JSON.stringify(files));
  fs.writeFileSync(path.join(destination,"configs.json"),JSON.stringify(configs(ROOT,files)));
  const {execFileSync}=require("node:child_process");
  const {hash}=require("./web-package.cjs");
  const git=(...args)=>execFileSync("git",args,{cwd:ROOT,encoding:"utf8"}).trim();
  const publicFiles=["index.html","player.js","style.css","assets.json","configs.json",
    ...["launcher",...games].flatMap(id=>[id+".js",id+".wasm"]),...files.map(file=>"runtime/"+file)];
  const build={version:1,sourceCommit:git("rev-parse","HEAD"),
    sourceDirty:!!git("status","--porcelain","--untracked-files=normal"),
    files:Object.fromEntries(publicFiles.sort().map(file=>[file,hash(fs.readFileSync(path.join(destination,file)))]))};
  fs.writeFileSync(path.join(destination,"build.json"),JSON.stringify(build,null,2)+"\n");
}
