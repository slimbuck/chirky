# Live frame profiling

See [the first live Pi measurements](performance-live-pi3.md) for deployment
validation, gameplay costs, spike examples and measurement limitations.

Build the host and all games together (`make`): the profiling callback arrived
in ABI8; the current asset/sprite API uses ABI9. Rebuild browser modules with `make web` too.
An older running host does not support the capture command.

On the Pi, from its checkout, start a capture while playing normally:

```sh
python3 tools/capture-profile.py build/live-profiles/play-01.json --frames 600
```

This records the next 600 complete frames, prints a summary, and preserves the
full trace. At 60fps that is about ten seconds; at 30fps it is twenty seconds.
The command neither restarts the host nor supplies input. Choose a new filename
for each run. The maximum is 1,800 frames; the default timeout is 180 seconds.
Only one capture can run at a time. Timeout does not cancel an in-progress capture.

The same command can be run over SSH. Copy its JSON back to this checkout to
analyse it locally (Python standard library only):

```sh
python3 tools/profile-report.py build/live-profiles/play-01.json
python3 tools/profile-report.py build/live-profiles/play-01.json --json
```

The capture is also a Chrome Trace Event JSON file. Its nested duration events
can be opened in a compatible timeline viewer. Event arguments contain frame
number, active CPU microseconds, rectangle calls, presentation interval, missed
refreshes, and whole-frame GPU microseconds when available. GPU totals are event
arguments, not invented GPU execution spans. The command-line report is sufficient
to compare captures without a viewer.

## What the report measures

- CPU scopes use main-thread CPU time independently of elapsed wall time.
  `present.wait` makes display waits visible without counting sleep as CPU work.
- The host records update, render, final rectangle flush, EGL swap, GBM buffer
  acquisition, submission and presentation wait. Input/control handling during
  a presentation wait contributes CPU time to that scope.
- Phosphor records scene state, background, world, particles, player, and HUD.
  The robot records animation clip, pose, transform, rasterisation, resolve and
  rectangle submission. Names are copied so unloading a game cannot invalidate
  a recording.
- Rosey records scene state, world, rain, HUD and result drawing. The platform
  records `assets.activate`, `assets.module`, `assets.game_init`, `assets.upload`,
  `audio.startup` and `audio.enqueue`. The report includes total sprites and
  batches per frame. See [API9 measurements](performance-platform-pi3.md).
- Inclusive CPU includes children; self CPU subtracts direct children. Do not
  add inclusive rows together. Statistics aggregate each scope per frame and
  include zero for frames where it is absent; a rare animation can have zero p95.
- Worst frames are ranked by missed refreshes, then elapsed time, with their
  scene and largest self CPU costs. Misses use DRM presentation sequence gaps.
  A separate highest-CPU list exposes workload spikes even when presentation
  remains on time and the elapsed ranking is dominated by display waits.
  Recorded wall time ends after handling the flip and releasing the old buffer;
  it is not a physical input-to-photon measurement.
- Whole-frame GPU timings use asynchronous elapsed queries when supported,
  otherwise the existing VC4 collector's kernel estimate. Capture activates the
  collector without showing the overlay. Kernel estimates include dispatch/IRQ
  latency and merge overlapping jobs. Missing, foreign, cross-frame or lost jobs
  are rejected by the existing attribution checks. Valid coverage is reported;
  unavailable samples are `null`, never zero. No GPU completion waits are added.
- Drawing scopes measure CPU construction/submission, not individual GPU costs.
  Batches can span several scopes and may flush inside a scope. Rectangle counts
  help explain workload but are not GPU time. Use the existing controlled GPU
  benchmark when investigating GPU cost or scheduling.

## Capture overhead and limits

Disabled game hooks are null checks; no clocks or allocations occur in them.
Enabled capture uses a preallocated bounded buffer (about 6 MB at 600 frames),
two clock reads per scope boundary, and GPU snapshot reads at most ten times
per second. The collector adds its own overhead. The `profiler.gpu_poll` scope
exposes snapshot processing cost. Clock resolution does not establish accuracy.

The host continues playing for 300 ms after the requested frames to collect late
GPU results, then writes the file atomically. A file-write hitch can occur after
the recording. Capture allocation can also affect the frame before recording
starts; that gap can affect the first recorded presentation interval even though
allocation is outside the recorded CPU scopes. Do not compare tiny timing
differences without repeat captures and checking instrumentation overhead.
Buffer overflow, unbalanced scopes or interrupted presentation invalidate the
capture; the report rejects it. Abrupt host termination can leave no output.

For a useful baseline, capture the title, stationary play, movement/jumping,
dashing, death/restart and level transitions separately. Keep the same overlay
setting, scene and hardware conditions between comparisons. Record the build
revision and Pi temperature/throttling alongside captures. These tools expose
costs; measurements still need representative play before choosing an optimisation.
