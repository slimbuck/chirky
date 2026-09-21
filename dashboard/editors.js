"use strict";
const fs=require("fs");
const path=require("path");
const crypto=require("crypto");
const ROOT=path.resolve(__dirname,"..");
function parseConfig(text) {
  const values = {};
  for (const line of text.split(/\r?\n/)) {
    const clean = line.trim();
    if (!clean || clean.startsWith("#") || clean.startsWith(";")) continue;
    const index = clean.indexOf("=");
    if (index > 0) values[clean.slice(0, index).trim()] = clean.slice(index + 1).trim();
  }
  return values;
}

function validGameId(id) {
  return typeof id === "string" && /^[a-z0-9][a-z0-9-]{0,62}$/.test(id);
}

function gameDirectory(id) {
  if (!validGameId(id)) throw new Error("invalid game id");
  const directory = path.join(ROOT, "games", id);
  if (!fs.existsSync(path.join(directory, "game.conf"))) throw new Error("game not found");
  return directory;
}

function editorAssetPath(directory, relative) {
  if (typeof relative !== "string" || !/^[A-Za-z0-9._/-]+$/.test(relative) ||
      relative.split("/").some((part) => !part || part === "." || part === ".."))
    throw new Error("invalid editor asset path");
  const resolved = path.resolve(directory, ...relative.split("/"));
  if (!resolved.startsWith(`${path.resolve(directory)}${path.sep}`))
    throw new Error("editor asset escapes game directory");
  return resolved;
}

