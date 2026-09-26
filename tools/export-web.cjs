// Test the standalone build, then copy it unchanged into the website repository.
// Does not commit, push or publish either repository.
const path = require('node:path');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const { readPackage, copyToWebsite } = require('./web-package.cjs');
const { createServer } = require('./serve-web.cjs');

async function main() {
  if (process.argv.length !== 3) throw new Error('Usage: node tools/export-web.cjs <slimbuck.com-repository>');
  const bundle = readPackage(path.join(__dirname, '../build/web'));
  const server = createServer(bundle.root);
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  try {
    const child = spawn(process.execPath, [path.join(__dirname, 'platform-browser.cjs'),
      `http://127.0.0.1:${server.address().port}/`, path.join(__dirname, '../build/web-export-checks')],
    { stdio:'inherit', windowsHide:true });
    const [code] = await once(child, 'exit');
    if (code !== 0) throw new Error('Browser checks failed; website unchanged');
    copyToWebsite(bundle, process.argv[2]);
    console.log(`Website updated from ${bundle.build.sourceCommit}${bundle.build.sourceDirty?' (uncommitted changes)':''}. Not published.`);
  } finally { await new Promise(resolve => server.close(resolve)); }
}
main().catch(error => { console.error(error); process.exitCode=1; });
