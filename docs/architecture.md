# Architecture

Chirky has one portable game ABI, one reusable runtime layer, and thin
platform implementations. Dependencies point inward:

```text
games -> include/chirky.h -> src runtime/render/assets
                                  ^
                  platform/linux -+- platform/web
```

## Portable code

- `include/chirky.h` is the complete game-facing ABI. It contains no operating
  system or browser types. Games receive logical console buttons, a logical
  viewport, rendering and asset callbacks, audio, and the optional director
  transport.
- `src/runtime.c` owns game ABI validation and the init, update, render, and
  shutdown lifecycle used by both hosts.
- `src/console.c` owns console state and transitions: catalog menu construction,
  navigation, pause/resume, release gates, mapping commit/cancel, recovery holds,
  loading state, and display calibration. Both hosts feed `console_input` and
  implement `console_services`; platform adapters must not implement another
  menu state machine. Capabilities determine which platform actions appear.
- `src/console_ui.h` draws console screens; `src/input_setup.h` manages mapping
  drafts and duplicate rejection. `src/drawing.h` implements shared font drawing
  and rectangle clipping.
- `src/rect_renderer.c`, `src/asset_store.c`, and the remaining files directly
  under `src/` implement reusable rendering, assets, timing, and host helpers.
- `src/viewport.h` is the canonical 320x240 framebuffer and 288x216 default
  safe-viewport geometry.

Portable code must not include Linux, Emscripten, DOM, WebAudio, DRM, ALSA,
evdev, or socket APIs. Add a platform operation or callback instead of testing
for a game id or operating system in core or game code.

Game-local persistent records use the optional ABI 13 `save_read` / `save_write`
callbacks. Games own versioned record formats; hosts own storage. Names are
bounded game/key identifiers, and records are limited to 64 KiB. Linux writes
`saves/<game>/<key>` using a flushed temporary file and atomic rename; deployment
leaves this directory intact. The browser stores the same bytes in localStorage
under `chirky.save.v1.<game>.<key>`. Storage is local to the device/browser origin,
not cloud-synced. Missing, oversized or unreadable records return zero; failed
writes return false so games can show a useful failure message.

## Linux platform

`src/platform/linux/` owns the Raspberry Pi executable and every Linux-only
dependency: DRM/GBM/EGL setup and presentation, evdev input and raw key state,
ALSA playback, POSIX HTTP transport, signals, control commands, and `dlopen`
game loading. Raw keyboard state is host control state and is never part of the
game ABI.

Raw analogue axes participate in mapping and release detection only after a
neutral value has been observed. Some digital USB controllers advertise unused
sticks fixed at their minimum; those must not block console navigation. Explicit
axis bindings still use the current device value for logical input.

## Web platform

`src/platform/web/host.c` owns the Emscripten/WebGL bridge.
`console_bridge.h` transports manifest metadata, raw input, localStorage mapping
results and browser actions to the portable console. Display calibration is
portable console code exposed by the Linux capability; fullscreen and muting
are browser capabilities. Power-down and timing instrumentation are Linux
services.

`web/player.js` creates the canvas WebGL context once. `launcher.js/.wasm` is
one persistent Emscripten main module containing the console, game runtime,
renderer, asset store, image cache and virtual filesystem. Games compile as
side modules containing only their own C sources. Their generated `<id>.js`
files are descriptors, not separate hosts. JavaScript downloads module bytes and
assets into the host filesystem. The C adapter uses `emscripten_dlopen` to
compile and load them asynchronously, then starts the game through `src/runtime.c`.
Loading requests are serialized, and cancelled generations cannot initialize a
game. Both local builds and CI use the official SDK pinned in `.emscripten-version`.
Game symbols have hidden visibility and do not share game state with each other.

Game exit calls shutdown, disconnects audio/director services, releases images
and clears the asset store. The host, renderer and framebuffer remain alive.
Compiled side modules and downloaded files are cached per game for the page's
lifetime; `init` must reset all game state on every launch, just as after a Pi
module load. Cancellation invalidates pending download work before it can
activate a game. URL/history changes do not reload the document, preserving
fullscreen and integer canvas scaling.

Browser services own DOM keyboard codes, Gamepad API buttons/axes, localStorage,
WebAudio, fetch and animation frames. Raw device codes are platform-specific;
logical buttons, menus, mapping workflow, game runtime and rendering are shared.
Controller mappings are saved per browser-reported device identity. Unrecognized
USB adapters use the same mapping wizard as standard controllers.

## Rendering roadmap

Polygon and mesh rendering must move to the GPU. The software triangle path in
Phosphor Run's articulated robot is temporary: it currently transforms and
rasterizes polygons on the CPU, resolves them to pixels, and submits those pixels
as rectangles.

Replace that path with a platform-neutral rendering capability exposed through
the Chirky ABI or shared renderer. The Linux implementation must use GLES and the
browser implementation must use WebGL. Do not add another CPU polygon rasterizer
or expand polygon output into per-pixel rectangle commands. Preserve Chirky's
320x240 logical coordinate system and identical game-facing behavior across both
platforms while allowing each GPU backend to own its native setup and submission.

## Game catalog

Every game is declared once by `games/<id>/game.conf` beside its `game.c`.
`id`, `name`, `description`, and `module` are required, and `id` must match the
directory. `role=diagnostic` marks a platform diagnostic instead of a normal
playable game. `browser_level_setting=<config-key>` optionally maps the browser
`level` query parameter to a game setting.

The native host reads these manifests directly. `tools/game-catalog.js`
validates the same manifests and generates `build/web/catalog.json`, which is
consumed by the browser launcher, dashboard server, package validator, and
export. Do not add another hand-maintained game-id list.

## Validation

`make test` runs the same console event trace with Linux and browser capabilities,
then covers the portable runtime, real game modules, Linux host behavior,
catalog generation, dashboard serving, and package validation. `make web`
builds one web host and a side module for each `games/*/game.c`, then generates the
catalog and asset package. Browser checks and a real Pi deployment remain
required for platform rendering, DRM, input-device, and audio behavior.
