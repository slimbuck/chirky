// Publish the verified standalone build directly to Chirky's own S3 origin.
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const {once}=require('node:events');
const {readPackage,hash}=require('./web-package.cjs');
const {createServer}=require('./serve-web.cjs');
const {releasePrefix,releaseEntry,contentType,IMMUTABLE,ENTRY_CACHE}=require('./web-release.cjs');

async function verifyPublished(bundle,base,{versioned=true,entry=true,compression=false}={}){
  const names=bundle.names.filter(name=>!name.endsWith('.conf'));
  const filesBase=versioned?new URL(releasePrefix(bundle),base):base;
  async function check(name){
    const response=await fetch(new URL(name,filesBase),{headers:{'Accept-Encoding':'br, gzip'},signal:AbortSignal.timeout(30000)});
    assert.equal(response.status,200,`${name}: HTTP ${response.status}`);
    assert.equal(hash(Buffer.from(await response.arrayBuffer())),hash(fs.readFileSync(path.join(bundle.root,name))),`${name}: deployed bytes differ`);
    if(name.endsWith('.wasm'))assert.match(response.headers.get('content-type')||'',/^application\/wasm\b/);
    if(versioned)assert.equal(response.headers.get('cache-control'),IMMUTABLE,`${name}: release must be immutable`);
    if(compression && /\.(js|wasm)$/.test(name) && fs.statSync(path.join(bundle.root,name)).size>=1000)
      assert.match(response.headers.get('content-encoding')||'',/^(br|gzip)$/,`${name}: CloudFront compression is required`);
  }
  for(let i=0;i<names.length;i+=6)await Promise.all(names.slice(i,i+6).map(check));
  if(versioned && entry){
    const response=await fetch(base,{cache:'no-store',signal:AbortSignal.timeout(30000)});
    assert.equal(response.status,200,'Entry page unavailable');
    assert.equal(hash(Buffer.from(await response.arrayBuffer())),hash(releaseEntry(bundle)),'Entry page does not point to tested release');
  }
  console.log(`Verified ${names.length} published files against ${bundle.build.sourceCommit}.`);
}
function aws(...args){
  const result=spawnSync(process.env.AWS_CLI||'aws',[...args,'--no-cli-pager'],{encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});
  if(result.error)throw result.error;
  if(result.status!==0)throw new Error(result.stderr||`AWS command failed: ${args[0]} ${args[1]}`);
  return result.stdout.trim();
}
async function main(){
  const bundle=readPackage(path.resolve(__dirname,'../build/web'));
  if(process.argv[2]==='--verify')return verifyPublished(bundle,new URL(process.argv[3]||'https://chirky.org/'),{compression:true});
  const checkOnly=process.argv[2]==='--check';
  if(!checkOnly)assert.equal(bundle.build.sourceDirty,false,'Commit Chirky and rebuild before publishing');
  const bucket=process.env.AWS_S3_BUCKET,distribution=process.env.AWS_CLOUDFRONT_DISTRIBUTION_ID;
  if(!checkOnly){
    assert(bucket && /^[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]$/.test(bucket),'Set AWS_S3_BUCKET');
    assert(distribution && /^[A-Z0-9]+$/.test(distribution),'Set AWS_CLOUDFRONT_DISTRIBUTION_ID');
  }
  // The browser check is asynchronous so the temporary preview server stays responsive.
  const server=createServer(bundle.root,{versioned:true});server.listen(0,'127.0.0.1');await once(server,'listening');
  try{
    const {spawn}=require('node:child_process');
    const child=spawn(process.execPath,[path.join(__dirname,'platform-browser.cjs'),`http://127.0.0.1:${server.address().port}/`,path.join(__dirname,'../build/publish-checks')],{stdio:'inherit',windowsHide:true});
    const [code]=await once(child,'exit');assert.equal(code,0,'Browser checks failed; nothing uploaded');
    const scores=spawn(process.execPath,[path.join(__dirname,'phosphor-scores-browser.cjs'),`http://127.0.0.1:${server.address().port}/`,path.join(__dirname,'../build/publish-checks/scores')],{stdio:'inherit',windowsHide:true});
    const [scoreCode]=await once(scores,'exit');assert.equal(scoreCode,0,'Score persistence checks failed; nothing uploaded');
  }finally{await new Promise(resolve=>server.close(resolve));}
  assert.equal(readPackage(bundle.root).identity,bundle.identity,'Package changed after testing');
  if(checkOnly)return;
  const config=JSON.parse(aws('cloudfront','get-distribution-config','--id',distribution,'--output','json')).DistributionConfig;
  assert(config.Aliases.Items.includes('chirky.org'),'Distribution must serve chirky.org');
  assert(config.Origins.Items.some(origin=>origin.DomainName.startsWith(bucket+'.s3.') || origin.DomainName===bucket+'.s3.amazonaws.com'),'Distribution does not use the selected bucket');
  // Never overwrite files an open page may still need. Verify the complete
  // immutable release before making it reachable through the entry page.
  const prefix=releasePrefix(bundle);
  for(const name of bundle.names.filter(name=>!name.endsWith('.conf'))){
    aws('s3','cp',path.join(bundle.root,name),`s3://${bucket}/${prefix}${name}`,
      '--cache-control',IMMUTABLE,'--content-type',contentType(name),'--only-show-errors');
  }
  await verifyPublished(bundle,new URL('https://chirky.org/'),{entry:false,compression:true});
  assert.equal(readPackage(bundle.root).identity,bundle.identity,'Package changed during upload');
  const entryFile=path.join(__dirname,'../build/publish-checks/release-index.html');
  fs.writeFileSync(entryFile,releaseEntry(bundle));
  aws('s3','cp',entryFile,`s3://${bucket}/index.html`,'--content-type',contentType('index.html'),'--cache-control',ENTRY_CACHE,'--only-show-errors');
  aws('s3','cp',path.join(bundle.root,'build.json'),`s3://${bucket}/build.json`,'--content-type','application/json','--cache-control','no-store','--only-show-errors');
  const invalidation=aws('cloudfront','create-invalidation','--distribution-id',distribution,'--paths','/','/index.html','/build.json','--query','Invalidation.Id','--output','text');
  aws('cloudfront','wait','invalidation-completed','--distribution-id',distribution,'--id',invalidation);
  await verifyPublished(bundle,new URL('https://chirky.org/'),{compression:true});
}
module.exports={verifyPublished};
if(require.main===module)main().catch(error=>{console.error(error);process.exitCode=1;});
