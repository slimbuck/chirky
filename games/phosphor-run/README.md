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
a 60 Hz timer. Its opening camera holds a 4× view of the actual robot in the level for
1.5 seconds, then zooms the whole scene out over two-thirds of a second with a
cubic ease-in/out: gentle departure, a brisk middle, and a soft arrival before
control and the run timer begin. The HUD stays at native size. A qualifying completion enters the top ten for
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
  and HUD. Scenery uses load-time texture atlases on both hosts; the original
  merged-rectangle path remains a headless fallback and pixel-comparison oracle.

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

## Robot studio and GPU model

Run `make robot-preview` (`wsl make robot-preview NODE=node.exe` on Windows),
restart the dashboard after server changes, and open **Edit → Phosphor Run →
Robot studio**. Choose any of the six clips, face either direction, pause, step,
scrub, slow playback, or change robot size from 1× to 10×. This is the same C
pose evaluator and GPU renderer used in play, with an independent preview camera.
Jump poses stay camera-centred for comparison; gameplay collision is unchanged.

The **Independent face** panel defaults to Automatic idle moods, with manual
Neutral, Happy, Curious or Sleepy overrides. Automatic mode occasionally picks
a happy or curious expression (and a rarer sleepy look) while idle, holds it for
2–4 seconds, then rests at neutral for 3–6 seconds. Transitions ease smoothly;
movement returns the automatic expression to neutral without resetting gaze or
blinking. Cosmetic choices use a private random stream, independent of gameplay.
The panel also triggers
a blink, pauses the face clock or steps it one tick. The face keeps advancing
when movement is paused, restarted, scrubbed or changed. The playback speed
applies to both clocks; their pause and step controls remain independent.

Lighting, brightness, jump head lead/body delay, idle glance and blinking update
live. **Save to game** writes `assets/models/robot.conf` with validation and stale
file protection. Refresh the browser player to load saved settings, or deploy
the project to use them on Pi. **Reset changes** restores saved values. **Reload
model** rereads an exported `player.robot` without rebuilding the viewer.

The robot's authored source is `assets/models/phosphor-robot.blend`. Export with:

```sh
blender --background games/phosphor-run/assets/models/phosphor-robot.blend --python tools/blender/export_phosphor_robot.py
```

`model-info.json` describes the PRB2 mesh, six clips, rigid bone order and face
basis. The selected Pocket CRT has A's small rolling body, rounded cabinet,
convex glass, large capsule eyes and a tiny smile. It has no brow or jaw vent.
The body radius and head/neck pivots are exported from the adapted rig.
Idle uses a subtle 3.2-second breathing pose with no authored side-to-side sway.
The independent face clock supplies occasional eye glances and blinks; the head
gently follows the eyes during idle, with neutral rests between glances. The
level-start close-up uses this same idle animation.
Jump extends the head first and offsets the rolling body briefly
behind it; simulation/collision and jump responsiveness stay unchanged.
On falling, the body leads instead: the head briefly hangs back, its downward
pitch follows four ticks later, and the neck suspension settles during descent.

The CPU interpolates 3 rigid bones and transforms 1,230 vertices; 2,228 triangles
are submitted in one mesh call. The GPU handles lighting, depth and coverage at
the requested camera size. It never enlarges a tiny software-rendered robot image.
The collision box is unchanged. Pixel scenery
uses rounded shared edges during the temporary zoom and returns to 1:1 pixels.

Validation: `make test`, `make web`, and `sh tools/texture-tests.sh` (GLES headers
required for the last command). The mesh tests cover deterministic poses, all
clips/facings, blink, magnification and bounded submission; GLES checks cover
depth, safe viewport clipping, 2D ordering and state restoration. Visual review
must include the large preview and the live 1× game, not just mesh structure.

`make robot-review` builds an offscreen review tool and writes
`build/robot-gpu-review.ppm`: six clips, four poses, both facings, 1× and 4×.
Rows also cover neutral eyes, a closed blink, Happy and Curious expressions.
It uses the real GLES renderer and also reports a bounded timing measurement
including GPU completion; this is an offscreen workload, not a live FPS claim.
The reviewed sheet is retained as `tests/references/phosphor-robot-gpu.png`.
Use the sheet together with the Blender portrait and live studio when changing
geometry, materials, or motion. Different GPUs may differ at triangle edges;
reference review must distinguish those edges from changes to identity or pose.

### Face animation and asset contract

