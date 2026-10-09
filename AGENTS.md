# Chirky Agent Guide

## Project Context

Chirky is a small C games console with equally supported Raspberry Pi/Linux
and desktop/mobile browser targets. The Pi uses native shared game modules;
the browser uses WebAssembly side modules. Both share the portable console,
runtime, game code and game-facing ABI. Mobile currently means the browser
player, including Home Screen launch, not a separate native app.

There are four unfinished games (Phosphor Run, Rosey Chop, Bramble Hollow and
the local two-player Circuit Clash prototype). `games/*/game.conf` remains the authoritative catalog; this overview
is not a runtime registration list.

The local Node dashboard edits working-tree game data and controls one
configured Pi at a time over SSH/SCP, normally at a static LAN address. It
copies source/assets and invokes the Pi build. The installed console runs
without the dashboard. The public browser build is a separate static site;
the checked-in publishing target is chirky.org. Do not expose the development
dashboard or its Pi administration routes as a public player service.

Phosphor Run stores device-local scores through the host save callbacks.
Bramble's optional director supplies eventually-consistent world state; it is
not a multiplayer session service. Global scores, public content submissions
and network multiplayer are future work, not existing platform features.

Read `README.md` for operation, `docs/architecture.md` for code ownership,
`docs/platform-api.md` for the current ABI, and `docs/platform-evolution.md`
for the proposed sequence for online services, local content and multiplayer.
Keep implemented behavior distinct from proposals when updating these files.

## Future Platform Work

- Preserve standalone single-player operation. Optional online services must
  not block game frames or make the dashboard necessary for normal play.
- Design new game-facing services for both native and browser hosts. Keep
  transport, credentials and device identity outside game code; use explicit
  capabilities and unavailable states where support differs.
- Keep official content, local drafts and submitted content distinguishable.
  Before adding shared scores or sessions, define rules/content compatibility;
  a level name or client-reported hash alone does not prove a legitimate run.
- Add concrete service contracts when implementing their first consumer.
  Do not expand the ABI with unused networking callbacks or repurpose the
  world-director protocol for real-time player input.

## Rendering Contract

- Chirky targets a native 320x240 framebuffer. The host reserves and centers a
  CRT-safe logical viewport using `safe_x`, `safe_y`, and the calibrated display
  offsets. Games draw only in `chirky_host_api.screen_width` by
  `chirky_host_api.screen_height` coordinates.
- Do not add the CRT border or centering offset inside a game. Native sprite and
  rectangle helpers in the host apply that offset and clip to the logical
  viewport. Phosphor Run is the reference implementation for this ownership.
- The browser host emulates the same framebuffer and safe viewport. Fit the
  320x240 canvas as large as possible at 4:3, allowing fractional display scales
  and keeping the surrounding border minimal. Keep pixelated scaling for user
  review; a filtering shader is a possible later change, not yet implemented.

## Pixel Art

- One runtime sprite texel should normally become one framebuffer pixel. Keep a
  sprite atlas cell the same dimensions as its destination draw rectangle.
- Never generate large art, enlarge it, and then shrink it at render time. A
  nearest-neighbour reduction such as 64x64 to 40x40 still distributes source
  pixels unevenly and looks stretched on both WebGL and the Pi.
- High-resolution generated artwork may be retained as source material, but it
  must be isolated and reduced exactly once offline to the final runtime grid.
  Inspect that reduction as pixel art; a valid image file is not evidence that
  facial details, silhouettes, or animation alignment survived. Do not replace
  approved artwork with crude procedural geometry merely to simplify scaling.
- Use a small palette, binary alpha where appropriate, transparent gutters,
  and a shared baseline for animation frames.
- Mixed-size subjects may share an atlas by using transparent padding in one
  common final-size cell. Do not compensate by drawing the same cell at several
  aspect ratios.
- Preserve integer positions and dimensions through camera, clipping, and draw
  calls. Do not add visual bobbing to an animation whose frames already move the
  body unless that motion is intentional and tested.

## Visual Verification

For rendering or asset changes:

1. Compare the authoritative source with the final runtime cell at a large
   nearest-neighbour zoom. Check identity details, facial marks, thin lines,
   silhouettes, transparency, and feet alignment.
2. Inspect the runtime asset at its original 1x resolution. Generated source
   art and a magnified preview can both hide a bad reduction.
