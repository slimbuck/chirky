"use strict";
const fs=require("fs"),path=require("path");
const ROOT=path.resolve(__dirname,"..");
const {gameCatalog}=require("./game-catalog");
const catalog=gameCatalog(ROOT);
const games=catalog.games.map(game=>game.id);
function assets(root=ROOT) {
  const files=["assets/launcher/mascot.ppm"];
  function walk(relative) {
    for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true})) {
      const name=relative+"/"+entry.name;
      if(entry.isDirectory())walk(name);
      else if(entry.isFile() && /\.(conf|txt|sprite|ppm|pam|wav|robot)$/.test(name))files.push(name);
    }
  }
  games.forEach(id=>walk("games/"+id));
  return files.sort();
}
function configs(root=ROOT,files=assets(root)) {
  return Object.fromEntries(files.filter(file=>file.endsWith(".conf"))
    .map(file=>[file,fs.readFileSync(path.join(root,file),"utf8")]));
}
module.exports={assets,catalog,configs,games,ROOT};
if(require.main===module) {
  const destination=path.join(ROOT,"build/web");fs.mkdirSync(destination,{recursive:true});
  for(const name of ["index.html","player.js","style.css","favicon.svg","favicon.ico"])fs.copyFileSync(path.join(ROOT,"web",name),path.join(destination,name));
  // Game JavaScript is only a generated descriptor. The persistent host owns
  // the one renderer, filesystem, asset store and runtime for every game.
  for(const id of games)fs.writeFileSync(path.join(destination,id+".js"),
    `export default ${JSON.stringify({module:id+".wasm",config:`games/${id}/game.conf`})};\n`);
  const files=assets();
  for(const name of files) {
    const target=path.join(destination,"runtime",name);fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.copyFileSync(path.join(ROOT,name),target);
  }
  fs.writeFileSync(path.join(destination,"assets.json"),JSON.stringify(files));
  fs.writeFileSync(path.join(destination,"configs.json"),JSON.stringify(configs(ROOT,files)));
  fs.writeFileSync(path.join(destination,"catalog.json"),JSON.stringify(catalog,null,2)+"\n");
  const {execFileSync}=require("node:child_process");
  const {hash}=require("./web-package.cjs");
  const git=(...args)=>execFileSync("git",args,{cwd:ROOT,encoding:"utf8"}).trim();
  const publicFiles=["index.html","player.js","style.css","favicon.svg","favicon.ico","assets.json","configs.json","catalog.json",
    ...["launcher",...games].flatMap(id=>[id+".js",id+".wasm"]),...files.map(file=>"runtime/"+file)];
  const build={version:1,sourceCommit:git("rev-parse","HEAD"),
    sourceDirty:!!git("status","--porcelain","--untracked-files=normal"),
    files:Object.fromEntries(publicFiles.sort().map(file=>[file,hash(fs.readFileSync(path.join(destination,file)))]))};
  fs.writeFileSync(path.join(destination,"build.json"),JSON.stringify(build,null,2)+"\n");
}
