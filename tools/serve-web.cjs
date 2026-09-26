// Standalone preview of the exact publishable build, without dashboard or Pi APIs.
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { readPackage } = require('./web-package.cjs');

function createServer(directory) {
  const bundle = readPackage(directory);
  const files = new Set(bundle.names);
  const types = { '.html':'text/html; charset=utf-8', '.js':'text/javascript; charset=utf-8',
    '.css':'text/css', '.json':'application/json', '.wasm':'application/wasm', '.wav':'audio/wav' };
  return http.createServer((request, response) => {
    try {
      const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname).slice(1) || 'index.html';
      if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405).end(); return; }
      // Match the website: game configuration travels in configs.json, not .conf URLs.
      if (!files.has(name) || name.endsWith('.conf')) { response.writeHead(404).end('Not found'); return; }
      const file = path.join(bundle.root, name);
      response.writeHead(200, { 'Content-Type':types[path.extname(name)] || 'application/octet-stream',
        'Cache-Control':'no-store' });
      response.end(request.method === 'HEAD' ? undefined : fs.readFileSync(file));
    } catch { response.writeHead(400).end('Invalid request'); }
  });
}

if (require.main === module) {
  const server = createServer(path.join(__dirname, '../build/web'));
  server.on('error', error => { console.error(error.message); process.exitCode=1; });
  server.listen(Number(process.argv[2] || 3031), '127.0.0.1', () =>
    console.log(`Chirky standalone: http://127.0.0.1:${server.address().port}/`));
}
module.exports = { createServer };
