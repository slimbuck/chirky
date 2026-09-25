const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

function player() {
  const elements = new Map(), created = [];
  const document = {
    querySelector(selector) {
      if (!elements.has(selector)) elements.set(selector, {
        addEventListener() {}, focus() {}, setAttribute() {},
      });
      return elements.get(selector);
    },
    querySelectorAll: () => [], addEventListener() {},
  };
  const context = vm.createContext({
    document, window: { addEventListener() {} },
    location: { search: '?game=phosphor-run' }, URLSearchParams, console,
    // Hold startup before WASM loading; exercise the real playback functions.
    fetch: () => new Promise(() => {}),
    fakeAudio: {
      destination: {},
      createBufferSource() {
        const source = {
          starts: 0, stops: 0, connect() {},
          start() { this.starts++; }, stop() { this.stops++; },
        };
        created.push(source);
        return source;
      },
    },
  });
  const run = code => vm.runInContext(code, context);
  run(fs.readFileSync(path.join(__dirname, '../web/player.js'), 'utf8'));
  run('audio=fakeAudio; for(let i=1;i<=10;i++)assetSounds.set(i,{});');
  return { run, created, elements };
}

test('rapid repeats neither stack nor interrupt, while distinct sounds mix', () => {
  const { run, created } = player();
  run('for(let i=0;i<32;i++)playAssetSound(1);');
  assert.equal(created.length, 1);
  assert.equal(created[0].starts, 1);
  assert.equal(created[0].stops, 0);
  run('playAssetSound(2); playAssetSound(1);');
  assert.equal(created.length, 2);
  assert.equal(run('sources.size'), 2);
  created[0].onended();
  run('playAssetSound(1);');
  assert.equal(created.length, 3);
  assert.equal(run('sources.size'), 2);
});

test('voice bound, pause and mute release sources so sounds can play again', () => {
  const { run, created, elements } = player();
  run('for(let i=1;i<=9;i++)playAssetSound(i);');
  assert.equal(run('sources.size'), 8);
  assert.equal(created[0].stops, 1);
  run('setPaused(true); playAssetSound(1);');
  assert.equal(run('sources.size'), 0);
  assert.equal(created.length, 9);
  assert.ok(created.every(source => source.stops === 1));
  run('setPaused(false); playAssetSound(1);');
  assert.equal(created.length, 10);
  elements.get('#mute').onclick();
  run('playAssetSound(1);');
  assert.equal(run('sources.size'), 0);
  assert.equal(created.length, 10);
  elements.get('#mute').onclick();
  run('playAssetSound(1);');
  assert.equal(created.length, 11);
  // A delayed ended event from a stopped source must not remove its replacement.
  created[9].onended();
  assert.equal(run('sources.size'), 1);
});
