"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawn } = require("child_process");

const DASHBOARD = __dirname;
const ROOT = path.resolve(DASHBOARD, "..");
const PUBLIC = path.join(DASHBOARD, "public");
const CAPTURES = path.join(DASHBOARD, "captures");
const HOST_CONFIG = path.join(ROOT, "config", "host.conf");
const config = {
  ...JSON.parse(fs.readFileSync(path.join(DASHBOARD, "config.json"), "utf8")),
  ...readOptionalJson(path.join(DASHBOARD, "config.local.json")),
};
if (process.env.TWO_FORTY_DASHBOARD_PORT) config.port=Number(process.env.TWO_FORTY_DASHBOARD_PORT);
fs.mkdirSync(CAPTURES, { recursive: true });

function readOptionalJson(file) {
  try { return JSON.parse(fs.readFileSync(file, "utf8")); }
  catch { return {}; }
}

function command(program, args, timeoutMs = 20_000) {
  return new Promise((resolve, reject) => {
    const child = spawn(program, args, { windowsHide: true });
    const stdout = [];
    const stderr = [];
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error(`${program} timed out`));
    }, timeoutMs);
    child.stdout.on("data", (chunk) => stdout.push(chunk));
    child.stderr.on("data", (chunk) => stderr.push(chunk));
    child.on("error", (error) => { clearTimeout(timer); reject(error); });
    child.on("close", (code) => {
      clearTimeout(timer);
      const result = {
        code,
        stdout: Buffer.concat(stdout).toString("utf8").trim(),
        stderr: Buffer.concat(stderr).toString("utf8").trim(),
      };
      if (code === 0) resolve(result);
      else reject(Object.assign(new Error(result.stderr || `${program} exited ${code}`), { result }));
    });
  });
}

function sshBase() {
  const args = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=4"];
  if (config.identityFile) args.push("-i", config.identityFile);
  if (config.knownHostsFile) {
    args.push("-o", `UserKnownHostsFile=${config.knownHostsFile}`,
      "-o", "StrictHostKeyChecking=yes");
  }
  args.push(`${config.user}@${config.host}`);
  return args;
}

function ssh(remoteCommand, timeoutMs) {
  return command("ssh", [...sshBase(), remoteCommand], timeoutMs);
}

function scpArgs() {
  const args = ["-q", "-o", "BatchMode=yes"];
  if (config.identityFile) args.push("-i", config.identityFile);
  if (config.knownHostsFile) {
    args.push("-o", `UserKnownHostsFile=${config.knownHostsFile}`,
      "-o", "StrictHostKeyChecking=yes");
  }
  return args;
}

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

const {gameDirectory, editorAssetPath, loadEditors, findEditor, textHash, validateEditorText,
  writeAtomic, readCatalog, createEditor, reorderLevels, renameLevel} = require("./editors");

function listGames() {
  const root = path.join(ROOT, "games");
  if (!fs.existsSync(root)) return [];
  return fs.readdirSync(root, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => {
      const file = path.join(root, entry.name, "game.conf");
      if (!fs.existsSync(file)) return null;
      const text = fs.readFileSync(file, "utf8");
      const values = parseConfig(text);
      const assetsPath = path.join(root, entry.name, "assets");
      const assets = fs.existsSync(assetsPath)
        ? fs.readdirSync(assetsPath, { withFileTypes: true })
          .filter((asset) => asset.isFile())
          .map((asset) => ({
            name: asset.name,
            size: fs.statSync(path.join(assetsPath, asset.name)).size,
            url: `/api/games/${encodeURIComponent(entry.name)}/assets/${encodeURIComponent(asset.name)}`,
          }))
        : [];
      const editors = loadEditors(entry.name).map(({ id, name, type, catalogKind, catalogId }) => ({ id, name, type, catalogKind, catalogId }));
      return { id: values.id || entry.name, name: values.name || entry.name,
        description: values.description || "", values, assets, editors };
    }).filter(Boolean);
}

