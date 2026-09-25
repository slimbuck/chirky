# Platform API 9: Pi measurements

Measured 25 September 2026 (workstation date) on the existing Pi 3B+ and CRT,
320x240 output, 298x220 playable viewport, approximately 16.662 ms refresh.
The Pi wall clock was still incorrect; durations use monotonic/thread clocks.
Implementation and lifetime rules are in [platform-api.md](platform-api.md).

## Live title and launcher captures

Each row represents 300 complete frames, profiling overlay off. Before uses the
previous ABI8 host; after uses API9, the asset worker, and textured splash art.
CPU is the main-thread frame scope, including event processing during waits.
GPU is the existing whole-frame kernel estimate over valid samples, not a sum
of individual drawing scopes. CPU/GPU overlap; do not add these columns.

| Scene | CPU mean before / after (ms) | CPU p95 before / after (ms) | GPU mean before / after (ms) | Missed refreshes before / after |
| --- | ---: | ---: | ---: | ---: |
| Launcher | 7.125 / 0.860 | 9.020 / 1.312 | 10.402 / 0.832 | 154 / 0 |
| Phosphor title | 11.081 / 0.822 | 12.561 / 1.048 | 17.140 / 0.787 | 300 / 0 |
| Rosey title | 14.644 / 0.915 | 17.267 / 1.071 | 23.207 / 0.975 | 544 / 0 |

Valid GPU frames before/after: launcher 288/285, Phosphor 296/291, Rosey 295/287,
out of 300 each. Missing samples are unknown. All three after-captures presented
at 60 Hz without a missed refresh. This is not a claim of zero misses outside
those recordings: capture export, screenshots and initial loading can hitch.

Per-frame rectangle counts fell from 21,133 to 1,107 (launcher), 35,224 to 1,020
(Phosphor), and 47,456 to 1,498 (Rosey). Each now adds one textured sprite and
uses two batches. Artwork was not simplified. A real Pi framebuffer snapshot
of the Phosphor title was inspected after deployment.

Raw captures: `build/api9-before-*.json` and `build/api9-after-*.json` locally;
on the Pi they are under `/home/retro/chirky/build/live-profiles/`.

## Loading and audio

Prefetch logs report worker wall/CPU time, bytes read, resident payload, main
activation elapsed time, and total launch latency. These are per-load totals,
not a per-core scheduling trace. Game parsing and GPU upload still occur on
the main thread. Page caches were not flushed; none of these is a cold-SD test.

| Load | Worker wall / CPU (ms) | Main activation elapsed (ms) | Total to activation (ms) |
| --- | ---: | ---: | ---: |
| First Phosphor after deployment | 21.885 / 17.090 | 26.193 | 49.438 |
| Rosey with mixer already open | 5.815 / 3.891 | 1.671 | 10.262 |
| Phosphor reload with mixer open | 6.327 / 5.867 | 3.983 | 21.704 |
| Phosphor after fresh host, detailed capture | 8.504 / 8.126 | 12.534 | 35.768 |

Resident prefetch payloads were 1,398,145 bytes for Phosphor and 1,009,205 bytes
for Rosey. These exclude renderer/driver/module memory. The game bundle also
includes the launcher image and both raw and decoded entries where applicable.

The 180-frame warm reload capture (`api9-load-phosphor.json`) had no misses.
Two fresh-host captures each missed one refresh during first activation.
The detailed one, `api9-first-load-detail.json`, recorded:

| Scope | Calls | Elapsed (ms) | Main CPU (ms) |
| --- | ---: | ---: | ---: |
| assets.module | 1 | 0.806 | 0.808 |
| assets.game_init | 1 | 2.408 | 2.410 |
| audio.startup | 1 | 8.593 | 0.217 |
| assets.activate, inclusive | 1 | 12.529 | 4.137 |
| assets.upload, launcher / title | 2 | 2.869 / 1.540 | 2.787 / 1.481 |

Independent clock reads can differ by a few microseconds. The first activation
frame used 7.510 ms CPU and presented after 33.322 ms. Audio setup is already
performed by its own worker, but startup still waits for that worker. Making
startup readiness asynchronous is the clearest remaining loading improvement.

