const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function packageFile(root, name) {
  assert(typeof name === 'string' && !name.includes('\\') && !name.includes(':') &&
    name.split('/').every(part => part && part !== '.' && part !== '..'), 'Unsafe package path');
  let file = root;
  for (const part of name.split('/')) {
    file = path.join(file, part);
    assert(!fs.lstatSync(file).isSymbolicLink(), 'Package links are not supported');
  }
  assert(fs.statSync(file).isFile(), `Not a file: ${name}`);
  return file;
}

function readPackage(directory) {
  const root = fs.realpathSync(directory);
  const bytes = fs.readFileSync(packageFile(root, 'build.json'));
  const build = JSON.parse(bytes);
  assert.equal(build.version, 1);
  assert.match(build.sourceCommit, /^[a-f0-9]{40}$/);
  assert.equal(typeof build.sourceDirty, 'boolean');
  assert(build.files && typeof build.files === 'object' && !Array.isArray(build.files));
  const names = Object.keys(build.files);
  assert(!names.includes('build.json'));
  for (const file of ['index.html', 'player.js', 'style.css', 'assets.json', 'configs.json',
    ...['launcher', 'phosphor-run', 'rosey-chop', 'hardware-test'].flatMap(id => [id+'.js', id+'.wasm'])])
    assert(names.includes(file), `Missing package file: ${file}`);
  for (const [file, digest] of Object.entries(build.files)) {
    assert.match(digest, /^[a-f0-9]{64}$/);
    assert.equal(hash(fs.readFileSync(packageFile(root, file))), digest, `Stale or changed build: ${file}. Run make web.`);
  }
  const assets = JSON.parse(fs.readFileSync(path.join(root, 'assets.json')));
  const configs = JSON.parse(fs.readFileSync(path.join(root, 'configs.json')));
  for (const file of assets) {
    assert(names.includes('runtime/'+file), `Missing runtime asset: ${file}`);
    if (file.endsWith('.conf'))
      assert.equal(configs[file], fs.readFileSync(packageFile(root, 'runtime/'+file), 'utf8'), `Configuration mismatch: ${file}`);
  }
  return { root, build, identity: hash(bytes), names: [...names, 'build.json'] };
}

function copyToWebsite(bundle, website) {
  const root = fs.realpathSync(website);
  assert.equal(JSON.parse(fs.readFileSync(path.join(root, 'package.json'))).name, 'slimbuck.com',
    'Destination must be the slimbuck.com repository');
  const apps = path.join(root, 'apps');
  assert.equal(fs.realpathSync(apps), apps, 'Website apps directory must not be a link');
  const target = path.join(apps, 'chirky');
  const exists = fs.existsSync(target);
  if (exists) assert.equal(fs.realpathSync(target), target, 'Website Chirky directory must not be a link');
  assert(!bundle.root.startsWith(apps+path.sep), 'Build must be outside website apps');
  assert.equal(readPackage(bundle.root).identity, bundle.identity, 'Build changed after testing');
  const stage = fs.mkdtempSync(path.join(apps, '.chirky-update-'));
  const staged = path.join(stage, 'new'), backup = path.join(stage, 'old');
  let backedUp = false;
  try {
    for (const name of bundle.names) {
      const destination = path.join(staged, name);
      fs.mkdirSync(path.dirname(destination), { recursive: true });
      fs.copyFileSync(packageFile(bundle.root, name), destination);
    }
    assert.equal(readPackage(staged).identity, bundle.identity);
    if (exists) { fs.renameSync(target, backup); backedUp = true; }
    try { fs.renameSync(staged, target); backedUp = false; }
    catch (error) {
      if (exists) { fs.renameSync(backup, target); backedUp = false; }
      throw error;
    }
  } finally {
    // Preserve the backup if rollback itself failed.
    assert.equal(path.dirname(fs.realpathSync(stage)), apps);
    if (!backedUp) fs.rmSync(stage, { recursive: true, force: true });
    else console.error(`Original website preserved at ${backup}`);
  }
}

module.exports = { hash, readPackage, copyToWebsite };