const launcherConfig=require("./launcher");
const LAUNCHER_CONFIG=path.join(ROOT,"config","launcher.conf");
let launcherSaving=false;
async function readLauncher() {
  const available=listGames();
  let text,source="pi";
  try {text=(await ssh(`cat ${config.remoteRoot}/config/launcher.conf`)).stdout;}
  catch {source="local";text=fs.existsSync(LAUNCHER_CONFIG)?fs.readFileSync(LAUNCHER_CONFIG,"utf8"):"";}
  try {return {items:launcherConfig.parse(text,available),source};}
  catch {return {items:launcherConfig.defaults(available),source,defaults:true};}
}
async function saveLauncher(request,response) {
  if(launcherSaving)return json(response,409,{error:"A launcher update is already in progress."});
  launcherSaving=true;
  try {
    const body=await requestBody(request);let text;
    try {text=launcherConfig.serialize(body.items,listGames());}
    catch(error){return json(response,400,{error:error.message});}
    // Upload separately, then atomically replace the Pi's configuration.
    const temporary=path.join(ROOT,"build","launcher-upload.conf");
    fs.mkdirSync(path.dirname(temporary),{recursive:true});fs.writeFileSync(temporary,text);
    await command("scp",[...scpArgs(),temporary,`${config.user}@${config.host}:${config.remoteRoot}/config/launcher.conf.next`]);
    await ssh(`cd ${config.remoteRoot} && mv config/launcher.conf.next config/launcher.conf`);
    writeAtomic(LAUNCHER_CONFIG,text);
    await sendControl("launcher-reload");
    json(response,200,{ok:true,items:launcherConfig.parse(text,listGames()),source:"pi"});
  } finally {launcherSaving=false;}
}

function readBootGame() {
  if (!fs.existsSync(HOST_CONFIG)) return "launcher";
  return parseConfig(fs.readFileSync(HOST_CONFIG, "utf8")).boot_game || "launcher";
}

function writeBootGame(game, sourceText) {
  fs.mkdirSync(path.dirname(HOST_CONFIG), { recursive: true });
  let text = sourceText !== undefined ? sourceText
    : (fs.existsSync(HOST_CONFIG) ? fs.readFileSync(HOST_CONFIG, "utf8") : "");
  if (/^boot_game=.*$/m.test(text)) text = text.replace(/^boot_game=.*$/m, `boot_game=${game}`);
  else text = `${text.trimEnd()}\nboot_game=${game}\n`;
  fs.writeFileSync(HOST_CONFIG, text.endsWith("\n") ? text : `${text}\n`, "utf8");
}

async function requestBody(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 2 * 1024 * 1024) throw new Error("request too large");
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
}

function json(response, status, body) {
  const data = Buffer.from(JSON.stringify(body));
  response.writeHead(status, { "Content-Type": "application/json", "Content-Length": data.length,
    "Cache-Control": "no-store" });
  response.end(data);
}

function contentType(file) {
  const extension = path.extname(file).toLowerCase();
  return ({ ".html":"text/html; charset=utf-8", ".css":"text/css; charset=utf-8",
    ".js":"text/javascript; charset=utf-8", ".wav":"audio/wav", ".ppm":"image/x-portable-pixmap" })[extension]
    || "application/octet-stream";
}

function sendFile(response, file) {
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) return json(response, 404, { error: "not found" });
  response.writeHead(200, { "Content-Type": contentType(file), "Content-Length": fs.statSync(file).size,
    "Cache-Control": "no-store" });
  fs.createReadStream(file).pipe(response);
}

async function sendControl(commandText) {
  if (!/^(menu|snapshot|reload|quit|poweroff|launcher-reload|settings (input|display|hardware)|launch [a-z0-9-]+)$/.test(commandText))
    throw new Error("invalid control command");
  return ssh(`cd ${config.remoteRoot} && printf '%s\\n' '${commandText}' > run/control.fifo`);
}

