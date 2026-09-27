"use strict";

const fs = require("node:fs");
const path = require("node:path");
const ROOT = path.resolve(__dirname, "..");
const ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function parseManifest(text) {
  const values = {};
  for (const line of text.split(/\r?\n/)) {
    const clean = line.trim();
    if (!clean || clean.startsWith("#") || clean.startsWith(";")) continue;
    const separator = clean.indexOf("=");
    if (separator > 0)
      values[clean.slice(0, separator).trim()] = clean.slice(separator + 1).trim();
  }
  return values;
}

function gameCatalog(root = ROOT) {
  const directory = path.join(root, "games");
  const games = fs.readdirSync(directory, { withFileTypes: true })
    .filter(entry => entry.isDirectory())
    .map(entry => {
      const manifest = path.join(directory, entry.name, "game.conf");
      if (!fs.existsSync(path.join(directory, entry.name, "game.c")) ||
          !fs.existsSync(manifest)) return null;
      const values = parseManifest(fs.readFileSync(manifest, "utf8"));
      if (!ID.test(values.id || "") || values.id !== entry.name)
        throw new Error(`Game id must match its directory: ${entry.name}`);
      if (!values.name || !values.description || !values.module)
        throw new Error(`Incomplete game manifest: ${entry.name}`);
      const role = values.role || "game";
      if (!new Set(["game", "diagnostic"]).has(role))
        throw new Error(`Unknown game role for ${entry.name}: ${role}`);
      const levelSetting = values.browser_level_setting || "";
      if (levelSetting && !/^[a-z][a-z0-9_]*$/.test(levelSetting))
        throw new Error(`Invalid browser level setting for ${entry.name}`);
      return { id: values.id, name: values.name, description: values.description, role,
        ...(levelSetting ? { levelSetting } : {}) };
    })
    .filter(Boolean);
  games.sort((left, right) =>
    (left.role === "diagnostic") - (right.role === "diagnostic") ||
    left.name.localeCompare(right.name));
  if (new Set(games.map(game => game.id)).size !== games.length)
    throw new Error("Game ids must be unique");
  return { version: 1, games };
}

module.exports = { ROOT, gameCatalog, parseManifest };
