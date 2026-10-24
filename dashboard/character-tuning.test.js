'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
const {listCharacters,readCharacter,saveCharacter,validate,workbench}=require('./character-tuning');
test('Character balance validates every independent move, preserves files on failure, and rejects stale saves',()=>{
 const game='circuit-clash',id='arthur',file='games/circuit-clash/assets/characters/arthur.conf';
 const {schema,characters}=listCharacters(game),current=readCharacter(game,id),before=fs.readFileSync(file);
 assert(characters.some(c=>c.id===id));assert.equal(schema.moves.length,6);
 assert.equal(listCharacters(game).workbench,true);
 const preview=workbench(game);assert.equal(preview.module,'/animation-workbench/circuit-clash/preview.js');
 assert.deepEqual(preview.manifest.cell_size,[96,96]);assert.match(preview.stage,/floor_y_px=42/);
 assert.throws(()=>workbench('../circuit-clash'));
 const values=Object.fromEntries(schema.character.filter(f=>f.type==='integer' && f.minimum!==f.maximum).map(f=>[f.key,+current.values[f.key]]));
 for(const patch of [{air_punch_damage:-1},{crouch_kick_active_ticks:0},{block_chord_ticks:7},{crouch_punch_bottom_px:99},
   {punch_followthrough_ticks:values.punch_recovery_ticks},{air_punch_followthrough_ticks:values.air_punch_recovery_ticks}])
  assert.throws(()=>saveCharacter(game,id,{hash:current.hash,values:{...values,...patch}}));
 assert.throws(()=>saveCharacter(game,id,{hash:current.hash,values:{...values,atlas:'assets/no.pam'}}));
 assert.throws(()=>saveCharacter(game,id,{hash:'stale',values}),e=>e.status===409);
 assert.throws(()=>readCharacter(game,'../arthur'));
 assert.deepEqual(fs.readFileSync(file),before);
 // Identical saves exercise serialization without changing the working configuration.
 const result=saveCharacter(game,id,{hash:current.hash,values});assert.equal(result.hash,current.hash);
 assert.deepEqual(fs.readFileSync(file),before);
 assert.throws(()=>validate(schema,{...current.values,air_kick_bottom_px:40,air_kick_top_px:30}));
});
