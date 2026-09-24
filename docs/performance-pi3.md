# Pi 3 CPU/GPU investigation

Measured on the attached Raspberry Pi 3 Model B Plus and its real CRT output,
23 September 2026. The current scheduling explains dropped refreshes even when
both overlay bars individually show less than 16.7ms. Preparing the next frame
while the previous flip is pending improved the mixed test and launcher from
30fps to 60fps. This remains a benchmark experiment; the production host's
scheduling has not been changed by this investigation.

## Hardware and method

- VC4 V3D 2.1, Mesa 25.0.7, kernel 6.12.75+rpt-rpi-v8, 320×240 output.
- Measured refresh period: 16.662ms, approximately 60Hz.
- CPU: active host-thread time (`CLOCK_THREAD_CPUTIME_ID`) and separate elapsed
  wall time (`CLOCK_MONOTONIC`) at each phase. Clock-reported resolution was 1ns;
  that does **not** establish nanosecond accuracy. CSV values are microseconds.
- GPU: kernel VC4 submission, binning/render dispatch, and completion IRQ traces,
  joined to explicit application frame markers and job sequence numbers.
  The reported GPU span includes dispatch/IRQ overhead and any gap between
  binning and rendering. It is an approximation, not a hardware timer query.
  `gpu_busy_us` also reports the union of binning and rendering intervals.
- Presentation: actual DRM page-flip events and refresh sequence gaps. A missed
  refresh means the previous image remained on screen for another refresh;
  it does not mean a submitted GL frame was discarded.
- Twelve warmup frames per case, then 60 measured frames in the main sweep;
  follow-up cases use 120. The two smoke tests use 30 measured frames each.
- The normal host and overlay collector were stopped during each capture and
  restored afterwards. Tracing and phase markers have some observer overhead.
  These are short controlled tests, not a long interactive gameplay soak test.

All 3,300 frames across 40 cases passed attribution/completion checks; 3,311 GPU
jobs include each capture's initial scanout setup. No trace buffers overran.
The analyzer regression tests and full project regression suite also passed.

The main sweep stayed at 54.2–56.9°C and a sampled V3D clock of 300MHz. ARM
frequency varied with workload (including 800MHz, 900MHz and 1.4GHz), so a fixed
loop count does not guarantee identical CPU milliseconds between separate runs.
The recorded throttling value remained `0x80000`; its low status bits were zero.
Clock/governor settings were not changed for these tests.

## CPU timers respond to real work

A dependent integer loop prevents the compiler from removing the work. At the
higher, steady-load points, doubling iterations doubled active CPU time:

| Loop iterations | Work CPU | Work elapsed | Presented FPS |
|---:|---:|---:|---:|
| 1,808,312 | 7.82ms | 7.82ms | 60 |
| 3,616,624 | 15.62ms | 15.63ms | 60 |
| 5,424,936 | 23.43ms | 23.43ms | 30 |
| 7,233,248 | about 31.2ms | about 31.2ms | 30 |

A separate 10ms sleep measured **10.07ms elapsed but only 0.019ms active CPU**
in the work phase. This verifies why the CPU overlay intentionally excludes
waiting and cannot by itself represent the entire frame deadline.

## GPU timers predict the missed refreshes

The GPU test increases fragment-shader arithmetic on the same fullscreen draw,
then increases blended overdraw. Values below are measured means, not requested
sleep durations or estimates based on the amount of work.

| GPU work | CPU | GPU | FPS | Missed refreshes / measured frames |
|---|---:|---:|---:|---:|
| 8 shader iterations | 0.23ms | 3.30ms | 60 | 0 / 60 |
| 16 iterations | 0.25ms | 6.25ms | 60 | 0 / 60 |
| 32 iterations | 0.37ms | 12.14ms | 60 | 0 / 60 |
| 40 iterations | 0.24ms | 15.08ms | 60 | 0 / 120 |
| 44 iterations | 0.32ms | 16.55ms | 30 | 120 / 120 |
| 64 iterations | 0.46ms | 23.91ms | 30 | 60 / 60 |
| 128 iterations | 0.41ms | 47.48ms | 20 | 120 / 60 |
| 32 iterations × 8 layers | 0.52ms | 96.72ms | 10 | 300 / 60 |

At 44 iterations, even the GPU bar is individually below 16.662ms. But the CPU
submission work precedes most GPU execution, so readiness crosses the deadline.
The display must keep the old image until the following refresh. There is also
driver/display scheduling overhead; a workload exactly on the nominal budget
has no useful safety margin.

## What happens after submission

In the normal scheduling cases, GPU completion happened **after** the page-flip
submission call returned and **before** the corresponding flip callback. The
swap and GBM lock were short; traced VC4 sequence waits were zero. An explicit
`glFinish` control instead blocked for about 12.22ms on the 12.14ms GPU workload,
with about 12.15ms visible in the driver's sequence-wait trace. These controls
distinguish asynchronous submission from completion.

One measured frame in the 8ms CPU / 12ms GPU case:

| Event | Time since frame preparation began |
|---|---:|
| CPU work finished | 7.838ms |
| Draw calls returned | 7.959ms |
| GPU dispatch | 8.018ms |
| Page-flip submission returned | 8.102ms |
| GPU completed | 20.157ms |
| Display flip callback | 33.322ms |

The GPU is executing the image just submitted, not automatically a frame late.
Whether that image reaches the next display refresh depends on when it becomes
ready. Here it was too late for the first refresh. The current host waits for
the flip callback before starting the next CPU frame. There is no per-frame
`glFinish`, but this wait still prevents CPU preparation from overlapping the
previous frame's GPU execution.

