const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const test = require('node:test');
const { hash, readPackage, copyToWebsite } = require('../tools/web-package.cjs');
const { createServer } = require('../tools/serve-web.cjs');
const { configs } = require('../tools/web-assets.js');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chirky-package-test-'));
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(root)), fs.realpathSync(os.tmpdir()));
    fs.rmSync(root, { recursive:true, force:true });
  });
  const source = path.join(root, 'build'), site = path.join(root, 'website');
  function write(name, data) {
    const file = path.join(source, name);
    fs.mkdirSync(path.dirname(file), { recursive:true }); fs.writeFileSync(file, data);
  }
  const config = 'games/phosphor-run/game.conf', text = 'start_level=1\r\n';
  const files = { 'index.html':'<html>Chirky</html>', 'player.js':'// player\r\n', 'style.css':'body{}',
    'assets.json':JSON.stringify([config]), 'configs.json':JSON.stringify({ [config]:text }),
    ['runtime/'+config]:text };
  for (const id of ['launcher', 'phosphor-run', 'rosey-chop', 'hardware-test']) {
    files[id+'.js']='// module'; files[id+'.wasm']=Buffer.from([0,97,115,109]);
  }
  for (const [name, data] of Object.entries(files)) write(name, data);
  const build = { version:1, sourceCommit:'a'.repeat(40), sourceDirty:false,
    files:Object.fromEntries(Object.entries(files).map(([name, data]) => [name, hash(data)])) };
  const manifest = () => write('build.json', JSON.stringify(build)); manifest();
  fs.mkdirSync(path.join(site, 'apps/chirky'), { recursive:true });
  fs.writeFileSync(path.join(site, 'package.json'), '{"name":"slimbuck.com"}');
  fs.writeFileSync(path.join(site, 'apps/chirky/obsolete.txt'), 'old');
  fs.writeFileSync(path.join(site, 'apps/keep.txt'), 'unrelated');
  return { source, site, write, build, manifest, config, text };
}

test('package configuration and exported bytes match; obsolete files are removed only in Chirky', t => {
  const f = fixture(t), bundle = readPackage(f.source);
  assert.equal(configs(path.join(f.source, 'runtime'), [f.config])[f.config], f.text);
  copyToWebsite(bundle, f.site);
  const target = path.join(f.site, 'apps/chirky');
  assert.equal(readPackage(target).identity, bundle.identity);
  for (const name of bundle.names)
    assert.deepEqual(fs.readFileSync(path.join(target, name)), fs.readFileSync(path.join(f.source, name)));
  assert(!fs.existsSync(path.join(target, 'obsolete.txt')));
  assert.equal(fs.readFileSync(path.join(f.site, 'apps/keep.txt'), 'utf8'), 'unrelated');
});

test('changed package is rejected before touching the website', t => {
  const f = fixture(t), bundle = readPackage(f.source);
  f.write('player.js', 'changed');
  assert.throws(() => copyToWebsite(bundle, f.site), /Stale or changed build/);
  assert(fs.existsSync(path.join(f.site, 'apps/chirky/obsolete.txt')));
});

test('unsafe paths, wrong destination and changed configuration fail validation', t => {
  const f = fixture(t), bundle = readPackage(f.source);
  fs.writeFileSync(path.join(f.site, 'package.json'), '{"name":"other"}');
  assert.throws(() => copyToWebsite(bundle, f.site), /Destination/);
  f.build.files['../outside']=hash('test'); f.manifest();
  assert.throws(() => readPackage(f.source), /Unsafe package path/);
  delete f.build.files['../outside'];
  f.write('configs.json', '{}'); f.build.files['configs.json']=hash('{}'); f.manifest();
  assert.throws(() => readPackage(f.source), /Configuration mismatch/);
});

test('standalone preview serves exact build with WASM MIME, but not .conf or arbitrary files', async t => {
  const f = fixture(t), server = createServer(f.source);
  t.after(() => new Promise(resolve => server.close(resolve)));
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  const base = `http://127.0.0.1:${server.address().port}/`;
  const response = await fetch(base+'phosphor-run.wasm');
  assert.equal(response.headers.get('content-type'), 'application/wasm');
  assert.deepEqual(Buffer.from(await response.arrayBuffer()), fs.readFileSync(path.join(f.source, 'phosphor-run.wasm')));
  assert.equal((await (await fetch(base+'configs.json')).json())[f.config], f.text);
  for (const name of ['runtime/'+f.config, '../package.json', 'unknown.js'])
    assert.equal((await fetch(base+name)).status, 404);
  assert.equal((await fetch(base, { method:'POST' })).status, 405);
  assert.equal(await (await fetch(base, { method:'HEAD' })).text(), '');
});
