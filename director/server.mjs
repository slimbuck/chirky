import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const DEFAULT_STATE = path.join(ROOT, "games", "bramble-hollow", "assets", "director.conf");
const TEXT_FIELDS = [
  "long_theme", "long_church_goal", "medium_event", "medium_shop_special",
  "short_focus", "zebra_line", "turtle_line", "cat_line", "sheep_line", "nun_line"
];
const OUTPUT_FIELDS = [...TEXT_FIELDS, "weather", "growth_boost"];
const WEATHER = new Set(["sun", "rain", "mist", "wind"]);
const WEATHER_ORDER = ["sun", "wind", "rain", "mist"];
const HISTORY_LIMIT = 6;
const MAX_WEATHER_REVISIONS = 3;
const DEFAULT_GENERATION_INTERVAL_MS = 20000;
const COMPLETE_SENTENCE_PATTERN = ".*[.!?]$";

export const schema = {
  type: "object",
  properties: {
    long_theme: {type: "string", maxLength: 90, pattern: COMPLETE_SENTENCE_PATTERN},
    long_church_goal: {type: "string", maxLength: 90, pattern: COMPLETE_SENTENCE_PATTERN},
    medium_event: {type: "string", maxLength: 90, pattern: COMPLETE_SENTENCE_PATTERN},
    medium_shop_special: {type: "string", maxLength: 90, pattern: COMPLETE_SENTENCE_PATTERN},
    short_focus: {type: "string", maxLength: 90, pattern: COMPLETE_SENTENCE_PATTERN},
    weather: {type: "string", enum: [...WEATHER]},
    growth_boost: {type: "integer", enum: [1, 2, 3]},
    zebra_line: {type: "string", maxLength: 118, pattern: COMPLETE_SENTENCE_PATTERN},
    turtle_line: {type: "string", maxLength: 118, pattern: COMPLETE_SENTENCE_PATTERN},
    cat_line: {type: "string", maxLength: 118, pattern: COMPLETE_SENTENCE_PATTERN},
    sheep_line: {type: "string", maxLength: 118, pattern: COMPLETE_SENTENCE_PATTERN},
    nun_line: {type: "string", maxLength: 118, pattern: COMPLETE_SENTENCE_PATTERN}
  },
  required: OUTPUT_FIELDS,
  additionalProperties: false
};

function clean(value, limit) {
  return String(value).replace(/[\r\n=\x00-\x1f\x7f]/g, " ")
    .replace(/\s+/g, " ").trim().slice(0, limit);
}

export function parseState(text) {
  const values = Object.fromEntries(String(text).split(/\r?\n/).map(line => {
    const index = line.indexOf("=");
    return index > 0 ? [line.slice(0, index), line.slice(index + 1)] : null;
  }).filter(Boolean));
  const state = {};
  for (const field of TEXT_FIELDS) state[field] = clean(values[field] || "", field.endsWith("_line") ? 118 : 90);
  state.weather = WEATHER.has(values.weather) ? values.weather : "sun";
  state.growth_boost = [1, 2, 3].includes(Number(values.growth_boost)) ? Number(values.growth_boost) : 1;
  return {revision: Math.max(0, Number(values.revision) || 0), state};
}

function validateState(value) {
  if (!value || Array.isArray(value) || typeof value !== "object" ||
      Object.keys(value).length !== OUTPUT_FIELDS.length) throw new Error("The model returned an invalid state object.");
  const state = {};
  for (const field of TEXT_FIELDS) {
    const limit = field.endsWith("_line") ? 118 : 90;
    if (typeof value[field] !== "string" || !value[field].trim() || value[field].length > limit)
      throw new Error(`The model returned invalid ${field}.`);
    state[field] = clean(value[field], limit);
  }
  if (!WEATHER.has(value.weather)) throw new Error("The model returned invalid weather.");
  if (![1, 2, 3].includes(value.growth_boost)) throw new Error("The model returned invalid growth_boost.");
  state.weather = value.weather;
  state.growth_boost = value.growth_boost;
  return state;
}

function validateGeneratedState(value, previous, events) {
  const state = validateState(value);
  for (const field of TEXT_FIELDS) {
    if (!/[.!?]$/.test(state[field]))
      throw new Error(`The model returned incomplete ${field}.`);
  }
  if (events.length) {
    if (state.short_focus === previous.short_focus)
      throw new Error("The model did not update short_focus for recent events.");
    const changedDialogue = TEXT_FIELDS.slice(5)
      .filter(field => state[field] !== previous[field]).length;
    if (changedDialogue < 2)
      throw new Error("The model did not update at least two neighbour lines for recent events.");
  }
  return state;
}

