"use strict";
const fs=require('node:fs'),path=require('node:path');
const {gameDirectory,editorAssetPath,textHash,writeAtomic}=require('./editors');
const fail=(message,status=400)=>{throw Object.assign(new Error(message),{status});};
function schemaFor(game) {
  const file=path.join(gameDirectory(game),'content-schema.json');
  if(!fs.existsSync(file))fail('This game has no character tuning schema.',404);
  const schema=JSON.parse(fs.readFileSync(file,'utf8'));
  if(!Array.isArray(schema.character))fail('Character tuning is unavailable.',404);
  return schema;
}
function characterFile(game,id) {
  if(typeof id!=='string' || !/^[a-z0-9][a-z0-9-]{0,62}$/.test(id))fail('Invalid character id.');
  const file=editorAssetPath(gameDirectory(game),`assets/characters/${id}.conf`);
  if(!fs.existsSync(file))fail('Character not found.',404);
  return file;
}
function parse(text) {
  const values=Object.create(null);
  for(const line of text.split(/\r?\n/)) {
    const clean=line.trim();if(!clean || clean.startsWith('#'))continue;
    const split=clean.indexOf('=');if(split<1)fail('Malformed character configuration.');
    const key=clean.slice(0,split).trim();if(Object.hasOwn(values,key))fail(`Duplicate ${key}.`);
    values[key]=clean.slice(split+1).trim();
  }
  return values;
}
function validate(schema,values) {
  const fields=schema.character,keys=new Set(fields.map(f=>f.key));
  for(const key of Object.keys(values))if(!keys.has(key))fail(`Unknown field: ${key}.`);
  for(const f of fields) {
    const v=values[f.key];if(v===undefined)fail(`Missing ${f.key}.`);
    if(f.type==='integer' && (!/^-?\d+$/.test(String(v)) || !Number.isSafeInteger(+v) || +v<f.minimum || +v>f.maximum))
      fail(`${f.key} must be an integer from ${f.minimum} to ${f.maximum}.`);
    if(f.type==='string' && (typeof v!=='string' || !/^[\x20-\x7e]+$/.test(v) || v.length<f.minLength || v.length>f.maxLength))fail(`Invalid ${f.key}.`);
    if(f.type==='asset-path' && (typeof v!=='string' || !/^assets\/[a-zA-Z0-9_./-]+$/.test(v) || v.includes('..') || v.includes('//')))fail(`Invalid ${f.key}.`);
  }
  for(const rule of schema.constraints || []) {
    const m=/^(\w+) (<=|<) (\w+)$/.exec(rule);
    if(!m || !(m[2]==='<' ? +values[m[1]]<+values[m[3]] : +values[m[1]]<=+values[m[3]]))fail(`Required: ${rule}.`);
  }
  return values;
}
function listCharacters(game) {
  const schema=schemaFor(game),dir=path.join(gameDirectory(game),'assets/characters');
  return {schema,characters:fs.readdirSync(dir).filter(n=>/^[a-z0-9][a-z0-9-]*\.conf$/.test(n)).map(n=>{
    const id=n.slice(0,-5),v=parse(fs.readFileSync(characterFile(game,id),'utf8'));return {id,name:v.name};
  })};
}
function readCharacter(game,id) {
  const schema=schemaFor(game),text=fs.readFileSync(characterFile(game,id),'utf8');
  const values=validate(schema,parse(text));
  return {id,values,hash:textHash(text)};
}
function saveCharacter(game,id,body) {
  const schema=schemaFor(game),file=characterFile(game,id),text=fs.readFileSync(file,'utf8');
  if(body.hash!==textHash(text))fail('The character changed on disk. Reload it before saving.',409);
  if(!body.values || typeof body.values!=='object' || Array.isArray(body.values))fail('Expected numeric tuning values.');
  const current=parse(text),editable=schema.character.filter(f=>f.type==='integer' && f.minimum!==f.maximum);
  const allowed=new Set(editable.map(f=>f.key));
  for(const key of Object.keys(body.values))if(!allowed.has(key))fail(`Field is not editable: ${key}.`);
  for(const f of editable)if(!Object.hasOwn(body.values,f.key))fail(`Missing ${f.key}.`);
  validate(schema,{...current,...body.values});
  const updated=text.replace(/^(\s*)(\w+)(\s*=).*$/gm,(line,space,key,eq)=>allowed.has(key)?`${space}${key}${eq}${body.values[key]}`:line);
  writeAtomic(file,updated);return {ok:true,hash:textHash(updated)};
}
module.exports={listCharacters,readCharacter,saveCharacter,parse,validate};
