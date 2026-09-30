"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const { gameCatalog, ROOT } = require("../tools/game-catalog");

function source(relative) {
  return fs.readFileSync(path.join(ROOT, relative), "utf8");
}

test("portable ABI and games do not depend on platform headers", () => {
  const files = ["include/chirky.h",...fs.readdirSync(path.join(ROOT,"src")).filter(name=>/\.[ch]$/.test(name)).map(name=>"src/"+name)];
  for (const entry of fs.readdirSync(path.join(ROOT, "games"), { withFileTypes:true })) {
    if (!entry.isDirectory()) continue;
    for (const file of fs.readdirSync(path.join(ROOT, "games", entry.name)))
      if (/\.[ch]$/.test(file)) files.push(`games/${entry.name}/${file}`);
  }
  const forbidden = /__EMSCRIPTEN__|<emscripten(?:\/|\.h)|<linux\/|<pthread\.h>|<dlfcn\.h>|<sys\/socket\.h>/;
  for (const file of files)
    assert.doesNotMatch(source(file), forbidden, file);
});

test("both platform hosts use the shared lifecycle and viewport", () => {
  for (const file of ["src/platform/linux/host.c", "src/platform/web/host.c"]) {
    const text = source(file);
    assert.match(text, /#include "runtime\.h"/, file);
    assert.match(text, /#include "viewport\.h"/, file);
    for (const call of ["chirky_runtime_start", "chirky_runtime_stop",
      "chirky_runtime_update", "chirky_runtime_render"])
      assert.match(text, new RegExp(`\\b${call}\\b`), `${file}: ${call}`);
  }
});

test("console decisions have one portable owner and both adapters consume it",()=>{
  for(const file of ['src/platform/linux/host.c','src/platform/web/console_bridge.h']) {
    const text=source(file);
    for(const name of ['chirky_console_update','chirky_console_render','chirky_console_launch','chirky_console_home','chirky_console_capture'])
      assert(text.includes(name),`${file} must consume ${name}`);
    assert.doesNotMatch(text,/\b(console_menu_update|setup_begin|setup_release)\s*\(/,'platform adapters must not duplicate console transitions');
  }
  assert.doesNotMatch(source('web/index.html'),/<nav|<dialog/,'console menus belong inside the framebuffer');
  assert.match(source('Makefile'),/-sMAIN_MODULE=1/);
  assert.match(source('Makefile'),/-sSIDE_MODULE=2/);
  assert.match(source('web/player.js'),/preinitializedWebGLContext:displayContext/);
  assert.doesNotMatch(source('web/player.js'),/createModule\(nextId\)|runtime!==shell/,'games use the persistent host');
});

test("production browser plumbing contains no hand-maintained game ids", () => {
  const plumbing = ["web/player.js", "tools/web-assets.js", "tools/web-package.cjs",
    "dashboard/server.js"];
  for (const file of plumbing) {
    const text = source(file);
    for (const game of gameCatalog().games)
      assert(!text.includes(`"${game.id}"`) && !text.includes(`'${game.id}'`),
        `${file} hardcodes ${game.id}`);
  }
});
