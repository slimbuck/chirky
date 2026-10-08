"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { gameCatalog } = require("../tools/game-catalog");

test("game manifests are the ordered browser catalog", () => {
  const catalog = gameCatalog();
  assert.equal(catalog.version, 1);
  assert.deepEqual(catalog.games.map(game => game.id),
    ["phosphor-run", "rosey-chop", "bramble-hollow", "circuit-clash"]);
  assert.deepEqual(catalog.games.slice(0,3).map(game=>game.order),[10,20,30]);
  assert(catalog.games.slice(0,3).every(game=>game.icon));
  assert(catalog.games.every(game=>game.role==="game"));
  assert.equal(catalog.games.find(game => game.id === "phosphor-run").levelSetting,
    "start_level");
});

test("catalog rejects a directory and manifest id mismatch", t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-catalog-"));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, "games", "wrong"), { recursive: true });
  fs.writeFileSync(path.join(root, "games", "wrong", "game.c"), "/* fixture */\n");
  fs.writeFileSync(path.join(root, "games", "wrong", "game.conf"),
    "id=other\nname=Other\ndescription=Test\nmodule=other.so\n");
  assert.throws(() => gameCatalog(root), /must match its directory/);
});

test("launcher metadata has safe defaults and rejects invalid order or icon paths", t => {
  const root=fs.mkdtempSync(path.join(os.tmpdir(),"chirky-order-"));
  t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
  const directory=path.join(root,"games","example");fs.mkdirSync(directory,{recursive:true});
  fs.writeFileSync(path.join(directory,"game.c"),"/* fixture */");
  const base="id=example\nname=Example\ndescription=Test\nmodule=example.so\n";
  const write=extra=>fs.writeFileSync(path.join(directory,"game.conf"),base+extra);
  write("");assert.equal(gameCatalog(root).games[0].order,1000);
  for(const order of ["-1","1.5","abc","1000001"]) {
    write(`launcher_order=${order}\n`);assert.throws(()=>gameCatalog(root),/Invalid launcher order/);
  }
  write("launcher_order=0\n");assert.equal(gameCatalog(root).games[0].order,0);
  for(const icon of ["../icon.png","assets/launcher/icons/missing.png"]) {
    write(`launcher_icon=${icon}\n`);assert.throws(()=>gameCatalog(root),/Invalid launcher icon/);
  }
});
