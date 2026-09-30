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
- `src/console_ui.h` owns launcher, settings, pause and input-screen rendering,
  plus menu selection/navigation rules. `src/input_setup.h` owns the eight-step
  mapping draft, release gating and duplicate rejection. Both hosts use these
  directly; browser menus are drawn inside the framebuffer, like the Pi.
- `src/rect_renderer.c`, `src/asset_store.c`, and the remaining files directly
  under `src/` implement reusable rendering, assets, timing, and host helpers.
- `src/viewport.h` is the canonical 320x240 framebuffer and 288x216 default
  safe-viewport geometry.

Portable code must not include Linux, Emscripten, DOM, WebAudio, DRM, ALSA,
evdev, or socket APIs. Add a platform operation or callback instead of testing
for a game id or operating system in core or game code.

## Linux platform

`src/platform/linux/` owns the Raspberry Pi executable and every Linux-only
dependency: DRM/GBM/EGL setup and presentation, evdev input and raw key state,
ALSA playback, POSIX HTTP transport, signals, control commands, and `dlopen`
game loading. Raw keyboard state is host control state and is never part of the
game ABI.

## Web platform

`src/platform/web/host.c` owns the Emscripten/WebGL bridge. Its `console.h`
adapter supplies catalog entries, platform actions and raw input to the shared
menus and wizard. Pi display calibration and power-down remain Linux-only;
fullscreen and sound muting are browser capabilities.

`web/player.js` creates the canvas WebGL context once. The launcher module stays
loaded while individual game modules load and unload on that same context via
Emscripten's preinitialized-context support. Each module owns its renderer and
GL resources; switching modules releases the outgoing game's resources, not the
display. Every render makes that module's context registration current and
binds its own GL state. URL/history changes do not reload the document. This
keeps fullscreen, screen geometry and console navigation intact.

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

`make test` covers the portable runtime, real game modules, Linux host behavior,
catalog generation, dashboard serving, and package validation. `make web`
compiles every `games/*/game.c` against the web platform and generates the
catalog and asset package. Browser checks and a real Pi deployment remain
required for platform rendering, DRM, input-device, and audio behavior.
