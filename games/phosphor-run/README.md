# Phosphor Run

A 320×240, 60 Hz platformer for the Chirky Pi and browser hosts.

## Playing

- D-pad: move
- Primary: jump; wall-jump while touching a wall; begin, advance, or replay
- Secondary: air dash
- Start: sacrifice a life and respawn at the current checkpoint
- Menu: pause; choose Continue Game or Return to Launcher

The game reads logical Chirky buttons. Keyboard and controller mappings live in
the shared Input Settings. Default browser keys are arrows, X for Primary,
Z for Secondary, Enter for Start and Escape for Menu.

Collect every shard in the current level to unlock its portal. A run starts with
three lives and ends when all three are lost. Lives carry between campaign levels.
Each level starts with fresh shards, checkpoint, dash, particles, camera state and
a 60 Hz timer. Its opening card shows the articulated player at 2× size running
in place before control begins. A qualifying completion enters the top ten for
that level. Use Up/Down to edit an initial, Primary (touch A) to advance and
submit the final letter, and Secondary (touch B) to go back for corrections.
Left/Right are ignored so sliding the touch D-pad cannot change the active
character. Letter edits play a short tick; advancing, going back and submitting
play a two-note confirmation. The active letter blinks and editing restarts its blink.
Scores save immediately through the host's persistent storage and survive game
relaunches, browser reloads and Pi restarts. Each level is keyed by its catalog ID
so reordering levels preserves its scores. The browser uses localStorage; Pi
records live under `saves/phosphor-run/`. These are device-local, not shared online.
Unavailable storage is reported as `SCORE NOT SAVED`; malformed records are ignored.
Scenery sprites use stable world-position phase offsets while retaining their
authored animation speeds. The host reserves a CRT-safe border; UI and cameras use the
remaining logical viewport. The camera follows both axes
for wider or taller levels. `start_level` in `game.conf` selects the zero-based
campaign starting position; the default is zero.

## Editing

Use **Edit levels** or **Edit sprites & animations** in the dashboard. The asset
selector lists every campaign level and named animation. **Duplicate as new asset**
creates a new file and registers it in the catalog; save your edits first. New
levels join the end of the campaign. Use **Earlier/Later in campaign** to reorder.

Every sprite element is editable: player idle/run/jump/fall/dash/death, platforms,
hazards, shards, checkpoint states, portal states, dash trail, particles, machinery,
lamps, and background sparks. Frame controls add, duplicate, delete and reorder
frames, adjust timing, and play the animation preview. Undo/redo includes all
frames and timing. Resize applies to the whole animation. Particle art is a mask
that gameplay tints with the effect colour. Sprite sizes change the artwork, not
the player's 12×14 collision box or the world's 8×8 collision tiles.

**Save locally** writes the asset with validation and stale-file detection.
**Save and play on Pi** saves locally, copies the game package and Makefile, builds
the game module if needed, and launches it. For a level, it sets that level as the
remote playtest start without changing local `game.conf`. A later full deploy
restores local campaign settings. A failed upload/build reports that the local
save succeeded, so retrying does not lose edits or cause a stale-hash conflict.
Restart an already-running dashboard after updating its server code.

## Data layout and format

- `game.conf`: movement, sounds, palette, catalog location and starting level.
- `content.conf`: ordered levels and named animations, shared by runtime and editor.
- `editor.json`: reusable level/sprite palette templates and validation constraints.
- `assets/levels/`: all campaign level grids.
- `assets/sprites/`: all named sprite and animation files.
- `assets/*.wav`: sounds; `assets/concept.png`: visual reference.

Regenerate the deterministic PCM sound effects with
`python3 tools/generate_phosphor_assets.py` from the repository root. This includes
the quiet 45 ms letter tick and 100 ms two-note selection sound, played through
the same host audio service on Pi and in the browser.

Catalog example (paths are relative to `content.conf`):

```ini
level.relay-shaft=assets/levels/relay-shaft.txt
level.signal-bridge=assets/levels/signal-bridge.txt
sprite.player-idle=assets/sprites/player-idle.sprite
sprite.player-run=assets/sprites/player-run.sprite
```

IDs use lowercase letters, digits and hyphens, up to 63 characters. Paths must
remain within the game folder. The catalog supports up to 256 levels and 256
animations. Level ordering is file order, independent of sprite entries. Adding
an animation to the catalog automatically exposes it in the editor; gameplay must
select its ID to display it. Unknown new animation IDs need no loader changes.

Level grids use `. # ^ o C S E` for empty, solid, hazard, shard, checkpoint, spawn,
and exit. They must be rectangular, at most 512×512 tiles, with exactly one spawn,
one exit and at least one solid tile. All grids are top-to-bottom in the files;
world coordinates increase upwards. Blank lines and `# ` comment lines are ignored.
Validation checks structure, not whether a player can complete the layout.

Animation files use one character per native pixel. A single frame is compatible
with the original sprite format. Separate frames with a line containing `---`:

```text
# ticks=6
.c.
cwc
---
.w.
wcw
```

All frames have identical dimensions, at most 128×128, with up to 64 frames.
`# ticks=N` sets the duration of every frame in 60 Hz ticks (1–600, default 6).
Animations loop. Palette keys are `. n s c a w r g`: transparent, navy, steel,
cyan, amber, white, coral and phosphor. Colours are resolved from `game.conf` by
both the renderer and dashboard.

## Runtime structure and extension points

- `game.c`: lifecycle, campaign progression, player movement/collision, interactions,
  checkpoints, particles and fixed-tick updates. This module owns mutable state.
- `game_state.h`: internal state/types shared by the game modules; the host ABI
  remains in `include/chirky.h`.
- `settings.c`: defaults and configuration parsing.
- `scores.c`: versioned per-level score records, validation and host persistence.
- `assets.h` / `assets.c`: catalog, strict grid/animation loaders, frame sampling
  and memory ownership. Independent of input, gameplay and graphics APIs.
- `render.c`: read-only drawing passes for background, world, particles, player
  and HUD. Repeated rows and colour runs are merged into rectangles for the Pi's
  batched rectangle renderer.

Keep future enemies/projectiles in their own modules with explicit entity state
and update functions called by `game.c`. Resolve their animation IDs through the
catalog. New tile behaviors require a matching palette entry, loader alphabet,
and gameplay/render handling; arbitrary art does not silently add collision rules.
Lighting and post effects belong in rendering passes, with graphics API additions
made deliberately at the host boundary. Do not encode future game rules in the
editor or animation loader. The Makefile compiles every `.c` in each game's folder
and rebuilds when its headers change.

The dashboard separates HTTP/deployment (`server.js`), catalog and validation
(`editors.js`), general controls (`public/app.js`) and asset interaction/history/
preview (`public/asset-editor.js`). Existing explicit `editor.json` registrations
for other games remain supported.

## Verification

From the repository root on Linux/WSL: `make test`. The tests cover shipped data,
malformed animations/catalogs, duplication, ordering, campaign transitions,
replay, selected start, animation timing and cleanup. On Windows, run the dashboard
suite with `npm test --prefix dashboard`. Runtime tests use a stub host and do not
require the CRT or change the running Pi session.

## Title artwork

`assets/artwork/splash.png` is the editable 288x216 image used by the dashboard;
`splash.ppm` is its runtime export. See [artwork exports and generation prompt](../ARTWORK.md).
