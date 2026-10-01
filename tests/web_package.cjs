const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const test = require('node:test');
const { hash, readPackage, copyToWebsite } = require('../tools/web-package.cjs');
const { createServer } = require('../tools/serve-web.cjs');
const { verifyPublished } = require('../tools/publish-web.cjs');
const {releasePrefix,releaseEntry,IMMUTABLE,ENTRY_CACHE}=require('../tools/web-release.cjs');
const { catalog, configs, games } = require('../tools/web-assets.js');

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
  const files = { 'index.html':'<html><head></head><body>Chirky</body></html>', 'player.js':'// player\r\n', 'style.css':'body{}',
    'favicon.svg':fs.readFileSync(path.join(__dirname,'../web/favicon.svg')),
    'favicon.ico':fs.readFileSync(path.join(__dirname,'../web/favicon.ico')),
    'assets.json':JSON.stringify([config]), 'configs.json':JSON.stringify({ [config]:text }),
    'catalog.json':JSON.stringify(catalog),
    ['runtime/'+config]:text };
  for (const id of ['launcher', ...games]) {
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
  await verifyPublished(readPackage(f.source),new URL(base),{versioned:false});
  const response = await fetch(base+'phosphor-run.wasm');
  assert.equal(response.headers.get('content-type'), 'application/wasm');
  assert.deepEqual(Buffer.from(await response.arrayBuffer()), fs.readFileSync(path.join(f.source, 'phosphor-run.wasm')));
  assert.equal((await (await fetch(base+'configs.json')).json())[f.config], f.text);
  for(const [name,type] of [['favicon.svg','image/svg+xml'],['favicon.ico','image/x-icon']]){
    const icon=await fetch(base+name);assert.equal(icon.status,200);assert.equal(icon.headers.get('content-type'),type);
  }
  for (const name of ['runtime/'+f.config, '../package.json', 'unknown.js'])
    assert.equal((await fetch(base+name)).status, 404);
  assert.equal((await fetch(base, { method:'POST' })).status, 405);
  assert.equal(await (await fetch(base, { method:'HEAD' })).text(), '');
});

test('versioned entry pins every relative URL; only entry metadata can change in place',async t=>{
  const f=fixture(t),bundle=readPackage(f.source),prefix=releasePrefix(bundle);
  const server=createServer(f.source,{versioned:true});
  t.after(()=>new Promise(resolve=>server.close(resolve)));
  server.listen(0,'127.0.0.1');await once(server,'listening');
  const base=new URL(`http://127.0.0.1:${server.address().port}/`);
  await verifyPublished(bundle,base);
  const response=await fetch(new URL('?game=phosphor-run&level=2',base)),html=await response.text();
  assert.equal(response.headers.get('cache-control'),ENTRY_CACHE);
  assert.equal(html,releaseEntry(bundle).toString());
  const assetBase=new URL(html.match(/<base href="([^"]+)"/)[1],base);
  assert.equal(assetBase.pathname,'/'+prefix);
  for(const name of ['player.js','launcher.wasm','catalog.json','configs.json']){
    const asset=await fetch(new URL(name,assetBase));
    assert.equal(asset.status,200);assert.equal(asset.headers.get('cache-control'),IMMUTABLE);
    assert.equal(hash(Buffer.from(await asset.arrayBuffer())),bundle.build.files[name]);
    assert.equal((await fetch(new URL(name,base))).status,404,'Must not silently fall back to mutable files');
  }
  assert.equal((await fetch(new URL('runtime/'+f.config,assetBase))).status,404);
  assert.equal((await fetch(new URL('build.json',base))).headers.get('cache-control'),'no-store');
  f.write('player.js','// next release');f.build.files['player.js']=hash('// next release');f.manifest();
  const next=readPackage(f.source);
  assert.notEqual(releasePrefix(next),prefix,'Every changed build must receive a different URL');
  await assert.rejects(verifyPublished(next,base),/HTTP 404/,'Old entry must not pass verification for another release');
});

test('CloudFront compresses releases and allows separate browser and edge entry lifetimes',()=>{
  assert.equal(ENTRY_CACHE,'public,max-age=0,s-maxage=3600,must-revalidate');
  const template=JSON.parse(fs.readFileSync(path.join(__dirname,'../deploy/web-hosting.json')));
  const policy=template.Resources.ReleaseCachePolicy.Properties.CachePolicyConfig;
  assert.equal(policy.MinTTL,0);assert.equal(policy.MaxTTL,31536000);
  assert.equal(policy.ParametersInCacheKeyAndForwardedToOrigin.EnableAcceptEncodingBrotli,true);
  assert.equal(policy.ParametersInCacheKeyAndForwardedToOrigin.EnableAcceptEncodingGzip,true);
  const behavior=template.Resources.Distribution.Properties.DistributionConfig.DefaultCacheBehavior;
  assert.equal(behavior.Compress,true);assert.equal(behavior.CachePolicyId.Ref,'ReleaseCachePolicy');
});