export function serializeState(state, revision) {
  const lines = ["version=1", `revision=${revision}`];
  for (const field of TEXT_FIELDS.slice(0, 5)) lines.push(`${field}=${clean(state[field], 90)}`);
  lines.push(`weather=${state.weather}`, `growth_boost=${state.growth_boost}`);
  for (const field of TEXT_FIELDS.slice(5)) lines.push(`${field}=${clean(state[field], 118)}`);
  return lines.join("\n") + "\n";
}

function outputState(response) {
  if (response.status === "incomplete")
    throw new Error(`The model response was incomplete: ${response.incomplete_details?.reason || "unknown reason"}.`);
  const texts = [];
  for (const item of response.output || []) {
    if (item.type !== "message") continue;
    for (const content of item.content || []) {
      if (content.type === "refusal") throw new Error(`Model refusal: ${content.refusal}`);
      if (content.type === "output_text") texts.push(content.text);
    }
  }
  for (const text of [...texts, texts.join("")]) {
    try { return JSON.parse(text); }
    catch {}
  }
  throw new Error(`The response did not contain valid structured JSON (${texts.length} text blocks).`);
}

export function openAIGenerator({apiKey, model = "gpt-6-luna", fetchImpl = fetch} = {}) {
  if (!apiKey) return null;
  return async ({state, history = [], events}) => {
    let retryInstruction = "";
    for (let attempt = 0; attempt < 2; attempt++) {
      const response = await fetchImpl("https://api.openai.com/v1/responses", {
        method: "POST",
        headers: {Authorization: `Bearer ${apiKey}`, "Content-Type": "application/json"},
        body: JSON.stringify({
          model,
          reasoning: {effort: "none"},
          max_output_tokens: 4000,
          store: false,
          input: [
            {role: "developer", content: [{type: "input_text", text: [
              "You are the quiet world director for Bramble Hollow, a gentle woodland adventure.",
              "Maintain continuity across long-term village story, medium-term event and shop state, and immediate focus and dialogue.",
              "The player is a small brown bear. Neighbours are Zara the zebra shopkeeper, Moss the turtle gardener, Maple the cat baker, Woolsey the sheep librarian, and penguin nuns Sister Wren and Sister Pippa.",
              "Make each revision perceptibly responsive to recent events while preserving story continuity.",
              "Rewrite short_focus and at least two neighbour lines on every event-driven revision. A neighbour recently visited should respond specifically to what the player did, not merely repeat the standing village theme.",
              "Use recent_states to avoid repeating wording, conversational beats, and the same weather indefinitely. Weather may last for a few revisions, but should give way naturally and visibly.",
              "Long-term goals should evolve slowly. Medium-term events should progress after several relevant actions or a new day. Immediate focus and dialogue should change promptly.",
              "Keep every line warm, concise, suitable for all ages, and grounded in the supplied state.",
              "Every string must be a complete sentence. Use at most 10 words for story fields and 16 words for neighbour lines; never treat the character limit as a target or end mid-thought.",
              "Weather and growth are suggestions inside strict game-owned bounds. Never invent coordinates, inventory totals, controls, code, danger, combat, or irreversible consequences.",
              "Return only the object required by the response schema, with no explanation.",
              "Use plain ASCII with no line breaks or equals signs inside strings."
            ].join("\n")}]},
            {role: "user", content: [{type: "input_text", text: JSON.stringify({
              current: state, recent_states: history.slice(-4), recent_events: events,
              ...(retryInstruction ? {retry_instruction: retryInstruction} : {})
            })}]}
          ],
          text: {verbosity: "low",
            format: {type: "json_schema", name: "bramble_hollow_state", strict: true, schema}}
        })
      });
      if (!response.ok) throw new Error(`OpenAI API ${response.status}: ${await response.text()}`);
      try { return validateGeneratedState(outputState(await response.json()), state, events); }
      catch (error) {
        if (attempt) throw error;
        retryInstruction = `${error.message} Return a corrected object with complete sentences and visibly responsive immediate state.`;
      }
    }
  };
}

function validId(value, limit = 63) {
  return typeof value === "string" && value.length > 0 && value.length <= limit && /^[A-Za-z0-9._-]+$/.test(value);
}

function validEvent(value) {
  const event = value?.event;
  return Number.isSafeInteger(value?.sequence) && value.sequence > 0 && event &&
    !Array.isArray(event) && typeof event === "object" &&
    Number.isSafeInteger(event.tick) && event.tick >= 0 &&
    Number.isSafeInteger(event.day) && event.day >= 0 &&
    typeof event.kind === "string" && event.kind.length > 0 && event.kind.length <= 31 &&
    typeof event.detail === "string" && event.detail.length <= 127;
}

function readBody(request, limit = 65536) {
  return new Promise((resolve, reject) => {
    let size = 0, chunks = [];
    request.on("data", chunk => {
      size += chunk.length;
      if (size > limit) { reject(Object.assign(new Error("Request too large"), {status: 413})); request.destroy(); }
      else chunks.push(chunk);
    });
    request.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    request.on("error", reject);
  });
}

