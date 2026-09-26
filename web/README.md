# Chirky Box in the browser

The existing C games compile to WebAssembly using Emscripten. `web/host.c`
implements the host API with WebGL, reusing `src/rect_renderer.c`, the pixel
font, splash loader and launcher wordmark. Games retain their normal C source
and file paths. Each game has its own module, avoiding native `dlopen` and
colliding game symbols. The Pi build and ABI remain compatible.

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

## Test and update the website

Chirky owns the browser player, configuration loading and build. Configuration
files are transported in `configs.json`, retaining their original `.conf` names
inside WASM; this also works on CDNs that block `.conf` URLs. Embedded players
do not steal focus on startup. No website-specific player edits are needed.

`build/web/build.json` records the source commit, whether the source tree had
uncommitted changes, and SHA-256 hashes for every packaged file. It describes the
last build, not subsequent edits. Rebuild before testing or exporting new changes.

```sh
node --test tests/web_package.cjs tests/player_audio.cjs
node tools/platform-browser.cjs http://127.0.0.1:3031/ build/standalone-checks
node tools/export-web.cjs ../slimbuck.com
```

The export command starts a temporary standalone server, runs desktop and mobile
browser checks for both games, verifies that the package is unchanged, then
replaces only the website's `apps/chirky/` directory with the tested bytes.
It removes obsolete files there and leaves other apps untouched. Chrome must be
installed; set `CHROME` to its executable if necessary. Screenshots and results
go to `build/web-export-checks/`. Export does not commit, push or publish.

The website build copies this bundle unchanged. Review and commit both
repositories as appropriate; pushing the website's `main` branch triggers its
existing deployment. Its post-deployment checker verifies the published files
and reports the Chirky source commit. Commit Chirky and rebuild before a release
to obtain a clean source revision rather than a dirty-development marker.

## Controls and behaviour

- Arrows: D-pad. Z: Y, X/Enter: B, C: A, V: X, A/S: L/R.
- Space: Start. Escape/Select: browser pause; Select stays available as a game
  input in Hardware Test. The page also offers Pause, Restart and Full screen.
- Standard-mapped browser gamepads and touch controls are supported. Click the
  screen or press a keyboard key once to enable browser audio.
- Rendering is 320×240 with the normal 288×216 playable area and black border.
  Simulation runs at 60 ticks/second; catch-up is bounded after slow frames.
  Losing focus or hiding the tab pauses play; Resume continues it.
- The browser launcher includes both games and Hardware Test. Pi-specific
  display calibration, controller setup, power management and diagnostics stay
  in the native host. Browser controls do not change the Pi's mappings.
- No progress/save-state persistence is added; restarting starts a fresh game.

Built and checked with Emscripten 3.1.5 and Chromium. Physical gamepads, touch
devices and other browser engines require device testing.