Hardware Test exercises the same native mixer through its legacy adapter. Its
300-frame capture (`api9-audio-hardware.json`) had eight enqueue calls at
16-26 microseconds of main CPU each, update maximum 0.142 ms, and no missed
refreshes. A subsequent 350-sample device check observed 12 RUNNING-to-idle-XRUN
cycles under the same mixer thread (TID 7832), with hardware pointers advancing
beyond the 3,600-frame beep into its silence tail on every cycle. This confirms
repeated playback and idle recovery, not just acceptance into the command queue.
This validates dispatch/device activity, not audible quality at the speaker.
Idle ALSA underruns are recovered on subsequent playback; they are not frame
timing measurements. No per-effect process is created by the production host.

## Gameplay rendering comparison

The existing real-scanout benchmark now accepts `legacy|assets` and a game id.
It verifies PLAY state before measuring a frozen initial gameplay frame.
Each run discards 12 warm-up frames and measures 180. Updates and sound are
deliberately excluded, so this is a renderer regression check, not live play.

| Frozen scene | Legacy CPU / GPU (ms) | Assets CPU / GPU (ms) | Misses, both runs |
| --- | ---: | ---: | ---: |
| Phosphor | 1.857 / 0.559 | 1.870 / 0.563 | 0 |
| Rosey | 0.960 / 1.275 | 0.939 / 1.271 | 0 |

All GPU frames were attributed, with no unmatched frames or trace overruns.
Both paths use the newly built game modules; `legacy` selects null asset API
callbacks and the old splash helper, not the entire historical executable.
Small differences are not evidence of a speedup/regression. The robot and world
renderers were not optimized. Earlier representative moving-gameplay results
remain in [performance-live-pi3.md](performance-live-pi3.md).

Example (temporarily owns the CRT; supervisor restores normal services):

```sh
sudo python3 tools/run-performance.py fresh-output-name 0 0 0 1 3 180 assets phosphor-run
sudo python3 tools/analyse-performance.py build/performance/fresh-output-name
```

Local and remote benchmark metadata/CSVs are in
`build/performance/api9-play-{phosphor,rosey}-{legacy,assets}/`. Full GPU traces
remain on the Pi. Governor was `ondemand`, not locked. Spot ARM clocks ranged
from 800 to 1,400 MHz; V3D was 300 MHz. Benchmark temperature checks were
55.3-57.5 C. `get_throttled` changed from `0x0` before staging/building to
`0x80000` afterward and remained there; no low-word current-limit bits were set
at these checks. Sampling is not continuous thermal/frequency telemetry.

## Validation and deployment

- Native and browser builds and `make test NODE=node.exe` pass in WSL.
- Asset ownership/cancellation and audio fake/null ALSA tests also pass on Pi.
- Both real game modules pass three load/play/unload cycles on Pi, including
  sounds, Phosphor transitions, Rosey win/replay, and no post-prefetch asset I/O.
- GLES2/WebGL1 pixel tests cover image orientation, clipping, alpha, batching,
  and exact integer scaling. Existing rectangle benchmark pixels are unchanged.
- Both browser games pass desktop/mobile-emulation checks for rendering,
  controls, sound, pause/mute, restart and texture cleanup. Results/screenshots
  are in `build/platform-browser/`. Only the unrelated favicon request is 404.

Builds are based on working tree `f39e4f81069a1b607f25074b22199b30b97ab609`.
All native modules were rebuilt for ABI9. Source/binary deployment preserved
Pi game assets, configuration and calibration. The host and collector are active;
the console was left on Phosphor's title with profiling off.

Rollback backup: `/home/retro/chirky-before-platform.QatZWu/rollback.tgz`.
Backup SHA256: `ccd98b4b3c923df73fb02f99f1cbfab4cb925cd0dfe45557416ef6a1e3c35ff9`.
Staging: `/home/retro/chirky-platform-stage.ZlOzo7`.
Initial installed payload SHA256:
`f794cbea1b276c989f17142ed69ef724f3b59f603c8ff84556b5c415ef2f9d3d`.
The benchmark then gained an explicit PLAY-state check, and the host gained
the detailed activation scopes above and was rebuilt/restarted. The backup
contains matching pre-API9 host/modules; restore those together while stopped.

## Follow-up: asynchronous audio startup

