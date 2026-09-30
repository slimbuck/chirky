import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import {createDirectorServer, openAIGenerator, parseState} from "../director/server.mjs";

const defaultsPath = path.resolve("games/bramble-hollow/assets/director.conf");

test("director state preserves safe conversational punctuation", () => {
  const parsed = parseState("revision=1\ncat_line=Maple's buns, warm today!\n");
  assert.equal(parsed.state.cat_line, "Maple's buns, warm today!");
});

test("OpenAI generator selects the structured block from a multi-item response", async () => {
  const expected = {...parseState(fs.readFileSync(defaultsPath, "utf8")).state,
    medium_event: "A lantern picnic is beginning beside the bridge."};
  const fetchImpl = async (_url, options) => {
    const request = JSON.parse(options.body);
    assert.equal(request.text.format.type, "json_schema");
    assert.equal(request.text.verbosity, "low");
    assert.equal(request.max_output_tokens, 4000);
    return new Response(JSON.stringify({status: "completed", output: [
      {type: "message", content: [{type: "output_text", text: "Preparing the requested state."}]},
      {type: "message", content: [{type: "output_text", text: JSON.stringify(expected)}]}
    ]}), {status: 200, headers: {"Content-Type": "application/json"}});
  };
  const generate = openAIGenerator({apiKey: "test-key", fetchImpl});
  assert.deepEqual(await generate({state: expected, events: []}), expected);
});

test("OpenAI generator retries static or incomplete event responses", async () => {
  const current = parseState(fs.readFileSync(defaultsPath, "utf8")).state;
  const corrected = {...current,
    short_focus: "Bring Maple a flower for her market table.",
    cat_line: "A flower would make my market table especially cheerful.",
    sheep_line: "I read that market flowers once inspired the window makers."};
  let calls = 0;
  const fetchImpl = async (_url, options) => {
    const request = JSON.parse(options.body);
    if (++calls === 2) {
      const user = JSON.parse(request.input[1].content[0].text);
      assert.match(user.retry_instruction, /short_focus/);
    }
    return new Response(JSON.stringify({status: "completed", output: [
      {type: "message", content: [{type: "output_text", text: JSON.stringify(calls === 1 ? current : corrected)}]}
    ]}), {status: 200, headers: {"Content-Type": "application/json"}});
  };
  const generate = openAIGenerator({apiKey: "test-key", fetchImpl});
  assert.deepEqual(await generate({state: current, events: [{kind: "talk", detail: "Maple"}]}), corrected);
  assert.equal(calls, 2);
});

test("director sync deduplicates events and publishes generated state", async t => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-director-"));
  t.after(() => fs.rmSync(dataDirectory, {recursive: true, force: true}));
  let calls = 0;
  const generator = async ({state, events}) => {
    calls++;
    if (calls === 1) {
      assert.equal(state.weather, "sun");
      assert.deepEqual(events.map(event => event.detail), ["Maple"]);
    }
    return {...state, medium_event: "Maple is preparing moonberry buns by the bridge.",
      short_focus: "Visit Maple after tending one garden bed.", weather: "rain", growth_boost: 2};
  };
  const app = createDirectorServer({dataDirectory, defaultsPath, generator, generationIntervalMs: 0});
  const address = await app.listen(0, "127.0.0.1");
  t.after(() => app.close());
  const base = `http://127.0.0.1:${address.port}`;
  const payload = {protocol: 1, game: "bramble-hollow", world: "test-world", session: "browser-test",
    last_revision: 0, events: [{sequence: 1, event: {tick: 10, day: 1, kind: "talk", detail: "Maple"}}]};
  let response = await fetch(`${base}/v1/sync`, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(payload)});
  assert.equal(response.status, 200);
  assert.equal(response.headers.get("x-chirky-ack"), "1");
  assert.match(await response.text(), /revision=1/);

  for (let tries = 0; tries < 100; tries++) {
    const world = await (await fetch(`${base}/v1/worlds/test-world`)).json();
    if (world.revision === 2) break;
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  response = await fetch(`${base}/v1/sync`, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(payload)});
  const state = await response.text();
  assert.equal(response.headers.get("x-chirky-ack"), "1");
  assert.equal(response.headers.get("x-chirky-revision"), "2");
  assert.match(state, /weather=rain/);
  assert.match(state, /short_focus=Visit Maple/);
  assert.equal(calls, 1);
  const status = await (await fetch(`${base}/v1/worlds/test-world`)).json();
  assert.equal(status.clients, 1);

  response = await fetch(`${base}/v1/worlds/test-world/generate`, {method: "POST"});
  assert.equal(response.status, 200);
  assert.equal((await response.json()).revision, 3);
  assert.equal(calls, 2);
});