const routes = {
  async status(_request, response) {
    try {
      const result = await ssh(`cd ${config.remoteRoot} && cat run/status.json && printf '\\n---\\n' && vcgencmd measure_temp && vcgencmd get_throttled && uptime -p`);
      const [statusText, diagnostics = ""] = result.stdout.split("\n---\n");
      json(response, 200, { online: true, status: JSON.parse(statusText), diagnostics });
    } catch (error) {
      json(response, 200, { online: false, error: error.message });
    }
  },

  async games(_request, response) {
    json(response, 200, { games: listGames(), bootGame: readBootGame() });
  },

  async boot(request, response) {
    const body = await requestBody(request);
    const available = new Set(["launcher", ...listGames().map((game) => game.id)]);
    if (!available.has(body.game)) return json(response, 400, { error: "unknown boot target" });
    let remoteConfig;
    try {
      remoteConfig = (await ssh(`cat ${config.remoteRoot}/config/host.conf`)).stdout;
    } catch {
      remoteConfig = undefined;
    }
    writeBootGame(body.game, remoteConfig);
    await command("scp", [...scpArgs(), HOST_CONFIG,
      `${config.user}@${config.host}:${config.remoteRoot}/config/host.conf`]);
    json(response, 200, { ok: true, bootGame: body.game });
  },

  async control(request, response) {
    const body = await requestBody(request);
    const action = body.action;
    const commandText = action === "launch" ? `launch ${body.game}` : action;
    await sendControl(commandText);
    json(response, 200, { ok: true });
  },

  async deploy(_request, response) {
    await ssh(`mkdir -p ${config.remoteRoot}`);
    const hasHostConfig = (await ssh(
      `test -f ${config.remoteRoot}/config/host.conf && printf yes || true`)).stdout === "yes";
    const sources = ["Makefile", "README.md", "include", "src", "games", "tools", "deploy", "provision"]
      .map((item) => path.join(ROOT, item));
    await command("scp", [...scpArgs(), "-r", ...sources,
      `${config.user}@${config.host}:${config.remoteRoot}/`], 60_000);
    if (!hasHostConfig) {
      await ssh(`mkdir -p ${config.remoteRoot}/config`);
      await command("scp", [...scpArgs(), HOST_CONFIG,
        `${config.user}@${config.host}:${config.remoteRoot}/config/host.conf`]);
    }
    // scp overlays files, so explicitly retire the removed diagnostic package.
    const hasLauncher=(await ssh(`test -f ${config.remoteRoot}/config/launcher.conf && printf yes || true`)).stdout==="yes";
    if(!hasLauncher)await command("scp",[...scpArgs(),LAUNCHER_CONFIG,`${config.user}@${config.host}:${config.remoteRoot}/config/launcher.conf`]);
    const built = await ssh(`cd ${config.remoteRoot} && rm -rf -- games/latency-test && rm -f -- build/games/latency-test.so include/frame_probe.h && make && if sudo -n systemctl cat two-forty.service >/dev/null 2>&1; then sudo -n systemctl restart two-forty.service; else (printf 'quit\\n' > run/control.fifo 2>/dev/null || true); sleep 1; setsid -f ./build/two-forty-host > run/two-forty.log 2>&1 </dev/null; fi`, 60_000);
    json(response, 200, { ok: true, output: built.stdout || "Build is up to date." });
  },

  async restart(_request, response) {
    await ssh(`cd ${config.remoteRoot} && if sudo -n systemctl cat two-forty.service >/dev/null 2>&1; then sudo -n systemctl restart two-forty.service; else (printf 'quit\\n' > run/control.fifo 2>/dev/null || true); sleep 1; mkdir -p run; setsid -f ./build/two-forty-host > run/two-forty.log 2>&1 </dev/null; fi`);
    json(response, 200, { ok: true });
  },

  async logs(_request, response) {
    try {
      const result = await ssh(`cd ${config.remoteRoot} && tail -n 160 run/two-forty.log 2>/dev/null || true`);
      json(response, 200, { logs: result.stdout });
    } catch (error) { json(response, 502, { error: error.message }); }
  },

  async snapshot(_request, response) {
    await sendControl("snapshot");
    await new Promise((resolve) => setTimeout(resolve, 350));
    const latest = await ssh(`cd ${config.remoteRoot} && latest=$(ls -1t snapshots/*.ppm 2>/dev/null | head -1); test -n "$latest" && printf '%s' "$latest"`);
    const remote = latest.stdout;
    const name = path.basename(remote);
    await command("scp", [...scpArgs(), `${config.user}@${config.host}:${config.remoteRoot}/${remote}`,
      path.join(CAPTURES, name)], 20_000);
    json(response, 200, { ok: true, name, url: `/captures/${encodeURIComponent(name)}?t=${Date.now()}` });
  },
};

