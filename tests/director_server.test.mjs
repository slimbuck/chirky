import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import {createDirectorServer} from "../director/server.mjs";

const defaultsPath = path.resolve("games/bramble-hollow/assets/director.conf");

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
