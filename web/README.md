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
- **Input settings** remaps all eight inputs. Duplicate keys are rejected;
  Cancel preserves the live mapping; Save persists this browser's settings.
  Reset defaults restores keyboard and touch preferences. Pi mappings remain
  independent. Games display Chirky input names; the page shows bound keys.
- Menu pauses/resumes. Hardware Test uses Menu to return to the launcher.
  The page also offers Pause, Restart, Launcher, Mute, and Full screen.
- Standard gamepads use D-pad/left stick, south for Primary, west for Secondary,
  and Start/Select for Start/Menu. Pi's SNES adapter has its own native defaults.
- Touch controls support simultaneous movement/actions and Auto/Show/Hide.
  Pointer cancellation or loss of focus clears held inputs.
- Full screen toggles the whole player, including touch controls and navigation.
  Canvas scaling uses integer multiples of 320x240 whenever space permits.
  Unsupported fullscreen browsers display an explanatory message.
- Losing focus or hiding the tab pauses play. Resume continues the run.
- Games still render into the normal CRT-safe logical viewport. Simulation
  runs at 60 ticks/second with bounded catch-up after slow frames.
- No progress/save-state persistence is added; restarting starts a fresh game.

Automated checks use Chromium with simulated touch. Physical controllers,
touch devices, and other browser engines still require device testing.
