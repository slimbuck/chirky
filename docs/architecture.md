# Architecture

Chirky has one portable game ABI, one reusable runtime layer, and thin
platform implementations. Dependencies point inward:

```text
games -> include/chirky.h -> src runtime/render/assets
                                  ^
                  platform/linux -+- platform/web
```

The Pi/Linux and desktop/mobile browser console are equally supported targets.
The local dashboard is a development and Pi administration tool, separate from
the public static browser site. Bramble's director is an optional service,
not the console host. See [platform evolution](platform-evolution.md) for
proposed content identity, online scores, submissions and network multiplayer; those
features are not part of the implemented ABI described here.

## Portable code

- `include/chirky.h` is the complete game-facing ABI. It contains no operating
  system or browser types. Games receive logical console buttons, a logical
  viewport, rendering and asset callbacks, audio, optional local saves and the
  optional director transport.
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

The shared menu scroll animation is presentation state in `src/console.c`, advanced
by the same fixed update as navigation. Selection changes immediately; a damped
spring moves the rows beneath a fixed highlight and retains velocity when input
reverses. Lists stop at their first and last entries; blocked movement adds no
animation, and rendering never repeats entries beyond either end. Rendering
rounds to framebuffer pixels and never changes navigation.
Launcher, Settings, Input settings and Display area use this same list renderer.
Both hosts load the same 64x64 mascot, drawn inline with the wordmark or page
title to leave the full width below for content. Fredoka glyph masks, rounded
cards and the cream/gold/navy palette are shared with mapping, button testing
and the in-game pause overlay. See `assets/launcher/README.md` for the offline
asset pipeline.
Game manifests provide `launcher_order` (default 1000, with name as tie-breaker)
for the native host, browser catalog and dashboard defaults. Existing complete
Pi launcher configurations remain explicit overrides. `launcher_icon` refers
to a reviewed 20x20 PNG, compiled into a manifest-discovered portable icon
registry by the launcher artwork pipeline. Shared menu drawing clips the icon
with its scrolling row; it never scales game art at runtime.

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

The existing `button_label` callback resolves a logical action through the
current physical binding for the last-used input source. Both platforms support
SNES legends and generic button/axis identifiers through per-model profiles.
Controllers without a saved profile default to SNES bindings and labels on both hosts.
Browser generic standard pads use positional legends. Labels follow keyboard,
touch or the active gamepad and its saved mapping. Games query labels during rendering and never
interpret raw device identities. `include/input_labels.h` provides portable prompt
helpers without changing the input ABI or game update logic.

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

## GPU rendering

Rectangles, textures and opaque meshes share `src/rect_renderer.c` on GLES2 and
WebGL1. The ABI 14 mesh callback preserves logical viewport ownership and drawing
order. Host contexts provide a depth buffer; each mesh draw clears its own depth
layer and restores 2D rendering state. Phosphor Run's robot evaluates rigid poses
on the CPU, then submits one triangle list. Lighting and rasterization are GPU
work; there is no CPU triangle rasterizer or conversion into pixel rectangles.

The Phosphor development viewer is a separate `make robot-preview` target in
`tools/phosphor-preview.c`, sharing the game's evaluator and production renderer.
Its dashboard page and setting writes are local development tools, outside the
public browser package. Game camera transforms belong in the game; the host
continues to apply only the calibrated logical viewport offset.

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

ABI 15 adds load-time RGBA image creation and camera-projected sprite drawing.
The asset store owns immutable CPU images; the host image cache owns their GPU
textures. Phosphor Run packs its editable text grids and configured palette once
at game init. Both hosts use the same renderer-owned glyph atlases for 5×7 text
and one-pixel outlines. Font atlases survive game switches; game scenery atlases
do not. The original rectangle paths remain pixel oracles and headless fallbacks.

## Local multiplayer (ABI 16)

The combined input remains the default for existing games and all console menus.
Alongside it, hosts provide up to eight logical device snapshots with opaque
connection IDs, independent button edges and physical labels. `src/local_input.h`
shares edge tracking between adapters; games never see evdev or Gamepad codes.
Games own device-to-player assignment and their response to a disconnected
source. The host reports devices without assigning player slots.

Both hosts expose two keyboard layouts. P1 also supplies the combined keyboard
input used by menus and solo games; there is no separate solo mapping.
Player layouts have independent movement/actions and may share Start/Menu keys.
Linux groups keyboard interfaces into these layouts, keeps gamepads separate, scans for newly
connected devices once per second, and clears unplugged state. Both platforms
keep profiles by model and input state by connection ID. Identical pads share a
profile but have separate inputs. Touch is one additional browser source.

Input settings derives USB availability and separate test rows from the same
per-device snapshots that games receive. Both hosts render this through the
shared console UI; colours supplement source labels rather than identifying
players. Raw device readings remain diagnostic text below the rows.
The browser supplies presentation flags for its touch layout: hide keyboard
mapping entries and diagnostic rows until a key is used. The shared
console owns filtering and selection recovery; game input snapshots are unchanged.

The shared console owns controller selection, SNES/Generic choice, remapping,
release/cancel gates and disconnect recovery. Platform callbacks resolve a
connection to its model, apply the platform-specific raw SNES preset and persist
bindings. Capture and release detection use only the selected connection.
Linux stores profiles atomically in `config/controllers.conf`, keyed by USB
bus/vendor/product/version plus a device-name hash. Old global `bind_*` and
legend settings remain a migration fallback for the tested Pico model only.
Browser `chirky.controllers.v2` uses the reported gamepad ID and mapping mode;
old v1 arrays load as Generic profiles. Storage remains local to each host.
Both hosts keep tracking per-source edges while the console is paused/loading,
so a menu press does not become a gameplay press on resume.
