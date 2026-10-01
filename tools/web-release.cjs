// A page pins all relative imports, fetches and preloads to one complete release.
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const IMMUTABLE='public,max-age=31536000,immutable';
// Browsers check the entry on each visit; CloudFront serves it without an
// origin round trip. Publishing invalidates it after the release is verified.
const ENTRY_CACHE='public,max-age=0,s-maxage=3600,must-revalidate';
function releasePrefix(bundle){return `releases/${bundle.identity}/`;}
function releaseEntry(bundle){
  const html=fs.readFileSync(path.join(bundle.root,'index.html'),'utf8');
  assert(html.includes('<head>') && !/<base\b/i.test(html),'Entry page must have one unbased head');
  return Buffer.from(html.replace('<head>',`<head>\n<base href="/${releasePrefix(bundle)}">`));
}
const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8',
  '.css':'text/css','.json':'application/json','.wasm':'application/wasm',
  '.wav':'audio/wav','.svg':'image/svg+xml','.ico':'image/x-icon'};
const contentType=name=>types[path.extname(name)] || 'application/octet-stream';
module.exports={releasePrefix,releaseEntry,contentType,IMMUTABLE,ENTRY_CACHE};