The native mixer now returns from start after creating its worker. Readiness and
worker setup wall/CPU time are reported separately. Sound commands can queue
during startup, and an unload during startup cancels them without waiting for
ALSA or retaining freed sample pointers. Failed devices are retried on next game
activation. No game ABI or browser playback changes were needed.

Three fresh-host Phosphor launches, 180 recorded frames each, overlay off:

| Run | audio.startup main elapsed / CPU (ms) | assets.activate elapsed (ms) | Missed refreshes |
| --- | ---: | ---: | ---: |
| 1 | 0.301 / 0.302 | 4.378 | 0 |
| 2 | 0.273 / 0.278 | 4.173 | 0 |
| 3 | 0.219 / 0.219 | 4.200 | 0 |

The earlier synchronous startup scope was 8.593 ms elapsed. The new scope
measures thread creation/dispatch, not completed device setup. Worker setup
was still about 9 ms (for example, run 3: 9.331 ms elapsed / 9.094 ms CPU),
but no longer delayed the activation frame. Maximum main CPU in the three
recordings was 8.653, 8.108 and 8.974 ms respectively. Clock frequencies were
not fixed. Captures are `build/async-audio-load-{1,2,3}.json` locally and in
the Pi's `build/live-profiles/` directory. The user's timing overlay was
restored after testing, with Phosphor active.

`make test` passes, including a deliberately blocked ALSA setup test: start
returns while setup is blocked, queued sample storage can be released after
reset, and a subsequent sound plays after readiness. Audio tests and the null
ALSA backend pass on Pi and under local ASan/UBSan. TSan was attempted but could
not start in this WSL environment (`unexpected memory mapping`), including a
non-PIE retry; it did not produce a race-test result for this follow-up.

Rollback: `/home/retro/chirky-before-async-audio.sxt3Dx/rollback.tgz` contains
the prior host binary and changed existing source/test files. Game assets,
configuration and calibration were not modified.

### Pickup sound investigation

The reported Pi pickup difference was initially investigated in isolation. Its WAV is
identical locally, on Pi, and in the pre-platform backup (SHA256
`915b2ba3a595bbbdf37b6f3b6dd99633e44e6566b3e492c004df5b8dbcc49d4b`).
It is 7,680 stereo PCM16 frames at 48 kHz (160 ms). A new regression test,
`tests/audio_samples.c`, verifies that all six shipped Phosphor WAVs are
bit-identical after asset decoding and isolated mixer rendering, preserving
their samples, amplitude, duration, and rate. This passes on Pi too.

The previous native `aplay` path dropped requests while a sound process was
still alive. The new native mixer allows concurrent effects, so a pickup can
overlap jump/dash audio that previously suppressed it. That is a verified
behavioral difference; at that point it was not yet a confirmed explanation of
what the user heard. The WAV and mixing policy were initially left unchanged.
The sample tests do not validate audible speaker output or runtime scheduling.

The user subsequently identified rapid diamond collections as louder and more
chaotic. Native and browser asset playback now ignore repeats of the same active
sample without restarting its attack; different effects can still overlap.
This prevents duplicate pickups stacking gain without changing the WAV or its
normal volume. It is intentionally not a return to the old global sound lockout.

Validation: the full native test suite passes, and focused tests pass on Pi,
including real null-ALSA integration. All six Phosphor samples remain bit-exact
when eight duplicate requests arrive every 5 ms throughout playback. Tests also
cover startup-queued repeats, distinct-sound mixing, completion/retrigger, and
browser pause/mute/voice limits. Local ASan/UBSan passes. Both games' desktop and
mobile-emulation browser checks pass (`build/platform-browser-repeat/`). Audible
confirmation on the user's Pi speakers is still pending.

Deployed host SHA256:
`bbb4fb65e1db848004c31f2f23d3a3307c6c83cc59b22ce5a257d581d45611ca`.
The running process image matches the installed binary; both host and GPU
collector are active, with Phosphor and the timing overlay restored. Assets,
configuration and calibration were preserved. Rollback backup:
`/home/retro/chirky-before-repeat-audio.9fqTZH/rollback.tgz`.

Next performance work: record representative moving play, death/replay and level
changes before choosing a robot/world optimisation. No new frame-profile results
are claimed for the duplicate-sound change.
