# Chirky Box in the browser

The existing C games compile to WebAssembly using Emscripten. `src/platform/web/host.c`
implements the host API with WebGL, reusing `src/rect_renderer.c`, the pixel
font, splash loader and launcher wordmark. Games retain their normal C source
and file paths. Each game has its own module, avoiding native `dlopen` and
colliding game symbols. Both hosts and all modules use ABI 12 and must be rebuilt together.

## Build and run

Install Emscripten (`emcc`), Make and Node.js 22 or newer. From the repository root:

```sh
make web
node tools/serve-web.cjs
```

Open <http://127.0.0.1:3031/> to play the exact standalone build. A different
port can be supplied, for example `node tools/serve-web.cjs 3032`.

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

## Controls and behaviour

- Arrows move; X is Primary, Z is Secondary, Enter is Start, Escape is Menu.
- **Settings → Input Settings** uses the Pi's shared console UI: Map Controller,
  Map Keyboard and Test Buttons. The wizard captures all eight inputs and saves
  after the last release. Duplicate inputs are rejected. F1 cancels a draft;
  holding two controller buttons also cancels controller setup. In Test Buttons,
  hold Secondary for one second to return. Cancel or a storage failure preserves
  the old mapping. Pi and browser mappings are stored independently.
- Menu opens the shared pause menu: Continue Game or Return to Launcher.
  Primary selects and Secondary goes back. Hardware Test lives in Settings and
  Menu returns there. Relaunching a game starts a fresh run. F1 provides keyboard
  recovery even if the saved mapping is inconvenient.
- Standard gamepads use D-pad/left stick, south for Primary, west for Secondary,
  and Start/Select for Start/Menu. Unrecognized USB/SNES adapters can map their
  raw buttons and axes in Map Controller; use the keyboard to get there first.
  Each controller identity keeps its own mapping. Pi's SNES defaults remain native.
- Existing touch controls appear on touch devices and support simultaneous
  movement/actions. Pointer cancellation or loss of focus clears held inputs.
- **Settings → Full Screen** toggles the whole player. Double-clicking the screen
  also toggles it. Browser permission requires a recent keyboard or pointer
  gesture; a gamepad alone may not authorize entry. Escape may exit browser
  fullscreen, so map Menu to another key if desired. **Mute / Unmute** is also
  inside Settings. There are no external navigation/settings buttons.
  The 320x240 framebuffer uses whole physical screen pixels at browser/OS zoom
  levels whenever space permits. Its size is set before WASM loading, and the
  launcher and games share one persistent canvas and WebGL context. Switching
  games does not reload the page or leave fullscreen. No game sprite sizes change.
  Unsupported fullscreen browsers display an explanatory message.
- Losing focus or hiding the tab pauses play. Resume continues the run.
- Games still render into the normal CRT-safe logical viewport. Simulation
  runs at 60 ticks/second with bounded catch-up after slow frames.
- No progress/save-state persistence is added; restarting starts a fresh game.

Automated checks use Chromium with simulated touch. Physical controllers,
touch devices, and other browser engines still require device testing.
