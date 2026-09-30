# Chirky

**Chirky** (or **Chirky Box** can't decide) is a simple, low-latency games console written in c.

After visiting family in France and playing the original Mario on NES, I was amazed how smooth and instant the experience was. I wondered whether it was possible to recreate that old-school feeling on today's hardware.

So I made Chirky Box. I run it on a Raspberry Pi 3b+ connected to CRT monitor via scart HAT. I soldered a SNES controller to a Raspberry Pi Pico and connect that as USB joystick.

And it works great! I have a dashboard webapp for controlling the Raspberry PIs (which are connected via static IP over lan). I can edit games, levels and sprites.

All the games here are unfinished and very much a work in progress. I will hopefully improve them slowly over time as I get some myself.

The text below is the internal thought process of an AI agent or two.

---

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
See [the browser guide](web/README.md) for testing and
[web deployment](deploy/WEB.md) for publishing directly to chirky.org.
The browser and native launchers are generated from each game's `game.conf`;
there is no separate game list to maintain. See
[the architecture guide](docs/architecture.md) for the core/platform boundary.

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

Chirky exposes eight logical inputs: **Left, Right, Up, Down, Primary,
Secondary, Start and Menu**. Games use these names; physical controllers and
keyboards are mapped by the host. Both platforms use the same game-facing API.

| Chirky input | Default SNES controller | Default keyboard |
| --- | --- | --- |
| Arrows | D-pad | Arrow keys |
| Primary | B | X |
| Secondary | Y | Z |
| Start | Start | Enter |
| Menu | Select | Escape |

The Pi defaults match the tested SNES/Pico adapter. See the
[controller wiring guide](docs/snes-pico-usb-controller.md) for that hardware.

- Primary selects menu items; Secondary goes back. Menu pauses gameplay.
- Rosey Chop: Primary chops and Secondary jumps.
- Phosphor Run: Primary jumps, Secondary dashes, Start uses a life to retry.
- Bramble Hollow: Primary interacts/closes dialogue, Secondary cycles or uses
  a dialogue service, Start opens/closes world controls.
- Hardware Test: Primary toggles motion, Secondary plays sound, Menu leaves.
- Start, Primary or Secondary begins a title screen after inputs are released.
- F1 is native recovery/cancel, F12 captures a Pi snapshot. The physical
  Start + Select recovery chord remains available outside input setup.

Choose **Settings > Input Settings**, then **Map controller** or **Map keyboard**.
Both wizards capture the eight Chirky inputs in the order above, with Primary
before Secondary. Release inputs between prompts. Duplicate bindings are
rejected; completed mappings save atomically. F1 cancels keyboard setup; holding
two controller buttons for one second cancels controller setup. F1/F12 remain
reserved. Cancellation and failed saves preserve the previous mapping.

Green controller and gold keyboard indicators show both sources independently.
**Test buttons** suspends navigation; hold Secondary for one second or press F1
to return. Screen transitions consume the opening press and wait for two neutral
updates before accepting input on the next screen.

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
size and position; Back or Secondary restores both. Old configurations are centred
by default. Position is saved as `safe_offset_x` / `safe_offset_y`; drawing adds
these offsets to the existing margins, with no scaling or additional render pass.

`input_version=4` saves `bind_primary`, `key_primary`, `bind_secondary`,
`bind_start`, `bind_menu` and directional bindings. Existing SNES B/Y/Select
bindings migrate to Primary/Secondary/Menu; earlier action mappings still migrate.
Explicit new names take precedence. Conflicting new defaults are left unbound.
Retired A/X/L/R bindings are removed when saving. Host and all games must be
rebuilt together for ABI 13. `safe_x` and `safe_y` retain display margins.

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
selected with Primary; Secondary also goes back. Hardware Test uses Primary for
motion, Secondary for sound, and Menu to return.
Names support up to 24 characters using the CRT font. Cancel discards the draft.
Deployment preserves the Pi’s existing launcher configuration; use Apply to console
to change it. Missing or invalid configuration falls back to the default menu.

**Install project on Pi** copies and builds the laptop project, then restarts the
console. **Restart console software** uses the existing Pi build. Put local SSH key paths in
`dashboard/config.local.json`; that file is intentionally ignored by Git.

The dashboard and CRT launcher both include a deliberate Pi power-down action.
The dashboard asks for confirmation; the launcher keeps `POWER DOWN` separate
from the game list and activates it with Primary.

## Included games

The shared native/browser interface is documented in
[Platform API 12](docs/platform-api.md): retained assets, sprites, rectangles,
text, input, sound, timing scopes, and asynchronous world direction. See the
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
under `build/`. Portable console behaviour, menu rendering, input setup, game
lifecycle and viewport definitions live directly under `src/`. DRM, evdev,
ALSA, native networking, module loading and file persistence belong in
`src/platform/linux/`; Emscripten adapters belong in `src/platform/web/`, and
browser device APIs, storage and page presentation in `web/`. Both platforms
run the same console core. See [architecture](docs/architecture.md).
