"use strict";
let editorRequest=0;

function updateAnimationControls() {
  const state=levelState;
  if (state.editor.type!=="sprite") return;
  $("#animationFrame").innerHTML=state.frames.map((_,i)=>`<option value="${i}">${i+1} / ${state.frames.length}</option>`).join("");
  $("#animationFrame").value=String(state.frame);
  $("#animationTicks").value=state.ticks;
  $("#removeFrame").disabled=state.frames.length===1;
  $("#addFrame").disabled=$("#duplicateFrame").disabled=state.frames.length>=64;
  $("#frameEarlier").disabled=state.frame===0;
  $("#frameLater").disabled=state.frame===state.frames.length-1;
  $("#previewAnimation").textContent=state.preview?"Pause preview":"Play preview";
  $("#previewAnimation").setAttribute("aria-pressed",String(state.preview));
}

$("#assetSelect").addEventListener("change", async event=> {
  try { await openLevelEditor(levelState.gameId,event.target.value); }
  catch (error) { setAction(error.message,true); }
});
$("#animationFrame").addEventListener("change",event=> {
  syncFrame(); levelState.frame=Number(event.target.value);
  levelState.rows=levelState.frames[levelState.frame]; renderLevel();
});
function addAnimationFrame(duplicate) {
  if (!levelState || levelState.frames.length>=64) return;
  checkpointLevel();
  const rows=duplicate?copyRows():levelState.rows.map(row=>row.map(()=>levelState.editor.empty));
  levelState.frames.splice(++levelState.frame,0,rows);
  levelState.rows=rows; renderLevel();
}
$("#addFrame").addEventListener("click",()=>addAnimationFrame(false));
$("#duplicateFrame").addEventListener("click",()=>addAnimationFrame(true));
$("#removeFrame").addEventListener("click",()=> {
  if (levelState.frames.length===1) return;
  checkpointLevel(); levelState.frames.splice(levelState.frame,1);
  levelState.frame=Math.min(levelState.frame,levelState.frames.length-1);
  levelState.rows=levelState.frames[levelState.frame]; renderLevel();
});
function moveAnimationFrame(direction) {
  const next=levelState.frame+direction;
  if (next<0 || next>=levelState.frames.length) return;
  checkpointLevel();
  const frames=levelState.frames, current=levelState.frame;
  [frames[current],frames[next]]=[frames[next],frames[current]];
  levelState.frame=next; levelState.rows=frames[next]; renderLevel();
}
$("#frameEarlier").addEventListener("click",()=>moveAnimationFrame(-1));
$("#frameLater").addEventListener("click",()=>moveAnimationFrame(1));
$("#animationTicks").addEventListener("change",event=> {
  const ticks=Number(event.target.value);
  if (!Number.isInteger(ticks) || ticks<1 || ticks>600) {
    event.target.value=levelState.ticks; setAction("Timing must be 1–600 ticks.",true); return;
  }
  checkpointLevel(); levelState.ticks=ticks; renderLevel();
});
$("#previewAnimation").addEventListener("click",()=> {
  levelState.preview=!levelState.preview; levelState.previewStart=performance.now();
  renderLevel();
});
function animatePreview(now) {
  if (levelState?.preview && !$("#levelEditorPanel").closest(".hidden")) {
    syncFrame();
    const tick=Math.floor((now-levelState.previewStart)*60/1000);
    const rows=levelState.frames[Math.floor(tick/levelState.ticks)%levelState.frames.length];
    renderSpritePreview(new Map(levelState.editor.palette.map(tile=>[tile.value,tile])),rows);
  }
  requestAnimationFrame(animatePreview);
}
requestAnimationFrame(animatePreview);

