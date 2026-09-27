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
    ["bramble-hollow", "phosphor-run", "rosey-chop", "hardware-test"]);
  assert.equal(catalog.games.at(-1).role, "diagnostic");
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
