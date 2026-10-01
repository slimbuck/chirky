// Standalone preview of the exact publishable build, without dashboard or Pi APIs.
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { readPackage } = require('./web-package.cjs');
const {releasePrefix,releaseEntry,contentType,IMMUTABLE,ENTRY_CACHE}=require('./web-release.cjs');

function createServer(directory, {versioned=false}={}) {
  const bundle = readPackage(directory);
  const files = new Set(bundle.names);
  const prefix=releasePrefix(bundle);
  return http.createServer((request, response) => {
    try {
      let name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1) || 'index.html';
      if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405).end(); return; }
      const entry=versioned && name==='index.html',metadata=versioned && name==='build.json';
      if(versioned && !entry && !metadata){
        if(!name.startsWith(prefix)){response.writeHead(404).end('Not found');return;}
        name=name.slice(prefix.length);
      }
      // Match the website: game configuration travels in configs.json, not .conf URLs.
      if (!files.has(name) || name.endsWith('.conf')) { response.writeHead(404).end('Not found'); return; }
      const file = path.join(bundle.root, name);
      response.writeHead(200, { 'Content-Type':contentType(name),
        'Cache-Control':versioned?(entry?ENTRY_CACHE:metadata?'no-store':IMMUTABLE):'no-store' });
      response.end(request.method === 'HEAD' ? undefined : entry?releaseEntry(bundle):fs.readFileSync(file));
    } catch { response.writeHead(400).end('Invalid request'); }
  });
}

if (require.main === module) {
  const server = createServer(path.join(__dirname, '../build/web'));
  server.on('error', error => { console.error(error.message); process.exitCode=1; });
  const host = process.argv[3] || '127.0.0.1';
  server.listen(Number(process.argv[2] || 3031), host, () =>
    console.log(`Chirky standalone: http://${host}:${server.address().port}/`));
}
module.exports = { createServer };