$("#duplicateAsset").addEventListener("click",()=>act($("#duplicateAsset"),"Creating",async()=> {
  if (levelText()!==levelState.savedText) throw new Error("Save your edits before duplicating this asset.");
  const gameId=levelState.gameId;
  const result=await api(`/api/games/${encodeURIComponent(gameId)}/editors`,{
    method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({source:levelState.editorId,id:$("#newAssetName").value.trim()}),
  });
  await loadGames(); await openLevelEditor(gameId,result.editorId); $("#newAssetName").value="";
}));
async function moveCampaignLevel(direction) {
  if (levelText()!==levelState.savedText) throw new Error("Save your edits before reordering the campaign.");
  const {gameId,editorId}=levelState;
  const levels=games.find(g=>g.id===gameId).editors.filter(e=>e.catalogKind==="level");
  const current=levels.findIndex(e=>e.id===editorId), next=current+direction;
  if (next<0 || next>=levels.length) return;
  [levels[current],levels[next]]=[levels[next],levels[current]];
  await api(`/api/games/${encodeURIComponent(gameId)}/campaign`,{
    method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify({ids:levels.map(e=>e.catalogId)}),
  });
  await loadGames(); await openLevelEditor(gameId,editorId);
}
$("#levelEarlier").addEventListener("click",()=>act($("#levelEarlier"),"Moving",()=>moveCampaignLevel(-1)));
$("#levelLater").addEventListener("click",()=>act($("#levelLater"),"Moving",()=>moveCampaignLevel(1)));

function parseLevelText(text) {
  const lines=text.replace(/\r/g, "").split("\n");
  const comments=lines.filter(line=>line.startsWith("# ") && !line.startsWith("# ticks="));
  const frames=[[]];
  let ticks=6;
  for (const line of lines) {
    if (line.startsWith("# ticks=")) { ticks=Number(line.slice(8)); continue; }
    if (!line || line.startsWith("# ")) continue;
    if (line === "---" && levelState.editor.type === "sprite") frames.push([]);
    else frames.at(-1).push([...line]);
  }
  return {comments, frames, ticks, frame:0, rows:frames[0], preview:false, previewTick:0};
}
function syncFrame() { levelState.frames[levelState.frame]=levelState.rows; }
function levelText() {
  syncFrame();
  const comments=[...levelState.comments];
  if (levelState.editor.type === "sprite") comments.push(`# ticks=${levelState.ticks}`);
  const body=levelState.frames.map(rows=>rows.map(row=>row.join("")).join("\n")).join("\n---\n");
  return `${comments.length ? comments.join("\n")+"\n" : ""}${body}\n`;
}
function copyRows(rows = levelState.rows) { return rows.map(row=>[...row]); }
function snapshotAsset() {
  syncFrame();
  return {frames:levelState.frames.map(copyRows), frame:levelState.frame, ticks:levelState.ticks};
}
function restoreAsset(state) {
  Object.assign(levelState,state);
  levelState.rows=levelState.frames[levelState.frame];
  renderLevel();
}
function checkpointLevel() {
  levelState.undo.push(snapshotAsset());
  if (levelState.undo.length>100) levelState.undo.shift();
  levelState.redo=[];
}
function changeHistory(from,to) {
  if (!levelState || !levelState[from].length) return;
  levelState[to].push(snapshotAsset());
  restoreAsset(levelState[from].pop());
}

function validateLevel() {
  const errors = [];
  const palette = new Map(levelState.editor.palette.map((tile) => [tile.value, tile]));
  const counts = Object.fromEntries(levelState.editor.palette.map((tile) => [tile.value, 0]));
  syncFrame();
  levelState.frames.forEach(rows => rows.forEach((row, y) => row.forEach((value, x) => {
    if (!palette.has(value)) errors.push(`Unknown tile ${JSON.stringify(value)} at ${x + 1}, ${y + 1}.`);
    else counts[value]++;
  })));
  levelState.editor.palette.forEach((tile) => {
    if (tile.minimum !== undefined && counts[tile.value] < tile.minimum)
      errors.push(`${tile.name} requires at least ${tile.minimum}; found ${counts[tile.value]}.`);
    if (tile.maximum !== undefined && counts[tile.value] > tile.maximum)
      errors.push(`${tile.name} allows at most ${tile.maximum}; found ${counts[tile.value]}.`);
  });
  const width=levelState.rows[0]?.length, height=levelState.rows.length;
  const maximum=levelState.editor.type === "sprite" ? 128 : 512;
  if (!width || !height || width>maximum || height>maximum ||
      levelState.frames.some(rows=>rows.length!==height || rows.some(row=>row.length!==width)))
    errors.push("All frames must be rectangular and have the same valid dimensions.");
  if (!Number.isInteger(levelState.ticks) || levelState.ticks<1 || levelState.ticks>600)
    errors.push("Timing must be between 1 and 600 ticks.");
  return errors;
}

