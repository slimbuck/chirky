# Chirky

**Chirky** is the software behind **The Chirky Joybox**: a simple, low-latency Raspberry Pi CRT games console using the RGBerry SCART HAT.

Repository: [slimbuck/chirky](https://github.com/slimbuck/chirky).

The dashboard is the browser-based editing portal; the launcher is the menu
running on the Pi. Both use the Chirky name.

The persistent host opens the active DRM/KMS connector directly, creates a
GBM/EGL OpenGL ES surface at the connector's native resolution, and presents
each frame with a KMS page flip. It does not use X11, Wayland, SDL, or a
desktop compositor.

Rectangles and font pixels are batched as coloured triangles in their original
drawing order. A batch holds 4,096 rectangles; most scenes use one GPU draw.
The previous per-rectangle scissor/clear path remains only as a test/benchmark
reference. No changes to game artwork or the game ABI are needed for batching.

## Browser player

Run `make web` with Emscripten installed, then open
<http://localhost:3030/play/> with the dashboard running. The existing C games
run as WebAssembly, with keyboard, gamepad and touch input. The editing portal
includes **Play in browser** and **Save and play in browser**.
To preview the exact publishable build without the dashboard, run
`node tools/serve-web.cjs` after `make web` and open <http://127.0.0.1:3031/>.
See [the browser guide](web/README.md) for testing and the checked website export.

## Frame timing

For frame-by-frame CPU attribution, robot stage timings and correlated GPU
totals, see [live profiling](docs/live-profiling.md). A bounded capture of normal
play produces a summary and an exportable timeline; the overlay below remains
useful for a quick check.

The overlay starts disabled. Hold **Start for two seconds** to toggle it from
any screen (Enter with the default keyboard bindings). It has two running bars:

- **CPU**: main-thread CPU time for game updates, rendering and frame submission.
  Sleeping, blocked driver waits and waiting for the display flip are excluded.
- **GPU** (kernel estimate): approximate GPU time from VC4 kernel dispatch/completion events. All
  jobs within a completed host frame are combined; overlapping intervals are
  counted once. Interrupt and dispatch latency is included. If OpenGL elapsed
  timer queries are available, the label is **GPU** instead.

The overlay occupies **24 pixels**: two compact rows, down from 52 pixels.
Each row reads `CPU 3.1 A2.5 M4.8` (or `GPU`): latest time, **A**verage over the
last wall-clock second, and **M**aximum over that same second, all in milliseconds.
This is a time window, not a fixed frame count, so it remains one second when
frames are dropped. Unavailable/pending GPU samples are excluded; `--` means no
valid samples in the last second. GPU values on VC4 remain kernel estimates even
though the compact row label is simply `GPU`.

Both thin bars use the same fixed scale: zero to two display refresh periods
(about 33.3 ms at 60 Hz). The white tick marks the one-frame budget, **16.7 ms**,
also printed at the right of the GPU row. Current fill turns red above that
budget; the amber marker shows the maximum from the last second and falls again
when that sample expires. GPU samples arrive asynchronously, so displayed CPU
and GPU values need not describe the same frame. These bars measure workload,
not the complete presentation deadline; bars below budget do not guarantee 60 FPS.

The **D** counter at the right of the CPU row counts missed display refreshes
since enabling the overlay. Its background flashes red for one second on a new
miss. It uses actual DRM presentation sequence gaps. Toggle off/on to reset the
counter; averages and maxima roll continuously without needing a reset.

`deploy/install-service.sh` also installs the small root-owned
`chirky-gpu.service` collector. It uses a dedicated tracefs instance and a bounded
ring of atomically published snapshots in `/run/chirky-gpu/samples`, without waiting for GPU completion or collector results
in the game. The collector batches reads at 10 Hz and disables tracing while the overlay is
hidden. Collection adds some CPU/trace overhead. It requires Python 3 and the VC4 kernel tracepoints;
without it, CPU and dropped-frame diagnostics continue to work. The game itself
still runs as the normal unprivileged user. On other drivers, native OpenGL timer
queries are used when supported.

For remote diagnostics, send `timing on` or `timing off` to `run/control.fifo`.
Old `frame_timing` configuration entries are ignored. Work/submission time,
display intervals and dropped refreshes are still logged every 300 frames.

`tools/render_benchmark.c` compares the reference and batched paths on an offscreen
EGL surface without touching the CRT or the running game. On the connected Pi,
the initial Rosey Chop garden measured 20.8 ms versus 2.0 ms per render on average,
including GPU completion, with byte-identical framebuffer pixels. This is an
offscreen rendering comparison, not a claim that every live frame meets its deadline.
Run `make benchmark` on Linux/Pi to repeat the measurement and pixel comparison,
including a stress check that overflows the rectangle batch multiple times.

## Build on the Raspberry Pi

```sh
make
```

The build intentionally links to the versioned DRM/Mesa runtime libraries.
This lets the clean Raspberry Pi OS Lite image build the test without adding
development packages.

## Run

Run from SSH while the CRT is connected:

```sh
./build/chirky-host
```

The console exposes the SNES D-pad, **B, Y, A, X, L, R, Start and Select**.
Games read those buttons directly and choose their gameplay behavior. Labels
always name SNES buttons, including when playing with a keyboard.

- D-pad moves through the launcher; B selects; A goes back. Select pauses a game.
- Rosey Chop: B chops and Y jumps. Phosphor Run: B jumps, Y dashes and L restarts.
- Title screens wait indefinitely for a fresh face, shoulder, or Start button press.
- Select opens Pause: Continue Game or Return to Launcher. A goes back to the game; B selects.
- Pausing freezes gameplay and resumes the same run. B replays after a game ends.
- F1 is a keyboard recovery/cancel shortcut; F12 captures a snapshot outside setup.
- The physical Start + Select recovery chord is retained outside input setup.

Default keyboard emulation:

| SNES input | Keyboard key |
| --- | --- |
| D-pad | Arrow keys |
| Y | Z |
| B | X |
| A | C |
| X | S |
| L | A |
| R | D |
| Start | Enter |
| Select | Escape |

The launcher lists Phosphor Run, Rosey Chop, **Settings**, then **Power Down**.
Settings contains **Input Settings**, **Display Area**, and **Hardware Test**.

Choose **Settings > Input Settings**, then **Map SNES controller** or **Map keyboard to SNES**.
Both wizards ask for Left, Right, Up, Down, Y, B, A, X, L, R, Start and Select.
Screen changes consume the opening press and wait for two neutral frames before
accepting another. Holding B cannot open a submenu and activate its first item.
Release all inputs between prompts. Each physical input maps to one SNES button;
duplicates are rejected. Start and Select can be assigned during setup without
activating navigation. F1/F12 remain reserved. F1 cancels the draft; holding two
controller buttons for one second also cancels. Only a completed 12-input sequence
is saved atomically; cancellation or a failed save keeps the previous mapping.

The live display remains visible during mapping. Every SNES button has a green
controller indicator and a gold keyboard indicator, so simultaneous inputs from
both sources are visible. The PAD and KEY lines show held raw controller button
codes/axes and keyboard key names, including unmapped inputs; NONE means released
and MORE signals overflow. **Test buttons** suspends normal navigation so B and
Select can be tested too. Hold A for one second, or press F1, to leave testing.

Choose **Settings > Display Area** to calibrate CRT overscan. Up/Down selects Side Margin,
Top/Bottom Margin, Horizontal, Vertical, Save or Back; Left/Right adjusts the selected value. Keep all
four cyan edges visible. The defaults reserve 16 pixels per side and 12 at the top
and bottom, leaving a 288×216 playable area inside the physical 320×240 output.
Margins can be 0–32 horizontally and 0–24 vertically. All drawing is translated
and clipped to this area at native pixel size; game cameras and UI use its logical
dimensions. Back restores the previous area; Save persists it across restarts.
The dashboard's level editor uses the connected Pi's viewport for its guides.

Horizontal and Vertical move the whole safe region one native pixel per step
without changing its dimensions. Positive values move right/up; negative values
move left/down. The full region stays inside the 320×240 output: horizontal
movement is limited to ±Side Margin and vertical movement to ±Top/Bottom Margin.
Reducing a margin clamps the corresponding position if necessary. Save persists
size and position; Back or A restores both. Old configurations are centred
by default. Position is saved as `safe_offset_x` / `safe_offset_y`; drawing adds
these offsets to the existing margins, with no scaling or additional render pass.

`input_version=3` stores SNES mappings as `bind_b`, `key_b`, `bind_start`, etc.
Older mappings migrate on load: Jump becomes Y, Dash becomes B, Menu becomes
Select and keyboard Confirm becomes Start. B now handles both gameplay and
choosing items; a separate old controller Confirm binding is used only when Dash
was absent (except the obsolete Start default). Explicit SNES mappings take
precedence. New defaults that conflict with retained custom inputs are left
unbound until configured. Saving removes obsolete action keys. `safe_x` and
`safe_y` continue to store display margins.

The program uses `/dev/dri/card0` and reads Linux evdev keyboard devices under
`/dev/input`. The `retro` user is already a member of the `video`, `render`,
and `input` groups on the configured test Pi.

## Games, configuration, and assets

Each game owns one inspectable directory under `games/`. The first example is
`games/hardware-test/`:

- `game.conf` contains plain `key=value` settings
- `assets/colour-bars.ppm` is a plain-text image
- `assets/edge-beep.wav` is ordinary uncompressed audio

The WAV can be regenerated with `python tools/generate_test_assets.py`, or
replaced with any compatible WAV. Restart the program after changing config or
assets. The host also accepts a different config path as its first argument.

Snapshots are written as lossless PPM files under `snapshots/`. Press `F12`, or
send `SIGUSR1` to the running process. The latter is the dashboard integration
point:

```sh
pkill -USR1 -x chirky-host
```

## Dashboard

The local laptop dashboard has no third-party runtime dependencies:

```powershell
cd dashboard
npm start
```

Open `http://127.0.0.1:3030`. **Console** shows the snapshot, current state,
health, startup, installation, restart, power, and logs. **Launcher** mirrors the
CRT menu: games, Settings, and Power Down. Browsing its Settings submenu does
not change the CRT until a screen is selected. **Edit** lists the games and opens
a dedicated editing page for each.

**Edit launcher** changes menu names, order, and visibility. **Apply to console**
saves `config/launcher.conf` on both the laptop and Pi and reloads the menu without
restarting the running game. Each section must retain at least one visible item. Settings always includes a Back row,
selected with B; A also goes back. Hardware Test uses B for motion, X for sound,
and A to return.
Names support up to 24 characters using the CRT font. Cancel discards the draft.
Deployment preserves the Pi’s existing launcher configuration; use Apply to console
to change it. Missing or invalid configuration falls back to the default menu.

**Install project on Pi** copies and builds the laptop project, then restarts the
console. **Restart console software** uses the existing Pi build. Put local SSH key paths in
`dashboard/config.local.json`; that file is intentionally ignored by Git.

The dashboard and CRT launcher both include a deliberate Pi power-down action.
The dashboard asks for confirmation; the launcher keeps `POWER DOWN` separate
from the game list and activates it with B.

## Included games

The shared native/browser interface is documented in
[Platform API 9](docs/platform-api.md): retained assets, sprites, rectangles,
text, input, sound and timing scopes. See the
[Pi platform measurements](docs/performance-platform-pi3.md) for before/after
results and the asynchronous audio-startup follow-up.

- `rosey-chop`: clear the dead black roses from a colourful garden before the
  rainstorm, with sweeping chops, jumping, chasing wasps and a complete first level
- `hardware-test`: moving colour, motion, audio, input, and capture checks
- `phosphor-run`: a scrolling CRT-native platformer with wall-jumps, air dash,
  checkpoints, hazards, particles, collectible signal shards, and a complete
  win/death loop

## Start automatically on the Pi

After deploying the `deploy/` directory, run this once on the Pi:

```sh
cd /home/retro/chirky
sh deploy/install-service.sh
```

For a complete clean-card rebuild—including Trixie first-boot setup, RGBerry
timings, packages, network, software deployment, compilation, boot policy, and
validation—follow [provision/README.md](provision/README.md). The default
`config/host.conf` boots into the game launcher; the laptop dashboard is
optional once the Pi has been installed.


## Host regression tests

For CPU/GPU timing experiments on the actual Pi and CRT, see
[performance investigation and benchmark commands](docs/performance-pi3.md).
The benchmark captures per-frame CPU phases, asynchronous VC4 GPU jobs, and
missed display refreshes; it includes a controlled pipeline experiment.

`make test` checks the asset/editor, game and host input/layout behavior without
accessing DRM, input devices, or the live Pi. In WSL with Windows Node installed,
use `make test NODE=node.exe`. The host test writes software-rendered PPM previews
under `build/`. Host mapping and setup logic lives in `src/input_bindings.c`; evdev
routing, launcher screens, persistence and the safe viewport remain in `src/host.c`.