function loadEditors(id) {
  const directory = gameDirectory(id);
  const file = path.join(directory, "editor.json");
  if (!fs.existsSync(file)) return [];
  const gameValues = parseConfig(fs.readFileSync(path.join(directory, "game.conf"), "utf8"));
  const document = JSON.parse(fs.readFileSync(file, "utf8"));
  if (document.version !== 1 || !Array.isArray(document.editors))
    throw new Error(`${id}/editor.json has an unsupported format`);
  const seen = new Set();
  const catalog = document.catalog ? readCatalog(directory, document.catalog) : [];
  const definitions = [...document.editors, ...catalog.map((entry) => ({
    ...document.templates?.[entry.kind], id: `${entry.kind}-${entry.id}`,
    name: entry.kind === "level" ? (fs.readFileSync(editorAssetPath(directory, entry.file), "utf8").match(/^# ([^\r\n]+)/)?.[1] || entry.id.replace(/-/g, " ")) : entry.id.replace(/-/g, " "), file: entry.file, catalogKind: entry.kind,
    catalogId: entry.id,
  }))];
  return definitions.map((editor) => {
    const idPattern=editor?.catalogKind ? /^[a-z0-9][a-z0-9-]{0,69}$/ : /^[a-z0-9][a-z0-9-]{0,62}$/;
    if (!editor || !idPattern.test(editor.id || "") || seen.has(editor.id))
      throw new Error(`${id}/editor.json has an invalid editor id`);
    seen.add(editor.id);
    if (!new Set(["tilemap", "sprite"]).has(editor.type))
      throw new Error(`${id}/${editor.id} has an unsupported editor type`);
    editorAssetPath(directory, editor.file);
    if (editor.type === "tilemap" &&
        (!Number.isInteger(editor.tileSize) || editor.tileSize < 1 || editor.tileSize > 128))
      throw new Error(`${id}/${editor.id} has an invalid tile size`);
    if (!Array.isArray(editor.palette) || editor.palette.length < 2)
      throw new Error(`${id}/${editor.id} has an invalid palette`);
    const values = new Set();
    for (const tile of editor.palette) {
      if (!tile || typeof tile.value !== "string" || !/^[\x21-\x7e]$/.test(tile.value) || values.has(tile.value) ||
          typeof tile.name !== "string" || !/^#[0-9a-fA-F]{6}$/.test(tile.color || "") ||
          (tile.config !== undefined && !/^[a-z][a-z0-9_]{0,62}$/.test(tile.config)))
        throw new Error(`${id}/${editor.id} has an invalid palette entry`);
      if ((tile.minimum !== undefined && (!Number.isInteger(tile.minimum) || tile.minimum < 0)) ||
          (tile.maximum !== undefined && (!Number.isInteger(tile.maximum) || tile.maximum < 0)))
        throw new Error(`${id}/${editor.id} has an invalid tile count rule`);
      values.add(tile.value);
    }
    if (!values.has(editor.empty)) throw new Error(`${id}/${editor.id} has an invalid empty tile`);
    if (editor.type === "tilemap") {
      const viewport = editor.viewport || {};
      if (!Number.isInteger(viewport.width) || viewport.width < 1 ||
          !Number.isInteger(viewport.height) || viewport.height < 1)
        throw new Error(`${id}/${editor.id} has an invalid viewport`);
    }
    return { ...editor, palette: editor.palette.map((tile) => {
      const configured = tile.config ? gameValues[tile.config] : "";
      return { ...tile, color: /^[0-9a-fA-F]{6}$/.test(configured) ? `#${configured}` : tile.color };
    }) };
  });
}

function findEditor(gameId, editorId) {
  const editor = loadEditors(gameId).find((candidate) => candidate.id === editorId);
  if (!editor) throw Object.assign(new Error("editor not found"), { status: 404 });
  return editor;
}

function textHash(text) {
  return crypto.createHash("sha256").update(text).digest("hex");
}

function validateEditorText(editor, text) {
  const sprite = editor.type === "sprite";
  const lines = text.replace(/\r/g, "").split("\n");
  const frames = [[]];
  const errors = [];
  let ticks = 6;
  for (const line of lines) {
    if (!line) continue;
    if (line.startsWith("# ")) {
      if (line.startsWith("# ticks=")) {
        ticks = Number(line.slice(8));
        if (!sprite || !/^\d+$/.test(line.slice(8)) || !Number.isInteger(ticks) || ticks < 1 || ticks > 600)
          errors.push("Animation timing must be 1–600 ticks per frame.");
      }
      continue;
    }
    if (sprite && line === "---") frames.push([]);
    else frames.at(-1).push(line);
  }
  if (frames.length > 64) errors.push("An animation allows at most 64 frames.");
  const width = frames[0][0]?.length || 0, height = frames[0].length;
  const maximum = sprite ? 128 : 512;
  const allowed = new Set(editor.palette.map((tile) => tile.value));
  frames.forEach((rows, frameIndex) => {
    if (!rows.length || !width) errors.push(`Frame ${frameIndex + 1} is empty.`);
    if (width > maximum || rows.length > maximum) errors.push(`Dimensions cannot exceed ${maximum} × ${maximum}.`);
    if (rows.length !== height) errors.push("All animation frames must have the same dimensions.");
    const counts = Object.fromEntries(editor.palette.map((tile) => [tile.value, 0]));
    rows.forEach((row, y) => {
      if (row.length !== width) errors.push(`Frame ${frameIndex+1}, row ${y+1}: expected width ${width}.`);
      for (const value of row) {
        if (!allowed.has(value)) errors.push(`Unknown tile ${JSON.stringify(value)} in frame ${frameIndex+1}, row ${y+1}.`);
        else counts[value]++;
      }
    });
    for (const tile of editor.palette) {
      if (tile.minimum !== undefined && counts[tile.value] < tile.minimum)
        errors.push(`${tile.name} requires at least ${tile.minimum}; found ${counts[tile.value]}.`);
      if (tile.maximum !== undefined && counts[tile.value] > tile.maximum)
        errors.push(`${tile.name} allows at most ${tile.maximum}; found ${counts[tile.value]}.`);
    }
  });
  return {valid: !errors.length, errors, width, height, frames: frames.length, ticks};
}

function writeAtomic(file, text) {
  const temporary = `${file}.next-${process.pid}`;
  fs.writeFileSync(temporary, text, "utf8");
  fs.renameSync(temporary, file);
}


// The runtime and editor share this ordered catalog. No separate editor registrations.
function readCatalog(directory, relative) {
  const text = fs.readFileSync(editorAssetPath(directory, relative), "utf8");
  const entries = [], seen = new Set();
  const base = path.posix.dirname(relative);
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith("#")) continue;
    const match = /^(level|sprite)\.([a-z0-9][a-z0-9-]{0,62})=(.+)$/.exec(line);
    if (!match || seen.has(`${match[1]}.${match[2]}`)) throw new Error("Invalid or duplicate catalog entry");
    const [,kind,id,asset] = match;
    editorAssetPath(directory, asset);
    const file = base === "." ? asset : `${base}/${asset}`;
    editorAssetPath(directory, file);
    if (entries.filter(e => e.kind === kind).length >= 256) throw new Error("Catalog limit is 256 entries per kind");
    seen.add(`${kind}.${id}`); entries.push({kind, id, file, asset});
  }
  return entries;
}