function updateLevelState() {
  if (!levelState) return;
  const errors = validateLevel();
  const validation = $("#levelValidation");
  validation.textContent = errors.length ? errors[0] :
    `${levelState.editor.type === "sprite" ? "Sprite" : "Level"} is valid`;
  validation.title = errors.join("\n");
  validation.classList.toggle("invalid", errors.length > 0);
  $("#levelDirty").textContent = levelText() === levelState.savedText ?
    "No unsaved changes." : "Unsaved changes.";
  updateAnimationControls();
  $("#levelWidth").value = levelState.rows[0].length;
  $("#levelHeight").value = levelState.rows.length;
  $("#undoLevel").disabled = !levelState.undo.length;
  $("#redoLevel").disabled = !levelState.redo.length;
  $("#saveLevelLocal").disabled = errors.length > 0;
  $("#saveLevelPlay").disabled = errors.length > 0;
  $("#saveLevelBrowser").disabled = errors.length > 0;
}

function renderLevel() {
  if (!levelState) return;
  const canvas = $("#levelCanvas");
  const cell = levelState.cell;
  const width = levelState.rows[0].length;
  const height = levelState.rows.length;
  canvas.width = width * cell;
  canvas.height = height * cell;
  const context = canvas.getContext("2d");
  const palette = new Map(levelState.editor.palette.map((tile) => [tile.value, tile]));
  context.textAlign = "center";
  context.textBaseline = "middle";
  context.font = `700 ${Math.max(8, cell - 7)}px ui-monospace, monospace`;
  levelState.rows.forEach((row, y) => row.forEach((value, x) => {
    const tile = palette.get(value);
    context.fillStyle = tile?.color || "#ff00ff";
    context.fillRect(x * cell, y * cell, cell, cell);
    if (value !== levelState.editor.empty && cell >= 16) {
      context.fillStyle = "rgba(0,0,0,.68)";
      context.fillText(value, x * cell + cell / 2, y * cell + cell / 2 + .5);
    }
  }));
  if (cell >= 12) {
    context.beginPath();
    for (let x = 0; x <= width; x++) { context.moveTo(x * cell + .5, 0); context.lineTo(x * cell + .5, height * cell); }
    for (let y = 0; y <= height; y++) { context.moveTo(0, y * cell + .5); context.lineTo(width * cell, y * cell + .5); }
    context.strokeStyle = "rgba(255,255,255,.08)";
    context.lineWidth = 1;
    context.stroke();
  }
  const viewport = levelState.editor.viewport;
  if (viewport) {
    context.save();
    context.setLineDash([7, 5]);
    context.strokeStyle = "rgba(243,184,69,.9)";
    context.lineWidth = 2;
    for (let x = viewport.width; x < width; x += viewport.width) {
      context.beginPath(); context.moveTo(x * cell, 0); context.lineTo(x * cell, height * cell); context.stroke();
    }
    for (let y = viewport.height; y < height; y += viewport.height) {
      context.beginPath(); context.moveTo(0, y * cell); context.lineTo(width * cell, y * cell); context.stroke();
    }
    context.restore();
  }
  if (levelState.editor.type === "sprite") renderSpritePreview(palette);
  updateLevelState();
}

function renderSpritePreview(palette, rows = levelState.rows) {
  const canvas = $("#spritePreview");
  const width = rows[0].length, height = rows.length;
  canvas.width = width; canvas.height = height;
  const scale = Math.max(1, Math.min(8, Math.floor(128 / width), Math.floor(112 / height)));
  canvas.style.width = `${width * scale}px`;
  canvas.style.height = `${height * scale}px`;
  const context = canvas.getContext("2d");
  context.clearRect(0, 0, width, height);
  rows.forEach((row, y) => row.forEach((value, x) => {
    if (value === levelState.editor.empty) return;
    context.fillStyle = palette.get(value)?.color || "#ff00ff";
    context.fillRect(x, y, 1, 1);
  }));
  $("#spritePreviewSize").textContent = `${width}×${height} native pixels`;
}

function renderLevelPalette() {
  const palette = $("#tilePalette");
  palette.innerHTML = levelState.editor.palette.map((tile) => `
    <button class="tile-choice ${tile.value === levelState.tile ? "active" : ""}" data-tile="${escapeHtml(tile.value)}">
      <span class="tile-swatch" style="background:${tile.color}">${escapeHtml(tile.value)}</span><span class="tile-name">${escapeHtml(tile.name)}</span>
    </button>`).join("");
  palette.querySelectorAll("[data-tile]").forEach((button) => button.addEventListener("click", () => {
    levelState.tile = button.dataset.tile;
    if (levelState.tool === "erase") selectLevelTool("pencil");
    renderLevelPalette();
  }));
}