// A playtest deploys one coherent game package and launches the selected level.
// The temporary start level is remote-only; local campaign settings stay intact.
async function playEditedGame(id, editor) {
  const directory=gameDirectory(id);
  let configText=fs.readFileSync(path.join(directory,"game.conf"),"utf8");
  if (editor.catalogKind === "level") {
    const document=JSON.parse(fs.readFileSync(path.join(directory,"editor.json"),"utf8"));
    const index=readCatalog(directory,document.catalog).filter(e=>e.kind==="level").findIndex(e=>e.id===editor.catalogId);
    configText=configText.replace(/^start_level=.*\r?$/m, "");
    configText+=`\nstart_level=${index}\n`;
  }
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),"two-forty-play-"));
  try {
    const configFile=path.join(temporary,"game.conf");
    fs.writeFileSync(configFile,configText);
    await ssh(`mkdir -p ${config.remoteRoot}/games`);
    await command("scp",[...scpArgs(),"-r",directory,`${config.user}@${config.host}:${config.remoteRoot}/games/`],60_000);
    await command("scp",[...scpArgs(),path.join(ROOT,"Makefile"),`${config.user}@${config.host}:${config.remoteRoot}/Makefile`]);
    await command("scp",[...scpArgs(),configFile,`${config.user}@${config.host}:${config.remoteRoot}/games/${id}/game.conf`]);
    await ssh(`cd ${config.remoteRoot} && make build/games/${id}.so`,60_000);
    await sendControl(`launch ${id}`);
  } finally {
    fs.unlinkSync(path.join(temporary,"game.conf")); fs.rmdirSync(temporary);
  }
}