3. Verify idle, every walk direction/frame, edge clipping, and every composed
   state such as riding a bicycle in the live game.
4. Check the browser at its actual fitted display scale, including fractional
   scales on desktop and mobile, as well as native-resolution assets. Do not
   approve a visual change from code review or an atlas preview alone.
5. Run `wsl make test NODE=node.exe` and `wsl make web NODE=node.exe` on Windows.
6. When asked to deploy, use the dashboard deployment path, launch the game,
   confirm `/api/status`, inspect a real Pi snapshot, and compare the deployed
   asset hash with the tested local asset. A successful build or deploy response
   alone is not visual verification.

Tests should assert atlas dimensions, transparent cell gutters, animation
baseline alignment, and 1:1 source/destination dimensions so visual regressions
fail before deployment.

Structural tests do not prove that artwork looks good. A game with authored
sprites should also retain approved runtime atlases or visual reference sheets.
Regeneration checks should build into a temporary location and byte-compare the
result with those approved files. Intentional visual changes require reviewing
and updating the reference, not weakening the comparison.

Each game with a nontrivial asset pipeline should have one documented build
command and one machine-readable manifest for atlas size, cell size, frame
order, alignment, and important semantic details. Do not manually patch shipped
runtime atlases or introduce a second undocumented generator.

## Browser Game Registration

- Native and WebAssembly modules are discovered from `games/*/game.c`.
  `tools/game-catalog.js` validates the adjacent `game.conf` files and generates
  the browser catalog. Do not add a hand-maintained game-id or title list.
- A manifest id must match its directory and provide `name`, `description`, and
  `module`. Use `role=diagnostic` for a platform diagnostic and
  `browser_level_setting=<key>` only when browser campaign links must override
  a numeric game setting.
- The launcher in `src/platform/web/host.c` obtains its count and labels from
  the generated catalog through `web/player.js`. The dashboard allowlist and
  package validator consume that same catalog.
- A successful `make web`, a present `build/web/<game>.wasm`, or a working
  direct `/play/?game=<id>` URL does not prove launcher integration. Restart
  the dashboard after server changes, open `http://127.0.0.1:3030/play/`,
  verify the game is visible, select it through the launcher, and check that the
  resulting JS and WASM requests return 200 without console errors.
- Run `dashboard/play.test.js`, `tests/player_audio.cjs`, and
  `tests/game_catalog.test.cjs` and `tests/web_package.cjs` for browser-catalog
  changes, followed by
  `wsl make test NODE=node.exe` and `wsl make web NODE=node.exe` on Windows.

## Platform Boundaries

- Keep `include/chirky.h`, games, and files directly under `src/` portable.
  They must not include Linux, Emscripten, DOM, DRM, ALSA, evdev, or socket
  interfaces.
- Linux-only implementation belongs in `src/platform/linux/`; the Emscripten C
  bridge belongs in `src/platform/web/`; browser JavaScript and presentation
  belong in `web/`.
- Both hosts use `src/console.c` for console state, navigation, pause, mapping
  workflow, recovery, loading transitions and capability-based settings. Keep
  platform adapters limited to raw input and service callbacks; do not add a
  second menu state machine in C or JavaScript. Console drawing lives in
  `src/console_ui.h`, with shared font drawing in `src/drawing.h`.
- The browser has one persistent main module, renderer and asset store. Game
  WASM side modules contain only game code; generated JavaScript descriptors
  identify their files. Game changes must not recreate the host or renderer.
- Build all browser modules with the official SDK version in `.emscripten-version`.
  `make web` verifies the compiler and finds the documented WSL/Linux SDK install
  automatically. See `web/README.md`; do not mix toolchain versions or bypass
  the version check when publishing.
- Both hosts use `src/runtime.c` for game lifecycle and `src/viewport.h` for
  framebuffer defaults. Do not duplicate init/shutdown sequencing or literal
  320x240/288x216 viewport geometry in a platform host.
- Platform code must use manifest metadata or generic capabilities instead of
  branching on a particular game id. See `docs/architecture.md` before changing
  host ownership or introducing a new platform service.

## Working Tree

This repository may contain unrelated user changes. Do not revert or reformat
them. Keep fixes scoped, and update documentation and validation together when a
runtime asset contract changes.