function selectLevelTool(tool) {
  levelState.tool = tool;
  $("#levelTools").querySelectorAll("[data-tool]").forEach((button) =>
    button.classList.toggle("active", button.dataset.tool === tool));
}

async function openLevelEditor(gameId, editorId) {
  if (levelState && levelText() !== levelState.savedText &&
      !confirm("Discard the unsaved asset changes?")) { $("#assetSelect").value=levelState.editorId; return; }
  const request=++editorRequest;
  const data = await api(`/api/games/${encodeURIComponent(gameId)}/editors/${encodeURIComponent(editorId)}`);
  if (request!==editorRequest) return;
  const isSprite = data.editor.type === "sprite";
  if (!isSprite && displayViewport) data.editor.viewport={
    width:displayViewport.width/data.editor.tileSize,height:displayViewport.height/data.editor.tileSize};
  levelState = { gameId, editorId, editor:data.editor, hash:data.hash, savedText:data.text,
    comments:[], rows:[], cell:isSprite ? 32 : 16, tile:data.editor.palette.find((tile) => tile.value !== data.editor.empty)?.value || data.editor.empty,
    tool:"pencil", undo:[], redo:[], drawing:false, start:null, last:null };
  Object.assign(levelState, parseLevelText(data.text));
  if (!data.validation.valid) { levelState=null; throw new Error(data.validation.errors.join(" ")); }
  levelState.savedText=levelText();
  const editors=games.find(game=>game.id===gameId).editors;
  $("#assetSelect").innerHTML=["tilemap","sprite"].map(type=>`<optgroup label="${type === "sprite" ? "Sprites & animations" : "Campaign levels"}">${editors.filter(e=>e.type===type).map(e=>`<option value="${e.id}">${escapeHtml(e.name)}</option>`).join("")}</optgroup>`).join("");
  $("#assetSelect").value=editorId;
  $("#animationControls").classList.toggle("hidden", !isSprite);
  $("#duplicateAsset").disabled=!data.editor.catalogKind;
  $("#levelEarlier").classList.toggle("hidden", data.editor.catalogKind!=="level");
  $("#levelLater").classList.toggle("hidden", data.editor.catalogKind!=="level");
  const levels=editors.filter(e=>e.catalogKind==="level");
  $("#levelEarlier").disabled=levels[0]?.id===editorId;
  $("#levelLater").disabled=levels.at(-1)?.id===editorId;
  $("#editorPanel").classList.add("hidden");
  $("#levelEditorTitle").textContent = `${games.find((game) => game.id === gameId)?.name || gameId} · ${data.editor.name}`;
  $("#gridEditorKind").textContent = isSprite ? "Pixel sprite editor" : "Tilemap editor";
  $("#gridPaletteTitle").textContent = isSprite ? "Palette" : "Tiles";
  $("#gridSizeTitle").textContent = isSprite ? "Image size" : "Map size";
  $("#gridEditorHelp").textContent = isSprite ?
    "Transparent pixels show the checkerboard behind the native preview." :
    `Each dashed frame is the ${Math.round(data.editor.viewport.width*data.editor.tileSize)}×${Math.round(data.editor.viewport.height*data.editor.tileSize)} playable area.`;
  $("#spritePreviewPanel").classList.toggle("hidden", !isSprite);
  $("#flipSprite").classList.toggle("hidden", !isSprite);
  $("#levelCanvas").setAttribute("aria-label", isSprite ? "Editable pixel sprite" : "Editable level tilemap");
  $("#levelCanvasScroll").classList.toggle("sprite", isSprite);
  $("#levelWidth").max = isSprite ? "128" : "512";
  $("#levelHeight").max = isSprite ? "128" : "512";
  $("#levelZoom").value = String(levelState.cell);
  selectLevelTool("pencil");
  renderLevelPalette();
  renderLevel();
  $("#levelEditorPanel").classList.remove("hidden");
  location.hash=`edit/${gameId}`;showPage(`edit/${gameId}`);
  $("#levelEditorPanel").scrollIntoView({ behavior:"smooth", block:"start" });
}