async function handle(request, response) {
  const url = new URL(request.url, "http://localhost");
  try {
    if (request.method === "GET" && url.pathname === "/api/launcher") return json(response,200,await readLauncher());
    if (request.method === "PUT" && url.pathname === "/api/launcher") return await saveLauncher(request,response);
    if (request.method === "GET" && url.pathname === "/api/status") return routes.status(request, response);
    if (request.method === "GET" && url.pathname === "/api/games") return routes.games(request, response);
    if (request.method === "PUT" && url.pathname === "/api/boot") return routes.boot(request, response);
    if (request.method === "POST" && url.pathname === "/api/control") return routes.control(request, response);
    if (request.method === "POST" && url.pathname === "/api/deploy") return routes.deploy(request, response);
    if (request.method === "POST" && url.pathname === "/api/restart") return routes.restart(request, response);
    if (request.method === "GET" && url.pathname === "/api/logs") return routes.logs(request, response);
    if (request.method === "POST" && url.pathname === "/api/snapshot") return routes.snapshot(request, response);

    let match = /^\/api\/games\/([^/]+)\/config$/.exec(url.pathname);
    if (match && request.method === "GET") {
      const id = decodeURIComponent(match[1]);
      const file = path.join(gameDirectory(id), "game.conf");
      return json(response, 200, { id, text: fs.readFileSync(file, "utf8") });
    }
    if (match && request.method === "PUT") {
      const id = decodeURIComponent(match[1]);
      const body = await requestBody(request);
      if (typeof body.text !== "string" || body.text.length > 64 * 1024 || body.text.includes("\0"))
        return json(response, 400, { error: "invalid configuration" });
      const file = path.join(gameDirectory(id), "game.conf");
      fs.writeFileSync(file, body.text.endsWith("\n") ? body.text : `${body.text}\n`, "utf8");
      if (body.deploy) {
        await command("scp", [...scpArgs(), file,
          `${config.user}@${config.host}:${config.remoteRoot}/games/${id}/game.conf`]);
        await sendControl("reload");
      }
      return json(response, 200, { ok: true });
    }

    match = /^\/api\/games\/([^/]+)\/editors\/([^/]+)\/name$/.exec(url.pathname);
    if (match && request.method === "PUT") {
      const body=await requestBody(request);
      renameLevel(decodeURIComponent(match[1]),decodeURIComponent(match[2]),body.name,body.hash);
      return json(response,200,{ok:true});
    }
    match = /^\/api\/games\/([^/]+)\/editors$/.exec(url.pathname);
    if (match && request.method === "POST") {
      const editorId = createEditor(decodeURIComponent(match[1]), await requestBody(request));
      return json(response, 201, {editorId});
    }
    match = /^\/api\/games\/([^/]+)\/campaign$/.exec(url.pathname);
    if (match && request.method === "PUT") {
      reorderLevels(decodeURIComponent(match[1]), (await requestBody(request)).ids);
      return json(response,200,{ok:true});
    }
    match = /^\/api\/games\/([^/]+)\/editors\/([^/]+)$/.exec(url.pathname);
    if (match && request.method === "GET") {
      const id = decodeURIComponent(match[1]);
      const editorId = decodeURIComponent(match[2]);
      const editor = findEditor(id, editorId);
      const text = fs.readFileSync(editorAssetPath(gameDirectory(id), editor.file), "utf8");
      return json(response, 200, { id, editor, text, hash: textHash(text),
        validation: validateEditorText(editor, text) });
    }
    if (match && request.method === "PUT") {
      const id = decodeURIComponent(match[1]);
      const editorId = decodeURIComponent(match[2]);
      const editor = findEditor(id, editorId);
      const file = editorAssetPath(gameDirectory(id), editor.file);
      const body = await requestBody(request);
      if (typeof body.text !== "string" || body.text.length > 2 * 1024 * 1024 || body.text.includes("\0"))
        return json(response, 400, { error: "invalid editor data" });
      const text = body.text.endsWith("\n") ? body.text : `${body.text}\n`;
      const current = fs.readFileSync(file, "utf8");
      if (body.hash && body.hash !== textHash(current))
        return json(response, 409, { error: "The asset changed on disk. Reopen it before saving." });
      const validation = validateEditorText(editor, text);
      if (!validation.valid) return json(response, 400,
        { error: validation.errors.join(" "), validation });
      writeAtomic(file, text);
      let playError;
      if (body.play) {
        try { await playEditedGame(id, editor); }
        catch (error) { playError=`Saved locally, but playtesting failed: ${error.message}`; }
      }
      return json(response, 200, { ok: true, hash: textHash(text), validation, playError });
    }

    match = /^\/api\/games\/([^/]+)\/assets\/([^/]+)$/.exec(url.pathname);
    if (match && request.method === "GET") {
      const id = decodeURIComponent(match[1]);
      const name = decodeURIComponent(match[2]);
      if (path.basename(name) !== name) return json(response, 400, { error: "invalid asset" });
      return sendFile(response, path.join(gameDirectory(id), "assets", name));
    }

    match = /^\/captures\/([^/]+)$/.exec(url.pathname);
    if (match && request.method === "GET") {
      const name = decodeURIComponent(match[1]);
      if (path.basename(name) !== name) return json(response, 400, { error: "invalid capture" });
      return sendFile(response, path.join(CAPTURES, name));
    }

    if (request.method === "GET") {
      const relative = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
      const file = path.resolve(PUBLIC, relative);
      if (file.startsWith(`${PUBLIC}${path.sep}`) || file === path.join(PUBLIC, "index.html"))
        return sendFile(response, file);
    }
    json(response, 404, { error: "not found" });
  } catch (error) {
    console.error(error);
    json(response, error.status || 500, { error: error.message });
  }
}

http.createServer(handle).listen(config.port, "127.0.0.1", () => {
  console.log(`Two Forty dashboard: http://127.0.0.1:${config.port}`);
});