The DRM event's supplied presentation timestamp was approximately **1.3ms ahead
of the application's callback clock** on this configuration. The captures keep
both values. Refresh sequence gaps and intervals are reliable for counting
misses; the report does not treat those timestamps as an exact photon-time
measurement or blindly subtract them from GPU trace timestamps.

## Pipeline experiment and latency

Mode 1 prepares frame N while frame N−1 has a pending flip, then retires N−1
before swapping and submitting N. Retiring before swap avoids exhausting GBM's
locked buffers. The trace confirms overlap between CPU N and GPU N−1.

| Main-sweep workload | Scheduling | CPU | GPU | FPS | Missed / 60 frames |
|---|---|---:|---:|---:|---:|
| Same fixed mixed work | Current | 8.09ms | 12.14ms | 30 | 60 |
| Same fixed mixed work | Pipeline | 8.08ms | 12.14ms | 60 | 0 |
| Same fixed mixed work | Explicit `glFinish` | 10.22ms | 12.14ms | 30 | 60 |
| Heavier mixed work | Current | 14.05ms | 23.91ms | 20 | 120 |
| Heavier mixed work | Pipeline | 12.01ms | 23.90ms | 30 | 60 |

The separate 120-frame repeat again produced 30fps/120 misses for current
scheduling and 60fps/zero misses for pipeline scheduling. CPU means were 10.22ms
and 8.09ms respectively because of frequency scaling; GPU means were both 12.14ms.

Rendering the actual launcher improved from 30fps to 60fps in the pipeline test
(GPU about 10.6–10.9ms). The Phosphor title stayed at 30fps, with GPU time about
17.4–17.9ms; scheduling cannot make that fit a 16.7ms refresh. A stationary
Phosphor gameplay scene rendered at 60fps with roughly 3.14ms CPU and 0.61ms GPU.
These scene tests exercise real rendering, but do not run continuous game
updates/input/audio, so they do not establish worst-case gameplay performance.

**Throughput and latency are separate.** On the light-GPU, 8ms-CPU case, both
schedules achieved 60fps, but preparation-to-flip-callback latency increased
from 16.66ms to 33.14ms with this simple pipeline. The mixed case improved
throughput while retaining roughly 33ms latency. Pipelined title rendering
reached roughly 66ms latency while still running at 30fps. A production pipeline
needs an explicit input-latency policy, not just an FPS comparison.

## Repeat the investigation

On the configured Pi, from `/home/retro/chirky`:

```sh
make performance-benchmark
sudo python3 tools/run-performance.py my-sweep
python3 tools/analyse-performance.py build/performance/my-sweep
```

Building does not take over the display. Running the supervisor temporarily
stops the normal host and collector, uses an isolated tracefs instance, then
restores the services, selected game and overlay setting. **The game's session
restarts.** Use a fresh output name for every capture; existing directories are
never overwritten. The supervisor enforces a seven-minute timeout and restores
services on errors or interruption. This trace backend requires VC4 tracepoints
and the configured systemd services; it is specific to this Pi setup.

Custom case arguments are `NAME MODE CPU_LOOPS SHADER_ITERATIONS LAYERS SCENE
[MEASURED_FRAMES]`:

```sh
# Current scheduling, then identical work with next-frame preparation:
sudo python3 tools/run-performance.py mixed-current 0 1808312 32 1 0 120
sudo python3 tools/run-performance.py mixed-pipeline 1 1808312 32 1 0 120
# Explicit GPU completion control:
sudo python3 tools/run-performance.py mixed-finish 2 1808312 32 1 0 120
```

Modes: 0 current, 1 pipeline, 2 explicit finish. Scenes: 0 synthetic shader,
1 launcher, 2 Phosphor title, 3 stationary Phosphor gameplay. Shader iterations
range from 0 to 128. CPU loops are fixed work, not milliseconds.

Each output directory contains:

- `frames.csv`: wall and active CPU timestamps for work, draw, finish, previous
  flip wait, swap, buffer lock, framebuffer lookup, and flip submission; display
  sequence/timestamp and callback time for every frame.
- `gpu-trace.txt`: raw frame markers, GPU jobs, and driver waits.
- `joined.csv`: per-frame phases joined to GPU execution, queue delay, overlap,
  latency and presentation intervals.
- `summary.csv`: means, medians, p95s and maxima, plus FPS and missed refreshes.
- `metadata.json`, `run.log`, `validation.json`: temperatures/clocks, captured
  status, timer resolution, trace loss checks and attribution validation.

The investigation captures are under `build/performance/`: `matrix-01`,
`repeat-serial`, `repeat-pipeline`, `repeat-finish`, `threshold-40`, `threshold-44`,
`launcher-pipeline`, `title-pipeline`, `latency-pipeline`, and the smoke captures.
Build outputs are intentionally untracked. Keep the raw CSV/trace files when
comparing later optimisations. The analyzer rejects incomplete GPU jobs, lost
trace events, missing frame attribution and GPU completion after its flip.

## What to optimise next

The compact two-bar overlay is a useful alarm, but not a complete explanation
of a deadline. Use the captures to distinguish CPU work, driver blocking, GPU
execution and display wait. In current scheduling, inspect the critical path
from CPU start to GPU completion; in pipelined scheduling, inspect overlap and
latency as well as the separate CPU/GPU budgets.

The clearest demonstrated opportunities are overlapping next-frame CPU work
with the previous GPU job, and reducing title-screen GPU rendering cost. For
individual render components, add CPU phase boundaries and compare controlled
component-on/component-off captures. Without native GPU timer queries, the
current trace measures whole GPU jobs; it cannot honestly assign a GPU duration
to each arbitrary draw call. The stationary gameplay result alone is not enough
to decide whether to remove or rewrite the robot renderer.