function cellAt(event) {
  const canvas = $("#levelCanvas");
  const bounds = canvas.getBoundingClientRect();
  return {
    x: Math.floor((event.clientX - bounds.left) * canvas.width / bounds.width / levelState.cell),
    y: Math.floor((event.clientY - bounds.top) * canvas.height / bounds.height / levelState.cell),
  };
}

function insideLevel(point) {
  return point.x >= 0 && point.y >= 0 && point.y < levelState.rows.length && point.x < levelState.rows[0].length;
}

function paintCell(point, value) {
  if (insideLevel(point)) levelState.rows[point.y][point.x] = value;
}

function paintLine(from, to, value) {
  let x = from.x, y = from.y;
  const dx = Math.abs(to.x - x), sx = x < to.x ? 1 : -1;
  const dy = -Math.abs(to.y - y), sy = y < to.y ? 1 : -1;
  let error = dx + dy;
  while (true) {
    paintCell({ x, y }, value);
    if (x === to.x && y === to.y) break;
    const doubled = 2 * error;
    if (doubled >= dy) { error += dy; x += sx; }
    if (doubled <= dx) { error += dx; y += sy; }
  }
}

function paintRectangle(from, to, value) {
  const left = Math.min(from.x, to.x), right = Math.max(from.x, to.x);
  const top = Math.min(from.y, to.y), bottom = Math.max(from.y, to.y);
  for (let x = left; x <= right; x++) { paintCell({ x, y:top }, value); paintCell({ x, y:bottom }, value); }
  for (let y = top; y <= bottom; y++) { paintCell({ x:left, y }, value); paintCell({ x:right, y }, value); }
}

function floodFill(start, replacement) {
  if (!insideLevel(start)) return;
  const target = levelState.rows[start.y][start.x];
  if (target === replacement) return;
  const pending = [start];
  while (pending.length) {
    const point = pending.pop();
    if (!insideLevel(point) || levelState.rows[point.y][point.x] !== target) continue;
    levelState.rows[point.y][point.x] = replacement;
    pending.push({x:point.x-1,y:point.y},{x:point.x+1,y:point.y},{x:point.x,y:point.y-1},{x:point.x,y:point.y+1});
  }
}

async function saveLevel(play) {
  const state=levelState, text=levelText();
  const result=await api(`/api/games/${encodeURIComponent(state.gameId)}/editors/${encodeURIComponent(state.editorId)}`, {
    method:"PUT", headers:{"Content-Type":"application/json"},
    body:JSON.stringify({text, hash:state.hash, play}),
  });
  state.hash=result.hash; state.savedText=text;
  if (levelState===state) updateLevelState();
  if (result.playError) throw new Error(result.playError);
  if (play) { activeGame=state.gameId; setTimeout(refreshStatus,350); }
}

function closeLevelEditor() {
  if (levelState && levelText() !== levelState.savedText &&
      !confirm("Close the editor and discard the unsaved changes?")) return;
  $("#levelEditorPanel").classList.add("hidden");
  editorRequest++;
  levelState = null;
}