function json(response, status, value) {
  const body = JSON.stringify(value);
  response.writeHead(status, {"Content-Type": "application/json", "Content-Length": Buffer.byteLength(body),
    "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store"});
  response.end(body);
}

function worldFile(directory, world) { return path.join(directory, `${world}.json`); }

export function createDirectorServer({
  dataDirectory = path.join(ROOT, "director", "data"),
  defaultsPath = DEFAULT_STATE,
  generator = openAIGenerator({apiKey: process.env.OPENAI_API_KEY, model: process.env.BRAMBLE_MODEL || "gpt-6-luna"}),
  generationIntervalMs = Number(process.env.DIRECTOR_INTERVAL_MS) || DEFAULT_GENERATION_INTERVAL_MS,
  now = () => Date.now()
} = {}) {
  const defaults = parseState(fs.readFileSync(defaultsPath, "utf8"));
  const worlds = new Map();
  const generating = new Map();
  const generationTimers = new Map();
  fs.mkdirSync(dataDirectory, {recursive: true});

  function persist(world) {
    const file = worldFile(dataDirectory, world.id), temporary = `${file}.next`;
    fs.writeFileSync(temporary, JSON.stringify(world, null, 2) + "\n");
    try { fs.renameSync(temporary, file); }
    catch (error) {
      if (process.platform !== "win32") throw error;
      fs.rmSync(file, {force: true}); fs.renameSync(temporary, file);
    }
  }

  function getWorld(id) {
    if (worlds.has(id)) return worlds.get(id);
    const file = worldFile(dataDirectory, id);
    let world;
    try { world = JSON.parse(fs.readFileSync(file, "utf8")); }
    catch { world = null; }
    if (!world || world.version !== 1 || world.id !== id) {
      world = {version: 1, id, revision: defaults.revision, state: defaults.state,
        sessions: {}, events: [], next_event: 1, generation_cursor: 0,
        history: [], weather_revisions: 1, last_generated_at: 0, last_error: ""};
      persist(world);
    } else {
      if (!Array.isArray(world.history)) world.history = [];
      if (!Number.isSafeInteger(world.weather_revisions) || world.weather_revisions < 1)
        world.weather_revisions = MAX_WEATHER_REVISIONS;
    }
    worlds.set(id, world);
    return world;
  }

  async function generateWorld(id, force = false) {
    if (!generator) throw Object.assign(new Error("OPENAI_API_KEY is not configured."), {status: 503});
    if (generating.has(id)) return generating.get(id);
    const world = getWorld(id);
    if (!force && now() - world.last_generated_at < generationIntervalMs) return false;
    const pending = world.events.filter(event => event.ordinal > world.generation_cursor).slice(-24);
    if (!force && !pending.length) return false;
    world.last_generated_at = now(); persist(world);
    const task = (async () => {
      try {
        const previous = structuredClone(world.state);
        const recentStates = world.history.map(item => item.state);
        const next = validateState(await generator({state: previous, history: recentStates,
          events: pending.map(({event}) => event)}));
        let weatherRevisions = next.weather === previous.weather ? world.weather_revisions + 1 : 1;
        if (weatherRevisions > MAX_WEATHER_REVISIONS) {
          const index = WEATHER_ORDER.indexOf(previous.weather);
          next.weather = WEATHER_ORDER[(index + 1) % WEATHER_ORDER.length];
          weatherRevisions = 1;
        }
        world.history.push({revision: world.revision, generated_at: world.last_generated_at,
          state: previous});
        if (world.history.length > HISTORY_LIMIT) world.history.splice(0, world.history.length - HISTORY_LIMIT);
        world.state = next;
        world.weather_revisions = weatherRevisions;
        world.revision = Math.max(1, world.revision + 1);
        if (pending.length) world.generation_cursor = pending[pending.length - 1].ordinal;
        world.last_error = "";
        persist(world);
        return true;
      } catch (error) {
        world.last_error = error.message;
        persist(world);
        throw error;
      } finally { generating.delete(id); }
    })();
    generating.set(id, task);
    return task;
  }

  function scheduleGeneration(id) {
    if (!generator || generationTimers.has(id)) return;
    const world = getWorld(id);
    const delay = Math.max(0, generationIntervalMs - (now() - world.last_generated_at));
    if (!delay) {
      generateWorld(id).catch(error => console.error(`Director generation failed for ${id}: ${error.message}`));
      return;
    }
    const timer = setTimeout(() => {
      generationTimers.delete(id);
      // Timers can wake before the wall-clock cooldown has elapsed. Recheck
      // and rearm so an early wakeup does not strand the pending events.
      scheduleGeneration(id);
    }, delay);
    timer.unref?.();
    generationTimers.set(id, timer);
  }

  async function handleSync(request, response) {
    const value = JSON.parse(await readBody(request));
    if (value.protocol !== 1 || value.game !== "bramble-hollow" || !validId(value.world) ||
        !validId(value.session, 95) || !Number.isSafeInteger(value.last_revision) || value.last_revision < 0 ||
        !Array.isArray(value.events) || value.events.length > 32 || !value.events.every(validEvent))
      throw Object.assign(new Error("Invalid sync request."), {status: 400});
    const world = getWorld(value.world);
    let acknowledged = Number(world.sessions[value.session]) || 0;
    let accepted = false, controlledWeather = null;
    for (const item of [...value.events].sort((a, b) => a.sequence - b.sequence)) {
      if (item.sequence <= acknowledged) continue;
      if (item.sequence !== acknowledged + 1)
        throw Object.assign(new Error(`Expected event sequence ${acknowledged + 1}.`), {status: 409});
      world.events.push({ordinal: world.next_event++, session: value.session,
        sequence: item.sequence, received_at: now(), event: item.event});
      if (item.event.kind === "world_control" && WEATHER.has(item.event.detail))
        controlledWeather = item.event.detail;
      acknowledged = item.sequence; accepted = true;
    }
    world.sessions[value.session] = acknowledged;
    if (world.events.length > 256) world.events.splice(0, world.events.length - 256);
    if (accepted) {
      if (controlledWeather && controlledWeather !== world.state.weather) {
        world.history.push({revision: world.revision, generated_at: now(), state: structuredClone(world.state)});
        if (world.history.length > HISTORY_LIMIT) world.history.splice(0, world.history.length - HISTORY_LIMIT);
        world.state.weather = controlledWeather;
        world.weather_revisions = 1;
        world.revision = Math.max(1, world.revision + 1);
      }
      persist(world);
      scheduleGeneration(world.id);
    }
    const body = serializeState(world.state, world.revision);
    response.writeHead(200, {"Content-Type": "text/plain; charset=utf-8", "Content-Length": Buffer.byteLength(body),
      "X-Chirky-Ack": String(acknowledged), "X-Chirky-Revision": String(world.revision),
      "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store"});
    response.end(body);
  }

  const server = http.createServer(async (request, response) => {
    response.setHeader("Access-Control-Allow-Origin", "*");
    response.setHeader("Access-Control-Allow-Headers", "Content-Type");
    response.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    if (request.method === "OPTIONS") { response.writeHead(204); response.end(); return; }
    try {
      const url = new URL(request.url, "http://director.local");
      if (request.method === "GET" && url.pathname === "/health") {
        json(response, 200, {ok: true, model_configured: Boolean(generator), worlds: worlds.size}); return;
      }
      if (request.method === "POST" && url.pathname === "/v1/sync") { await handleSync(request, response); return; }
      const match = /^\/v1\/worlds\/([A-Za-z0-9._-]+)(\/generate)?$/.exec(url.pathname);
      if (match && validId(match[1])) {
        const world = getWorld(match[1]);
        if (request.method === "GET" && !match[2]) {
          json(response, 200, {id: world.id, revision: world.revision, state: world.state,
            clients: Object.keys(world.sessions).length,
            pending_events: world.events.filter(event => event.ordinal > world.generation_cursor).length,
            weather_revisions: world.weather_revisions, recent_revisions: world.history.length,
            last_generated_at: world.last_generated_at, last_error: world.last_error}); return;
        }
        if (request.method === "POST" && match[2]) {
          await generateWorld(world.id, true);
          json(response, 200, {ok: true, revision: world.revision, state: world.state}); return;
        }
      }
      json(response, 404, {error: "Not found."});
    } catch (error) {
      json(response, error.status || 500, {error: error.message || "Director error."});
    }
  });

  return {server, getWorld, generateWorld,
    listen: (port = 3040, host = "0.0.0.0") => new Promise((resolve, reject) => {
      server.once("error", reject); server.listen(port, host, () => { server.off("error", reject); resolve(server.address()); });
    }),
    close: () => {
      for (const timer of generationTimers.values()) clearTimeout(timer);
      generationTimers.clear();
      return new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    }};
}

async function main() {
  const port = Number(process.env.DIRECTOR_PORT) || 3040;
  const host = process.env.DIRECTOR_HOST || "0.0.0.0";
  const model = process.env.BRAMBLE_MODEL || "gpt-6-luna";
  const app = createDirectorServer();
  await app.listen(port, host);
  console.log(`Bramble director listening on http://${host}:${port} using ${model}.`);
  if (!process.env.OPENAI_API_KEY) console.log("OPENAI_API_KEY is absent; networking works but generation is disabled.");
  const stop = async () => { await app.close(); process.exit(0); };
  process.once("SIGINT", stop); process.once("SIGTERM", stop);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { console.error(error); process.exitCode = 1; });