function catalogDocument(id) {
  const directory = gameDirectory(id);
  const document = JSON.parse(fs.readFileSync(path.join(directory, "editor.json"), "utf8"));
  if (!document.catalog) throw Object.assign(new Error("This game has no content catalog"), {status:400});
  return {directory, document, entries:readCatalog(directory, document.catalog)};
}

function namedLevelText(text, name) {
  if (typeof name !== "string" || !name.trim() || name.trim().length > 80 || /[\x00-\x1f\x7f]/.test(name))
    throw Object.assign(new Error("Use a level name of 1–80 characters on one line"), {status:400});
  return "# " + name.trim() + "\n" + text.replace(/^# [^\r\n]*\r?\n/, "");
}
function renameLevel(id, editorId, name, hash) {
  const editor=findEditor(id,editorId);
  if (editor.catalogKind !== "level") throw Object.assign(new Error("Select a campaign level"),{status:400});
  const file=editorAssetPath(gameDirectory(id),editor.file), text=fs.readFileSync(file,"utf8");
  if (hash !== textHash(text)) throw Object.assign(new Error("Level changed; refresh and try again"),{status:409});
  writeAtomic(file,namedLevelText(text,name));
}
function createEditor(id, body) {
  const {directory, document, entries} = catalogDocument(id);
  if (!validGameId(body.id)) throw Object.assign(new Error("Use 1–63 lowercase letters, digits or hyphens for the name"), {status:400});
  const source = findEditor(id, body.source);
  const kind = source.catalogKind;
  if (!kind) throw Object.assign(new Error("Select a catalog asset to duplicate"), {status:400});
  if (entries.some(e => e.kind === kind && e.id === body.id)) throw Object.assign(new Error("That name already exists"), {status:409});
  if (entries.filter(e => e.kind === kind).length >= 256) throw Object.assign(new Error("Catalog limit reached"), {status:400});
  const asset = `assets/${kind === "level" ? "levels" : "sprites"}/${body.id}.${kind === "level" ? "txt" : "sprite"}`;
  const base = path.posix.dirname(document.catalog);
  const file = editorAssetPath(directory, base === "." ? asset : `${base}/${asset}`);
  let text = fs.readFileSync(editorAssetPath(directory, source.file), "utf8");
  if (body.blank) {
    if (kind !== "level") throw Object.assign(new Error("Only levels can start blank"), {status:400});
    text = Array(25).fill(".".repeat(80)).join("\n") + "\n..S" + ".".repeat(74) + "E..\n" + "#".repeat(80) + "\n";
  }
  if (kind === "level" && body.name !== undefined) text = namedLevelText(text, body.name);
  const validation=validateEditorText(source,text);
  if (!validation.valid) throw Object.assign(new Error(validation.errors.join(" ")),{status:400});
  fs.mkdirSync(path.dirname(file), {recursive:true});
  fs.writeFileSync(file, text, {encoding:"utf8", flag:"wx"});
  const catalogFile = editorAssetPath(directory, document.catalog);
  try { writeAtomic(catalogFile, `${fs.readFileSync(catalogFile,"utf8").trimEnd()}\n${kind}.${body.id}=${asset}\n`); }
  catch (error) { fs.unlinkSync(file); throw error; }
  return `${kind}-${body.id}`;
}

function reorderLevels(id, ids) {
  const {directory, document, entries} = catalogDocument(id);
  const levels = entries.filter(e => e.kind === "level");
  if (!Array.isArray(ids) || ids.length !== levels.length || new Set(ids).size !== ids.length ||
      ids.some(id => !levels.some(e => e.id === id))) throw Object.assign(new Error("Include every level exactly once"), {status:400});
  const file = editorAssetPath(directory, document.catalog);
  let index=0;
  const text = fs.readFileSync(file,"utf8").split(/\r?\n/).map(line => {
    if (!line.startsWith("level.")) return line;
    const nextId=ids[index++];
    const e = levels.find(e => e.id === nextId);
    return `level.${e.id}=${e.asset}`;
  }).join("\n");
  writeAtomic(file,text);
}

module.exports = {gameDirectory, editorAssetPath, loadEditors, findEditor, textHash,
  validateEditorText, writeAtomic, readCatalog, createEditor, reorderLevels, renameLevel};