$("#closeLevelEditor").addEventListener("click", closeLevelEditor);
$("#levelTools").querySelectorAll("[data-tool]").forEach((button) => button.addEventListener("click", () => {
  if (levelState) selectLevelTool(button.dataset.tool);
}));
$("#levelZoom").addEventListener("change", (event) => {
  if (!levelState) return;
  levelState.cell = Number(event.target.value);
  renderLevel();
});
$("#undoLevel").addEventListener("click", () => changeHistory("undo", "redo"));
$("#redoLevel").addEventListener("click", () => changeHistory("redo", "undo"));
$("#flipSprite").addEventListener("click", () => {
  if (!levelState || levelState.editor.type !== "sprite") return;
  checkpointLevel();
  levelState.rows.forEach((row) => row.reverse());
  renderLevel();
});
$("#resizeLevel").addEventListener("click", () => {
  if (!levelState) return;
  const width = Number($("#levelWidth").value), height = Number($("#levelHeight").value);
  const maximum = levelState.editor.type === "sprite" ? 128 : 512;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > maximum || height > maximum) {
    setAction(`Dimensions must be between 1 and ${maximum}.`, true); return;
  }
  const oldWidth = levelState.rows[0].length, oldHeight = levelState.rows.length;
  if (width === oldWidth && height === oldHeight) return;
  const empty = levelState.editor.empty;
  syncFrame();
  const losesTiles = levelState.frames.some(rows => rows.some((row, y) => y >= height && row.some((tile) => tile !== empty) ||
    y < height && row.slice(width).some((tile) => tile !== empty)));
  if (losesTiles && !confirm("Shrinking the map will remove non-empty tiles. Continue?")) return;
  checkpointLevel();
  levelState.frames = levelState.frames.map(rows => Array.from({length:height}, (_, y) =>
    Array.from({length:width}, (_, x) => rows[y]?.[x] ?? empty)));
  levelState.rows=levelState.frames[levelState.frame];
  renderLevel();
});
$("#saveLevelLocal").addEventListener("click", () => act($("#saveLevelLocal"), "Saving", () => saveLevel(false)));
$("#saveLevelBrowser").addEventListener("click", () => {
  const state=levelState;
  const index=games.find(game=>game.id===state.gameId).editors.filter(editor=>editor.catalogKind==="level").findIndex(editor=>editor.id===state.editorId);
  const player=window.open("about:blank","chirky-play");
  act($("#saveLevelBrowser"),"Saving and launching",async()=>{
    try {
      await saveLevel(false);
      const url=`/play/?game=${encodeURIComponent(state.gameId)}${index>=0?`&level=${index}`:""}`;
      if(player)player.location.href=url;else throw new Error("Saved. Allow pop-ups to open the browser player.");
    } catch(error) {if(player)player.close();throw error;}
  });
});
$("#saveLevelPlay").addEventListener("click", () => act($("#saveLevelPlay"), "Saving and launching", () => saveLevel(true)));

const levelCanvas = $("#levelCanvas");
levelCanvas.addEventListener("pointerdown", (event) => {
  if (!levelState || event.button !== 0) return;
  const point = cellAt(event);
  if (!insideLevel(point)) return;
  event.preventDefault();
  levelCanvas.setPointerCapture(event.pointerId);
  levelState.drawing = true; levelState.start = point; levelState.last = point;
  checkpointLevel();
  const value = levelState.tool === "erase" ? levelState.editor.empty : levelState.tile;
  if (levelState.tool === "fill") { floodFill(point, value); levelState.drawing = false; renderLevel(); }
  else if (levelState.tool === "pencil" || levelState.tool === "erase") { paintCell(point, value); renderLevel(); }
});
levelCanvas.addEventListener("pointermove", (event) => {
  if (!levelState) return;
  const point = cellAt(event);
  $("#levelCoordinates").textContent = insideLevel(point) ? `Tile ${point.x}, ${point.y}` : "—";
  if (!levelState.drawing || !insideLevel(point) ||
      (point.x === levelState.last.x && point.y === levelState.last.y)) return;
  if (levelState.tool === "pencil" || levelState.tool === "erase") {
    const value = levelState.tool === "erase" ? levelState.editor.empty : levelState.tile;
    paintLine(levelState.last, point, value); levelState.last = point; renderLevel();
  }
});
function finishLevelStroke(event) {
  if (!levelState?.drawing) return;
  const point = cellAt(event);
  const end = insideLevel(point) ? point : levelState.last;
  const value = levelState.tool === "erase" ? levelState.editor.empty : levelState.tile;
  if (levelState.tool === "line") paintLine(levelState.start, end, value);
  else if (levelState.tool === "rectangle") paintRectangle(levelState.start, end, value);
  levelState.drawing = false;
  renderLevel();
}
levelCanvas.addEventListener("pointerup", finishLevelStroke);
levelCanvas.addEventListener("pointercancel", finishLevelStroke);
window.addEventListener("keydown", (event) => {
  if (!levelState || $("#levelEditorPanel").closest(".hidden") || !(event.ctrlKey || event.metaKey)) return;
  if (event.key.toLowerCase() === "z") { event.preventDefault(); changeHistory(event.shiftKey ? "redo" : "undo", event.shiftKey ? "undo" : "redo"); }
  else if (event.key.toLowerCase() === "y") { event.preventDefault(); changeHistory("redo", "undo"); }
});
window.addEventListener("beforeunload", (event) => {
  if (levelState && levelText() !== levelState.savedText) event.preventDefault();
});
