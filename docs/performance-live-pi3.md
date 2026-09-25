# First live profiler measurements

Session date: 25 September 2026 (workstation). The Pi's wall clock reported
24 April 2026; it was not changed. All duration measurements use monotonic or
thread CPU clocks, not calendar time.

## Deployment and validation

Deployed the working-tree profiler based on `f39e4f81069a1b607f25074b22199b30b97ab609`.
Built the native host and all game modules in a separate Pi staging directory,
then stopped the host, installed the matching ABI 8 binaries and source changes,
and restarted it. Existing calibration, configuration and assets were preserved.
Native trace, report and FIFO capture tests passed on the Pi. The host and GPU
collector remained active after recording; the final status had profiling off.

The pre-deployment backup is `/home/retro/chirky-before-profile.oz0LYj/rollback.tgz`.
Staging is `/home/retro/chirky-profile-stage.q24us7`.
Initial deployment payload SHA-256:
`8cf6f29216773d19e91e19ae882d0aa18401a893a4a93c4c9b2e5fc602189aa2`.
The capture/report scripts received two subsequent fixes described below.

Pi output was 320x240 with a 298x220 playable viewport and a 16.661 ms refresh
budget. Kernel: 6.12.75+rpt-rpi-v8, aarch64. CPU governor: `ondemand`.
Observed ARM frequencies included 700, 800 and 1,400 MHz. A V3D spot check was
300 MHz. Temperature spot checks were 52.6-54.8 C; throttling status was `0x0`.
Frequency was not fixed, and spot checks are not continuous telemetry.

## Captures

All timings below are milliseconds. CPU means include the main-thread frame
scope, including CPU spent processing events during presentation waits. GPU
values are whole-frame kernel estimates over valid samples only.

| Capture | Frames | CPU mean | CPU p95 | GPU mean | Valid GPU frames | Missed refreshes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Launcher | 300 | 8.529 | 9.236 | 10.886 | 286 | 275 |
| Phosphor title | 300 | 10.317 | 12.393 | 16.449 | 292 | 300 |
| Preliminary gameplay | 360 | 2.579 | 3.902 | 0.639 | 354 | 0 |
| Main gameplay, 29.99 seconds | 1,800 | 4.992 | 7.537 | 1.775 | 1,791 | 0 |

The main gameplay capture remained in `scene.play`. Robot clips observed:
657 falling, 478 jumping, 337 running, 314 idle and 14 dashing frames.
It does not cover death, victory or level transitions. Preliminary gameplay
covered running, jumping, falling and idle. Differences between these gameplay
captures are not an optimisation comparison: world contents and actions differ.

Raw recordings on the Pi are in `/home/retro/chirky/build/live-profiles/`:
`launcher-01.json`, `title-01.json`, `title-overhead-on.json` and `gameplay-01.json`.
The third filename is historical: gameplay began during an intended title
overhead check. Local copies are `build/launcher-01.json`, `build/title-01.json`,
`build/gameplay-preliminary.json` and `build/gameplay-01.json` respectively.
These generated files are not tracked in Git.

## What is expensive

Main gameplay CPU breakdown, inclusive per frame:

| Scope | Mean | p95 | Maximum |
| --- | ---: | ---: | ---: |
| World drawing | 1.945 | 2.548 | 3.686 |
| Robot, total | 1.487 | 1.836 | 3.195 |
| Robot rasterisation | 1.277 | 1.581 | 2.093 |
| Final renderer flush | 0.532 | 1.756 | 4.427 |
| Update | 0.171 | 0.072 | 5.054 |

Rasterisation accounts for about 86% of the robot's mean CPU cost. The robot
accounts for about 30% of mean frame CPU; world drawing accounts for about 39%.
Inclusive rows overlap, and a flush can occur inside another drawing scope.
Do not add all rows together or interpret their CPU costs as GPU attribution.

The highest-CPU frame was #1149 at 14.005 ms. Its update was 4.317 ms, world
drawing 3.686 ms, robot 1.902 ms, HUD 1.071 ms and final flush 1.744 ms.
The GPU estimate was 2.478 ms and DRM reported no missed refresh. CPU and GPU
can overlap; summing them does not establish a presentation deadline margin.
Several other high-CPU frames also had roughly 4 ms update spikes. The current
update scope is too broad to establish their cause. Audio dispatch is one
candidate to instrument, not a measured explanation.

The title averaged 35,224 rectangle submissions per frame and ran at 30 fps.
The launcher averaged 21,133 rectangles and frequently missed a refresh. Neither
capture contains robot rendering. Their rendering cost and the previously
identified scheduling behaviour are separate from robot performance.

## Profiler overhead and fixes

A Pi microbenchmark of 50,000 empty scope pairs per pass measured 2.805 us/pair
on the first pass and 2.585-2.591 us/pair on subsequent passes. The main gameplay
capture has 24 scopes per frame, implying roughly 0.062-0.067 ms of direct scope
instrumentation under those microbenchmark conditions. This estimate excludes
GPU collector work, export and changed cache/governor behaviour. GPU snapshot
processing within the main capture averaged another 0.028 ms per frame.

An off/on/off utilisation comparison was attempted, but the scene changed from
title to gameplay and ARM frequency varied. It is not a controlled overhead
estimate and must not be interpreted as a speedup or slowdown. Metadata is kept
as `build/overhead-check.json` locally and `title-overhead-check.json` on the Pi.

The initial output directory, `build/performance`, was owned by an older
privileged benchmark run. The host recording survived in `run/profile.json` and
was recovered. Live captures now use `build/live-profiles`; the capture command
tests directory write access before recording. A regression test covers this.

The report now lists highest-CPU frames separately from frames ranked by missed
refreshes and elapsed time. This exposes workload spikes that still present on
time; otherwise ordinary display waits can dominate the displayed worst frames.
Report regression tests pass, including this separate ranking.

## Next decisions

1. Investigate title/launcher rendering and frame scheduling: measured misses
   occur there without a robot.
2. Add narrower update scopes, especially audio dispatch and reset/loading work,
   to explain the occasional 4-5 ms CPU spikes.
3. Keep the robot for now. If more gameplay headroom is needed, its software
   rasteriser is the measured target; world drawing also warrants investigation.
4. Capture death, level transitions and a longer play session before concluding
   that gameplay always meets its deadline. Repeat a controlled overhead check
   with a fixed scene and recorded frequency before relying on small differences.