`robot_face` owns a fixed-tick clock, eased gaze, blink envelope and expression
blend. `game_update` advances it once per simulation tick; pause freezes it.
Movement clip changes and restarts never reset it, and rendering is read-only.
Natural blinking has unequal gaps and an occasional double blink. Expressions
and gaze blend independently of the body's six animation clips.

The face uses a head-local origin and orthonormal U (right), V (up), and normal
(outward) vectors. Eye vertices carry explicit left/right tags; the smile has
its own tag, so blinking cannot flatten the mouth. The evaluator applies gaze,
expression and eyelid opening in UV, projects back onto the curved CRT surface,
then applies the head bone and suspension. All triangles still share one GPU
mesh submission. The mesh service is unchanged; the current host ABI is 15.

PRB2 is little-endian: `PRB2`, six uint32 counts (vertices, triangles, bones,
materials, clips, frames), then 21 float32 rig values: body radius, head pivot Z,
neck minimum/maximum Z, origin XYZ, U XYZ, V XYZ, normal XYZ, screen half width,
half height, bulge, eye half-spacing and eye centre V. Palette entries are four
bytes (RGB/emissive); each vertex is XYZ float32 plus bone and part uint32.
Part IDs are 0 rigid, 1 left eye, 2 right eye, 3 mouth, 4 neck. Triangles remain
four uint16 (three indices/material), clips four uint32, poses 3×4 float32 bone
matrices. The loader validates dimensions, basis, tags and bounds before use;
historical PRB1 studies remain readable.

To verify source reproducibility, append `-- --output-root build/robot-export-check`
to the normal Blender export command and byte-compare that directory's
`games/phosphor-run/assets/models/{player.robot,model-info.json}` with the shipped
files. Never patch the binary. `tests/robot_asset.test.cjs` checks the format,
manifest, face basis, semantic parts, large eyes and their separation.

For historical non-shipping proportion studies, run Blender in the background with
`--python tools/blender/compare_phosphor_proportions.py`. This edits copies of
the authored model and uses the normal exporter; Blender sources, PRB1 meshes
and a parameter manifest go under `build/robot-options/`. The current model and
three alternatives can be reviewed with `make robot-review`, then, for each
`<id>` (`current`, `a`, `b`, `c`):

```sh
build/robot-review build/robot-options/<id>/portrait.ppm build/robot-options/<id>/games/phosphor-run/game.conf portrait
```

These are static silhouette studies at 14×, 1× and 4× through the game renderer.
Selecting a study still requires adapting the bone pivots and runtime eye/neck
anchors, reviewing every animation, and updating the approved runtime reference.
The comparison command does not replace the shipped model or tuning. These
studies require the original pre-CRT source at
`build/robot-options/current/robot.blend`; they do not re-proportion the adopted
CRT source. `adopt_phosphor_crt.py` records the one-time selection and pivot/tag
adaptation; future editing and exports use the adopted canonical `.blend`.

Pass `-- --crt` after the Blender script to continue from proportion A with
three rounded CRT head studies (`crt1`, `crt2`, `crt3`). These replace the brow,
visor and jaw vent with a rounded cabinet, a thin rim, genuinely convex glass,
rounded phosphor eyes and a small smile. `crt-manifest.json` records cabinet
dimensions, corner radii and glass curvature. Render them with the same portrait
command above; the same static-study limitations apply.

### Scenery atlas rendering

The authored `assets/sprites/*.sprite` grids and configured palette remain the
source of truth. `scenery_atlas_load` packs them once at game initialization,
without resizing, into 512×512 RGBA8 pages (maximum 16). A colour cell and a white
silhouette cell share each frame's dimensions and one-texel transparent gutters.
Particles use the silhouette with their original colour override. All animation
frames preserve the text grid's top-down order, size and baseline. The manifest
is `assets/scenery-atlas.json`; there is no separately edited runtime atlas.
Build with `make web` / `make` as usual; dashboard sprite edits take effect on
reloading the game. Both hosts copy and upload each page once, release game
images on unload, and batch one textured quad per visible frame.

`draw_sprite_projected` preserves the camera's rounded texel edges throughout
fractional zoom; nearest filtering, texel-centre sampling, no mipmaps and gutters
prevent blur or neighbouring-cell bleed. HUD glyphs use cached font atlases with
one-pixel outlines at every supported font size. `sh tools/texture-tests.sh`
compares complete game frames, clipped sprites, and plain/outlined text against
the original rectangle renderer with zero pixel tolerance. The same GLES tests
also compile to WebGL (`--web`); run `node tools/texture-browser.cjs` on the
compiled directory. Authored grid files themselves retain the approved pixels.
