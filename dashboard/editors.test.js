"use strict";
const test=require("node:test");
const assert=require("node:assert/strict");
const fs=require("fs");
const os=require("os");
const path=require("path");
const editors=require("./editors");

test("all campaign levels and animations use valid shared definitions",()=> {
  const all=editors.loadEditors("phosphor-run");
  assert.equal(all.filter(e=>e.type==="tilemap").length,4);
  assert.equal(all.filter(e=>e.type==="sprite").length,20);
  for (const editor of all) {
    const text=fs.readFileSync(editors.editorAssetPath(editors.gameDirectory("phosphor-run"),editor.file),"utf8");
    assert.equal(editors.validateEditorText(editor,text).valid,true,editor.id);
  }
});
test("animation validation rejects malformed frames and timing",()=> {
  const sprite=editors.findEditor("phosphor-run","sprite-shard");
  for (const text of ["cc\n---\nc\n", "cc\n---\n", "# ticks=0\ncc\n", "# ticks=1.5\ncc\n", "c?\n", "c\n"+"---\nc\n".repeat(64)])
    assert.equal(editors.validateEditorText(sprite,text).valid,false,text);
  const result=editors.validateEditorText(sprite,"# ticks=12\ncc\n---\ngg\n");
  assert.equal(result.valid,true); assert.equal(result.frames,2); assert.equal(result.ticks,12);
});
test("level validation enforces spawn and exit counts",()=> {
  const level=editors.findEditor("phosphor-run","level-relay-shaft");
  for (const text of ["SSE\n###\n", "S..\n###\n", "..E\n###\n"])
    assert.equal(editors.validateEditorText(level,text).valid,false);
  assert.equal(editors.validateEditorText(level,"S.E\n###\n").valid,true);
});
test("catalog rejects traversal and duplicate identifiers",()=> {
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),"phosphor-catalog-"));
  const file=path.join(directory,"content.conf");
  try {
    for (const text of ["level.one=../secret", "level.one=assets/a\nlevel.one=assets/b", "sprite.Bad=assets/x"] ) {
      fs.writeFileSync(file,text); assert.throws(()=>editors.readCatalog(directory,"content.conf"));
    }
  } finally {fs.unlinkSync(file);fs.rmdirSync(directory);}
});
test("duplicate assets register immediately and campaign ordering persists",()=> {
  const id=`editor-test-${process.pid}`;
  const directory=path.resolve(__dirname,"..","games",id);
  fs.mkdirSync(directory);
  const source=editors.gameDirectory("phosphor-run");
  try {
    fs.copyFileSync(path.join(source,"editor.json"),path.join(directory,"editor.json"));
    fs.writeFileSync(path.join(directory,"game.conf"),`id=${id}\n`);
    fs.writeFileSync(path.join(directory,"content.conf"),"level.one=one.txt\nsprite.idle=idle.sprite\n");
    fs.writeFileSync(path.join(directory,"one.txt"),"S.E\n###\n");
    fs.writeFileSync(path.join(directory,"idle.sprite"),"# ticks=9\nc\n---\ng\n");
    assert.equal(editors.createEditor(id,{source:"level-one",id:"two"}),"level-two");
    assert.equal(editors.createEditor(id,{source:"sprite-idle",id:"walk"}),"sprite-walk");
    assert.equal(editors.loadEditors(id).length,4);
    assert.throws(()=>editors.createEditor(id,{source:"level-one",id:"two"}));
    assert.throws(()=>editors.reorderLevels(id,["one","one"]));
    editors.reorderLevels(id,["two","one"]);
    assert.deepEqual(editors.loadEditors(id).filter(e=>e.type==="tilemap").map(e=>e.catalogId),["two","one"]);
    assert.equal(fs.readFileSync(path.join(directory,"assets/sprites/walk.sprite"),"utf8"),"# ticks=9\nc\n---\ng\n");
    editors.createEditor(id,{source:"sprite-idle",id:"a".repeat(63)});
    assert.equal(editors.loadEditors(id).length,5);
    const blankId=editors.createEditor(id,{source:"level-one",id:"new-level",name:"Furnace Shaft",blank:true});
    const blank=editors.findEditor(id,blankId);
    const blankFile=editors.editorAssetPath(directory,blank.file);
    const blankText=fs.readFileSync(blankFile,"utf8");
    assert.equal(blank.name,"Furnace Shaft");
    assert.equal(editors.validateEditorText(blank,blankText).valid,true);
    assert.equal(blankText.includes("^"),false);
    assert.throws(()=>editors.renameLevel(id,blankId,"Renamed","stale"));
    assert.throws(()=>editors.renameLevel(id,blankId,"bad\nname",editors.textHash(blankText)));
    editors.renameLevel(id,blankId,"Fire & Ice",editors.textHash(blankText));
    assert.equal(editors.findEditor(id,blankId).name,"Fire & Ice");
    assert.equal(fs.readFileSync(blankFile,"utf8").split("\n").slice(1).join("\n"),blankText.split("\n").slice(1).join("\n"));
    const copyId=editors.createEditor(id,{source:blankId,id:"copy-level",name:"Second Furnace"});
    assert.equal(editors.findEditor(id,copyId).name,"Second Furnace");
    editors.reorderLevels(id,["copy-level","two","one","new-level"]);
    assert.deepEqual(editors.loadEditors(id).filter(e=>e.type==="tilemap").map(e=>e.name),["Second Furnace","two","one","Fire & Ice"]);
  } finally {
    // Only this uniquely named test directory, created above, is disposable.
    fs.rmSync(directory,{recursive:true,force:true});
  }
});