test("director schedules cooldown events and supplies recent state history", async t => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-director-schedule-"));
  t.after(() => fs.rmSync(dataDirectory, {recursive: true, force: true}));
  const calls = [];
  const generator = async request => {
    calls.push(structuredClone(request));
    return {...request.state,
      short_focus: `A fresh village moment number ${calls.length}.`,
      zebra_line: `Zara noticed village moment ${calls.length}.`,
      turtle_line: `Moss noticed village moment ${calls.length}.`};
  };
  const app = createDirectorServer({dataDirectory, defaultsPath, generator, generationIntervalMs: 40});
  const address = await app.listen(0, "127.0.0.1");
  t.after(() => app.close());
  const base = `http://127.0.0.1:${address.port}`;
  const send = sequence => fetch(`${base}/v1/sync`, {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({protocol: 1, game: "bramble-hollow", world: "scheduled-world",
      session: "schedule-test", last_revision: 0,
      events: [{sequence, event: {tick: sequence, day: 1, kind: "talk", detail: sequence === 1 ? "Maple" : "Moss"}}]})});
  assert.equal((await send(1)).status, 200);
  for (let tries = 0; tries < 100 && calls.length < 1; tries++) await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(calls.length, 1);
  assert.equal((await send(2)).status, 200);
  for (let tries = 0; tries < 100 && calls.length < 2; tries++) await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(calls.length, 2);
  assert.deepEqual(calls[1].events.map(event => event.detail), ["Moss"]);
  assert.equal(calls[1].history.length, 1);
  const status = await (await fetch(`${base}/v1/worlds/scheduled-world`)).json();
  assert.equal(status.pending_events, 0);
  assert.equal(status.recent_revisions, 2);
});

test("an early cooldown timer keeps pending events scheduled", async t => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-director-early-"));
  t.after(() => fs.rmSync(dataDirectory, {recursive: true, force: true}));
  let clock = 1000, calls = 0;
  const app = createDirectorServer({dataDirectory, defaultsPath, generationIntervalMs: 40,
    now: () => clock, generator: async ({state}) => {calls++; return {...state};}});
  const address = await app.listen(0, "127.0.0.1");
  t.after(() => app.close());
  const send = sequence => fetch(`http://127.0.0.1:${address.port}/v1/sync`, {
    method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({protocol: 1, game: "bramble-hollow", world: "early-world",
      session: "early-test", last_revision: 0,
      events: [{sequence, event: {tick: sequence, day: 1, kind: "talk", detail: "Maple"}}]})});
  assert.equal((await send(1)).status, 200);
  assert.equal(calls, 1);
  assert.equal((await send(2)).status, 200);
  // Let the real timer fire while the injected cooldown clock is still early.
  await new Promise(resolve => setTimeout(resolve, 80));
  assert.equal(calls, 1, "An early wakeup must not bypass the cooldown");
  clock += 40;
  for (let tries = 0; tries < 100 && calls < 2; tries++) await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(calls, 2, "The pending event must run without another sync request");
  assert.equal(app.getWorld("early-world").generation_cursor, 2);
});

test("weather controls publish immediately and repeated generated weather advances", async t => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-director-weather-"));
  t.after(() => fs.rmSync(dataDirectory, {recursive: true, force: true}));
  const generator = async ({state}) => ({...state});
  const app = createDirectorServer({dataDirectory, defaultsPath, generator, generationIntervalMs: 0});
  const address = await app.listen(0, "127.0.0.1");
  t.after(() => app.close());
  const base = `http://127.0.0.1:${address.port}`;
  const response = await fetch(`${base}/v1/sync`, {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({protocol: 1, game: "bramble-hollow", world: "weather-world",
      session: "weather-test", last_revision: 0,
      events: [{sequence: 1, event: {tick: 1, day: 1, kind: "world_control", detail: "mist"}}]})});
  assert.equal(response.status, 200);
  assert.match(await response.text(), /weather=mist/);
  for (let tries = 0; tries < 100 && app.getWorld("weather-world").revision < 3; tries++)
    await new Promise(resolve => setTimeout(resolve, 5));
  assert.equal(app.getWorld("weather-world").revision, 3);
  await app.generateWorld("weather-world", true);
  await app.generateWorld("weather-world", true);
  assert.equal(app.getWorld("weather-world").state.weather, "sun");
  assert.equal(app.getWorld("weather-world").weather_revisions, 1);
});

test("director rejects malformed and out-of-sequence events", async t => {
  const dataDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "chirky-director-invalid-"));
  t.after(() => fs.rmSync(dataDirectory, {recursive: true, force: true}));
  const app = createDirectorServer({dataDirectory, defaultsPath, generator: null});
  const address = await app.listen(0, "127.0.0.1");
  t.after(() => app.close());
  const base = `http://127.0.0.1:${address.port}`;
  const invalid = {protocol: 1, game: "bramble-hollow", world: "test", session: "test",
    last_revision: 0, events: [{sequence: 2, event: {tick: 1, day: 1, kind: "talk", detail: "Maple"}}]};
  let response = await fetch(`${base}/v1/sync`, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(invalid)});
  assert.equal(response.status, 409);
  response = await fetch(`${base}/v1/worlds/test/generate`, {method: "POST"});
  assert.equal(response.status, 503);
  assert.equal((await response.json()).error, "OPENAI_API_KEY is not configured.");
});
