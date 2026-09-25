// node tools/texture-browser.cjs <compiled-test-directory> [chrome-executable]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

async function main() {
  const root = path.resolve(process.argv[2] || 'build/texture-tests');
  const browser = process.argv[3] || process.env.CHROME || (process.platform === 'win32'
    ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe' : 'chromium');
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'texture-browser-'));
  const server = http.createServer((req, res) => {
    const name = req.url.slice(1);
    if (!/^texture-renderer\.(html|js|wasm)$/.test(name)) { res.writeHead(404).end(); return; }
    const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
    res.setHeader('Content-Type', types[path.extname(name)]);
    fs.createReadStream(path.join(root, name)).on('error', () => res.destroy()).pipe(res);
  });
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/texture-renderer.html`;
    const result = await new Promise((resolve, reject) => {
      const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
        '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--disable-dev-shm-usage',
        '--virtual-time-budget=15000', '--dump-dom', `--user-data-dir=${profile}`, url], { windowsHide: true });
      let stdout = '', stderr = '';
      child.stdout.on('data', data => { stdout += data; });
      child.stderr.on('data', data => { stderr += data; });
      const timeout = setTimeout(() => { child.kill(); reject(new Error('Texture browser test timed out')); }, 60000);
      child.on('error', error => { clearTimeout(timeout); reject(error); });
      child.on('exit', code => { clearTimeout(timeout); resolve({ code, stdout, stderr }); });
    });
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /<body[^>]*data-texture-tests="passed"/, result.stdout + result.stderr);
    const output = result.stdout.match(/<pre id="output">([\s\S]*?)<\/pre>/);
    console.log(output ? output[1].trim() : 'TEXTURE_TESTS_PASSED');
    console.log('WebGL1 pixel tests passed in headless Chrome/SwiftShader.');
  } finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    // Only remove the dedicated directory created by this process.
    assert.equal(path.dirname(profile), path.resolve(os.tmpdir()));
    fs.rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
