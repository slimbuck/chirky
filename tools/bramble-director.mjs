import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const GAME = path.join(ROOT, "games", "bramble-hollow");
const DEFAULT_STATE = path.join(GAME, "assets", "director.conf");
const LIVE_STATE = path.join(GAME, "runtime", "director.conf");
const EVENTS = path.join(GAME, "runtime", "events.log");
const MODEL = process.env.BRAMBLE_MODEL || "gpt-6-luna";
const WATCH = process.argv.includes("--watch");

const fields = [
  "long_theme", "long_church_goal", "medium_event", "medium_shop_special",
  "short_focus", "zebra_line", "turtle_line", "cat_line", "sheep_line", "nun_line"
];

const schema = {
  type: "object",
  properties: {
    long_theme: { type: "string", maxLength: 90 },
    long_church_goal: { type: "string", maxLength: 90 },
    medium_event: { type: "string", maxLength: 90 },
    medium_shop_special: { type: "string", maxLength: 90 },
    short_focus: { type: "string", maxLength: 90 },
    weather: { type: "string", enum: ["sun", "rain", "mist", "wind"] },
    growth_boost: { type: "integer", enum: [1, 2, 3] },
    zebra_line: { type: "string", maxLength: 118 },
    turtle_line: { type: "string", maxLength: 118 },
    cat_line: { type: "string", maxLength: 118 },
    sheep_line: { type: "string", maxLength: 118 },
    nun_line: { type: "string", maxLength: 118 }
  },
  required: [...fields, "weather", "growth_boost"],
  additionalProperties: false
};

function parseState(text) {
  return Object.fromEntries(text.split(/\r?\n/).map(line => {
    const index = line.indexOf("=");
    return index > 0 ? [line.slice(0, index), line.slice(index + 1)] : null;
  }).filter(Boolean));
}

function clean(value, limit) {
  return String(value).replace(/[\r\n=,:;'\"<>\x00-\x1f\x7f]/g, " ").replace(/\s+/g, " ").trim().slice(0, limit);
}

function recentEvents() {
  if (!fs.existsSync(EVENTS)) return [];
  return fs.readFileSync(EVENTS, "utf8").split(/\r?\n/).filter(Boolean).slice(-24).map(line => {
    try { return JSON.parse(line); } catch { return null; }
  }).filter(Boolean);
}

function outputText(response) {
  for (const item of response.output || []) {
    if (item.type !== "message") continue;
    for (const content of item.content || []) if (content.type === "output_text") return content.text;
  }
  throw new Error("The response did not contain structured output text.");
}

function serialize(state) {
  const lines = ["version=1", `revision=${Date.now()}`];
  for (const field of fields) lines.push(`${field}=${clean(state[field], field.endsWith("_line") ? 118 : 90)}`);
  lines.splice(7, 0, `weather=${state.weather}`, `growth_boost=${state.growth_boost}`);
  return lines.join("\n") + "\n";
}

function writeAtomically(text) {
  fs.mkdirSync(path.dirname(LIVE_STATE), { recursive: true });
  const temporary = LIVE_STATE + ".next";
  fs.writeFileSync(temporary, text);
  try { fs.renameSync(temporary, LIVE_STATE); }
  catch (error) {
    if (process.platform !== "win32") throw error;
    fs.rmSync(LIVE_STATE, { force: true });
    fs.renameSync(temporary, LIVE_STATE);
  }
}

async function direct() {
  if (!process.env.OPENAI_API_KEY) throw new Error("Set OPENAI_API_KEY before running the Bramble Hollow director.");
  const source = fs.existsSync(LIVE_STATE) ? LIVE_STATE : DEFAULT_STATE;
  const current = parseState(fs.readFileSync(source, "utf8"));
  const events = recentEvents();
  const response = await fetch("https://api.openai.com/v1/responses", {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${process.env.OPENAI_API_KEY}`,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      model: MODEL,
      max_output_tokens: 700,
      input: [
        {
          role: "developer",
          content: [{ type: "input_text", text: [
            "You are the quiet world director for Bramble Hollow, a gentle woodland adventure.",
            "Maintain continuity across three horizons: long-term village story, medium-term event/shop state, and immediate focus/dialogue.",
            "The player is a small brown bear. Neighbours are Zara the zebra shopkeeper, Moss the turtle gardener, Maple the cat baker, Woolsey the sheep librarian, and penguin nuns Sister Wren and Sister Pippa.",
            "Keep every line warm, specific, concise, and suitable for all ages. Change things gradually from recent events.",
            "Weather and growth are suggestions inside strict game-owned bounds. Never invent coordinates, inventory totals, controls, code, danger, combat, or irreversible consequences.",
            "Use plain ASCII with no line breaks or equals signs in strings."
          ].join("\n") }]
        },
        {
          role: "user",
          content: [{ type: "input_text", text: JSON.stringify({ current, recent_events: events }) }]
        }
      ],
      text: { format: { type: "json_schema", name: "bramble_hollow_state", strict: true, schema } }
    })
  });
  if (!response.ok) throw new Error(`OpenAI API ${response.status}: ${await response.text()}`);
  const state = JSON.parse(outputText(await response.json()));
  writeAtomically(serialize(state));
  console.log(`Bramble Hollow director wrote revision ${Date.now()} using ${MODEL}.`);
}

async function run() {
  try { await direct(); }
  catch (error) { console.error(error.message); if (!WATCH) process.exitCode = 1; }
}

await run();
if (WATCH) setInterval(run, 60_000);
