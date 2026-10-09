# Chirky in the browser

The existing C games compile to WebAssembly using Emscripten. `src/platform/web/host.c`
implements the host API with WebGL, reusing `src/rect_renderer.c`, the pixel
font, splash loader and launcher wordmark. Games retain their normal C source
and file paths. One persistent main module owns the console, renderer, assets
and game runtime. Each game is a loadable WASM side module with isolated game
symbols, using the same `chirky_game_entry()` contract as the Pi. The generated
`<game>.js` file only identifies its WASM and configuration paths. Both hosts
and all game modules use ABI 16 and must be rebuilt together.

## Build and run

Install Make, Python 3 and Node.js 22 or newer. Use the official Emscripten SDK
version in `.emscripten-version`, matching CI. Distro packages can combine older
Emscripten sources with different LLVM/system libraries; the Ubuntu package
failed during main-module startup despite compiling successfully.

On Linux or inside WSL, from the repository root:

```sh
version=$(cat .emscripten-version)
sdk="$HOME/.cache/chirky/emsdk-$version"
git clone --depth 1 --branch "$version" https://github.com/emscripten-core/emsdk.git "$sdk"
"$sdk/emsdk" install "$version"
"$sdk/emsdk" activate "$version"
```

`make web` automatically finds this SDK and verifies its version before building.
For another installation, set `EMCC=/path/to/emsdk/upstream/emscripten/emcc`.
See the [official SDK guide](https://emscripten.org/docs/tools_reference/emsdk.html).
Then build and preview:

```sh
make web
node tools/serve-web.cjs
```

Open <http://127.0.0.1:3031/> to play the exact standalone build. A different
port can be supplied, for example `node tools/serve-web.cjs 3032`.
To preview from another device over LAN or Tailscale, listen on all interfaces
with `node tools/serve-web.cjs 3033 0.0.0.0`, then open this computer's LAN or
Tailscale IP with port `3033` on that device.

On Windows with Emscripten installed in WSL, build with
`wsl make web NODE=node.exe`, then run the preview with Windows Node.js.

`build/web/` is also a standalone static site: serve that directory over HTTP
or HTTPS, with `.wasm` served as `application/wasm`. It needs no Pi or dashboard
API. Do not open `index.html` directly with `file://`.

For live asset editing, run `node dashboard/server.js` and open
<http://localhost:3030/play/>. This uses the same player and WASM files but
reads the current working-tree assets and configuration bundle.

The dashboard serves runtime assets directly from the working tree. **Restart**
reloads them without recompiling. The editor's **Save and play in browser**
button saves locally and starts the selected campaign level. C changes require
`make web`; standalone static-site assets must also be refreshed with `make web`.

## Add a game to the browser

`make web` discovers every `games/*/game.c` and generates `catalog.json` from
the adjacent manifests through `tools/game-catalog.js`. The dashboard,
packager, and browser launcher consume that same catalog; do not add another
game-id list. `src/platform/web/host.c` asks the player for the generated
launcher count and labels. After rebuilding, restart `dashboard/server.js`, open
<http://127.0.0.1:3030/play/>, confirm the game appears, select it in the
launcher, and check that both `<game>.js` and `<game>.wasm` return HTTP 200.
A direct `?game=<id>` load only verifies the game route, not launcher
registration.

Run these focused checks before the full suite:

```sh
node --test dashboard/play.test.js tests/game_catalog.test.cjs tests/player_audio.cjs tests/web_package.cjs
make web
```

## Test and publish

`build/web/` is the standalone site published at **chirky.org**. The player,
catalog, assets and configuration all come from this repository. Slimbuck links
to the site and no longer receives exported game files. Configuration files are
transported through `configs.json`, retaining their original paths inside WASM.

`build.json` records the source commit, dirty status, and SHA-256 file hashes.
Rebuild after changes. Test a local build with:

```sh
node tools/publish-web.cjs --check
```

This exercises every game through the launcher at desktop/mobile sizes and
checks gameplay, input, audio and lifecycle behavior. Reports and screenshots
are saved in `build/publish-checks/`. See [AWS publishing](../deploy/WEB.md)
for initial hosting setup, GitHub Actions, publishing, and deployed-byte checks.

Cold startup begins launcher JavaScript, WASM and mascot downloads from HTML
preloads. The player fetches catalog, asset list and configurations concurrently
with the module downloads, then initializes the shared console. Production HTML
pins every file to one immutable release directory. CloudFront compresses and
caches those files; the player honors HTTP caching instead of forcing downloads.
The root HTML uses `max-age=0,s-maxage=3600,must-revalidate`: browsers check
CloudFront on each visit, but the edge can serve it for an hour without checking
S3. Publishing invalidates `/` and `/index.html` after switching releases.
Local previews and the dashboard retain their no-store responses for iteration.
`--check` previews the versioned layout used in production, including root game
links, rather than relying only on the flat development layout.

For repeatable cold/warm measurements in three fresh Chrome profiles:

```sh
node tools/profile-web.cjs https://chirky.org/ build/load-profile
node tools/profile-web.cjs https://chirky.org/ build/load-mobile --mobile
```

The optional throttle models 1.6 Mbps down, 750 Kbps up, 85 ms latency and 4x
CPU slowdown on the test computer; it is not a physical phone benchmark. Reports
include request waterfalls, transfer bytes, startup/compilation timings, a CPU
profile and launcher screenshot. Browser caches start empty; CDN caches are
left intact, as they would be for a new visitor.

## Controls and behaviour

Launcher order comes from each `game.conf`'s optional `launcher_order` integer
(0–1000000, default 1000). Diagnostic games stay in Settings. Optional
`launcher_icon` points to a native 20x20 picture built by
`python tools/build-launcher-art.py`; see `assets/launcher/README.md`.

- P1/default: arrows, N/M. P2: WASD, F/G. Both use Enter for Start and Esc for Menu.
- Console and game prompts resolve those internal actions to current physical
  controls. The last pressed device selects keyboard, touch or controller
  labels; a held pad does not override a subsequent keyboard/touch press.
  Keyboard remaps are reflected immediately; touch shows A/B and Menu/Start.
  Arrow keys and D-pad directions use `← → ↑ ↓`; letter bindings keep their letters.
  New controllers default to the SNES profile: B/Y/A/X, Start/Select and arrow labels.
  Explicitly saved profiles take precedence. Generic standard
  gamepads use positional face labels (South/East/West/North); generic raw adapters
  use button/axis identifiers. Disconnecting the active pad restores keyboard labels.
- The desktop keyboard guide uses chunky keycaps: an inverted-T movement cluster,
  coral Primary, turquoise Secondary, and cream Start/Menu keys. Labels follow
  saved keyboard mappings and depress while the corresponding key is held.
  Recovery and the display's double-click fullscreen shortcut sit below the keys.
  The guide wraps in narrow windows and stays hidden in touch layouts and fullscreen.
  Its Fredoka 500–600 character subset and SIL Open Font License are embedded in
  `style.css`, so the guide needs no external font request.
- **Settings → Input Settings** runs the same portable console code as the Pi: Test inputs,
  Map keyboard 1, Map keyboard 2, numbered Map controller entries, Back.
  Each controller entry appears only while that controller is connected. Its options have
  a Back option and accept Esc/Menu, Secondary or F1.
  The touch layout hides Input Settings entirely until a controller is detected
  or a physical keyboard is used. Losing the last controller before keyboard use
  closes input setup or testing and returns to Settings, discarding any draft.
  The touch layout initially hides both keyboard mapping entries and keyboard
  test rows. Keyboard use reveals both for the rest of the page session, and
  physical keyboards still work in games. Desktop layouts always show keyboard setup.
  Selecting a controller entry directly offers SNES preset, Generic controller and Remap buttons.
  Controller activity briefly marks its menu row to help identify it. Unplugging
  the selected controller returns to input settings and cancels any draft mapping.
  SNES applies default bindings and legends; Generic opens the mapping wizard;
  Remap retains the existing legend profile. The wizard captures all eight inputs and saves
  after the last release. Duplicate inputs are rejected. F1 cancels a draft;
  holding two controller buttons also cancels controller setup. In Test inputs, each source has its own labelled, coloured row showing its
  configured physical button labels, including when released. D-pad directions are
  arranged with Up above Left/Down/Right when row space permits, beside Prim, Sec, Start and Menu;
  pressed cells highlight in the device colour. Both keyboard layouts and each
  controller remain distinct; fixed touch controls are omitted. Hold Menu for one second to return. Cancel or a storage failure preserves
  the old mapping. Pi and browser mappings are stored independently.
- Menu opens the shared pause menu: Continue Game or Return to Launcher.
  Primary selects and Secondary goes back. Relaunching a game starts a fresh run. F1 provides keyboard
  recovery even if the saved mapping is inconvenient.
- Default SNES bindings on standard gamepads use D-pad/left stick, south for Primary, west for Secondary,
  and Start/Select for Start/Menu. Saving the SNES preset retains left-stick support. Remapped directions and
  Generic controller profiles use their explicit bindings.
  The GP2040 Generic HID adapter uses its separate D-pad buttons (16–19);
  old unmodified SNES presets are corrected automatically, preserving custom maps.
  Raw adapters default to the SNES/Pico layout; other USB devices can map their
  raw buttons and axes in Map controller; use the keyboard to get there first.
  Both platforms save profiles per model, independently of player connection IDs.
  Browser profiles use `chirky.controllers.v2` in localStorage; existing v1 maps
  migrate as generic profiles. Identical controllers share a profile. Selecting
  SNES preset installs the tested raw SNES/Pico or browser-standard bindings.
  Setup captures only the chosen connection; disconnecting discards its draft.
- Touch devices get a controller-shaped D-pad, A (Primary) and B (Secondary) face
  buttons, and smaller Menu/Start buttons above the two control groups. The A/B labels
  use [Fredoka Bold](https://fonts.google.com/specimen/Fredoka) (weight 700), by the Fredoka
  Project Authors, available under the [SIL Open Font License](https://github.com/google/fonts/blob/main/ofl/fredoka/OFL.txt).
  Their inline SVG outlines in `index.html` preserve the approved 30px lettering:
  each visible glyph bounding box is centred at (31,31) in a 62px button face.
  They need no font download or platform fallback. The toy handheld shell uses warm yellow,
  a graphite D-pad, coral Primary and turquoise Secondary, with inset grips and
  visible pressed feedback. Branding appears within the display only.
  Rounded corners, strong shell outlines and highlights, a raised five-pixel
  painted bezel, roomy grips and landscape speaker slots retain the handheld
  appearance. The measured display border stays one pixel; compact shell
  margins leave room for the painted bezel while preserving fractional scaling.
  All targets are at least 44 CSS pixels;
  the whole D-pad and an 8-pixel margin accept a sliding thumb. Directions follow
  the thumb around the fixed centre, with eight sectors (including diagonals)
  and a small neutral centre. Dragging beyond the pad keeps the direction held
  until release. A second finger can hold an action while steering; extra fingers
  on the D-pad do not take it over. Held directions/actions light up. Pointer cancellation,
  lost capture or loss of focus clears held inputs and the pressed appearance.
  In landscape, controls flank a centred display sized to the available height
  and width, in both normal and fullscreen mode. Portrait uses the visible viewport:
  the display gets the full available width, centred above the two grips anchored
  at the bottom. Normal desktop play wraps a centred shell around a display up
  to 800 pixels wide, with equal top and side margins and keyboard hints grouped
  together underneath. Smaller windows scale the display down; the keyboard
  guide wraps beneath it and can scroll into view in short windows;
  fullscreen still fills the available space. The page
  heading and keyboard help are hidden on touch layouts to leave room for play.
  Browser toolbar resizing and rotation recalculate the layout; bottom safe-area
  padding keeps controls above the phone's gesture bar. Safe-area padding keeps landscape controls clear of
  device cutouts. Layout and touch event handling belong in `web/`; they send the
  same logical Chirky inputs to the shared console and games.
  Canvas presentation fits its available slot at 4:3 without rounding to integer
  scales. The native framebuffer stays 320x240 and CSS keeps pixelated scaling.
  Fractional scales can produce uneven apparent pixel widths; this first pass
  leaves their appearance for device review before adding any filtering shader.
  Browser checks cover maximum fit, stable loading, source rows/columns in raster
  screenshots, and controls staying visible without overlap.
- **Settings → Full Screen** toggles the whole player. Double-clicking or
  double-tapping the display also toggles it. Touch selections request fullscreen
  on finger-up, when the browser grants permission; short menu taps are delivered
  to the shared console inside that gesture, without advancing game simulation.
  Keyboard selections likewise retain their gesture. A gamepad alone may not
  authorize entry. Escape may exit browser
  fullscreen, so map Menu to another key if desired. **Mute / Unmute** is also
  inside Settings. There are no external navigation/settings buttons.
  The 320x240 framebuffer scales to the available space at browser/OS zoom
  levels, including fractional scales. `web/shell.js` is inlined into the generated
  HTML by `tools/web-assets.js`, applying saved touch preferences, keyboard
  labels and canvas sizing before the first paint, without another download.
  The same layout function handles later resizing and fullscreen changes.
  Its size is set before the player module or WASM loads, and the
  launcher and games share one persistent canvas, WebGL context, renderer and
  asset store. Switching
  games does not reload the page or leave fullscreen. No game sprite sizes change.
  Initial page startup has an HTML progress bar centred inside the display,
  visible before JavaScript or WASM is ready. Its percentage combines completed
  startup stages with host WASM download bytes (when an uncompressed byte total
  is available); it is not an elapsed-time estimate. Compilation and asset setup
  reserve the final portion, and 100% appears only after the first console render.
  Unknown or compressed response sizes advance at stage completion. Startup
  failures keep an in-display reload message visible. This browser bootstrap
  overlay does not replace the shared console's game-loading screen.
  While a game loads, the shared console draws a small single-scale `LOADING`
  badge in the safe viewport's top-right corner, clear of the launcher rows.
  Standard and WebKit-prefixed fullscreen APIs are supported. Unsupported or
  blocked requests show a message over the player, including in landscape;
  they never replace the launcher/game title. Mobile browser checks use real
  touch events for double-taps and menu selection after earlier activation expires.
- Losing focus or hiding the tab pauses play. Resume continues the run.
- iPhone/iPad browsers that reject or lack element fullscreen show Safari's
  **Share → Add to Home Screen** instructions instead of suggesting repeated
  fullscreen retries. Enable **Open as Web App** when that option is shown, then
  launch Chirky from the new icon for a view without Safari's toolbar. Apple web
  app metadata also enables this mode on older iOS versions. A Home Screen app
  recognises that launch mode; its Full Screen action reports that it is already
  running as an app. Native fullscreen remains available where supported.
  This does not add offline caching; loading the games still requires a network.
- Page pinch/double-tap zoom is disabled across the console, including its display
  and empty shell. Viewport limits and `touch-action: none` are backed by
  non-passive Safari gesture handlers and multi-touch move cancellation.
  Pointer input remains active for diagonal D-pad movement and simultaneous
  action buttons; the display's deliberate double-tap fullscreen shortcut remains.
  The D-pad and action/system buttons also cancel native touch-start gestures
  to suppress browser long-press feedback while pointer events handle held inputs.
- Games still render into the normal CRT-safe logical viewport. Simulation
  runs at 60 ticks/second with bounded catch-up after slow frames.
- Restarting starts a fresh run; there is no general resume/save-state feature.
  Game-owned persistent records are supported through ABI 13. Phosphor Run's
  high scores survive reloads in localStorage, isolated to this browser origin.
  They are not global scores or synchronized with a Pi. Browser mappings and
  preferences also persist locally.

The public build currently provides play, not a public editor or submission
service. Dashboard editing changes the developer's working tree. Personal
content drafts, shared leaderboards and multiplayer are planned separately;
see [platform evolution](../docs/platform-evolution.md).

See [architecture](../docs/architecture.md) for code ownership and module lifetime.

Automated checks use Chromium with simulated touch. Physical controllers,
touch devices, and other browser engines still require device testing.

### Circuit Clash local play

Select Circuit Clash in the launcher. Release the launch button, then each
player presses their own action button to join. One keyboard supports both
players: P1 arrows + N/M and P2 WASD + F/G, with shared Enter/Esc. Change either layout
in Input Settings; P1 also controls menus and solo games. Two gamepads or keyboard
plus gamepad also work; touch can claim one slot. Identical gamepads are distinct
players. Primary punches, Secondary (or Up) jumps, and left/right move. Prompts
show the mapped physical controls. Both players press their punch button to
rematch after a KO. A disconnected player pauses the match until an unassigned
device joins. Menu opens the normal shared pause menu.

Run `node tools/circuit-clash-browser.cjs` against the dashboard for the real
WASM check with two simulated identical pads and keyboard replacement. It checks
independent movement and records fitted 4:3 gameplay/lobby screenshots.
