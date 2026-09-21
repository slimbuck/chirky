# Chirky Box in the browser

The existing C games compile to WebAssembly using Emscripten. `web/host.c`
implements the host API with WebGL, reusing `src/rect_renderer.c`, the pixel
font, splash loader and launcher wordmark. Games retain their normal C source
and file paths. Each game has its own module, avoiding native `dlopen` and
colliding game symbols. The Pi build and ABI remain compatible.

## Build and run

Install Emscripten (`emcc`), Make and Node.js. From the repository root:

```sh
make web
node dashboard/server.js
```

On Windows with Emscripten installed in WSL, run `wsl make web`, then run the
dashboard with Windows Node.js. Open <http://localhost:3030/play/>.

`build/web/` is also a standalone static site: serve that directory over HTTP
or HTTPS, with `.wasm` served as `application/wasm`. It needs no Pi or dashboard
API. Do not open `index.html` directly with `file://`.

The dashboard serves runtime assets directly from the working tree. **Restart**
reloads them without recompiling. The editor's **Save and play in browser**
button saves locally and starts the selected campaign level. C changes require
`make web`; standalone static-site assets must also be refreshed with `make web`.

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
