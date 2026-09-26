# Bramble Hollow Agent Guide

Read the repository-root `AGENTS.md` before changing this game.

## Runtime Artwork

Generated PNGs under `assets/artwork/` are source material only. The game loads
the final PAM atlases in `assets/`:

- `player.pam`: 4x4 cells, each 40x40 pixels, atlas size 160x160.
- `friends.pam`: 4x2 cells, each 50x45 pixels, atlas size 200x90.

All calls in `render.c` must draw player cells at 40x40 and friend cells at
50x45. Subject size differences belong inside transparent cell padding. Do not
resize a whole cell to make an NPC, bicycle, or bird appear larger or smaller.

The `*-polished.png` files under `assets/artwork/sources/` are the authoritative
sprite sources. `tools/clean-bramble-sprites.ps1` isolates each frame and makes
one nearest-neighbour reduction to its final size. Do not use the older concept
sheets, repeatedly resize assets, or replace polished art with crude procedural
geometry. Keep character feet on one baseline and pack cells with transparent
gutters.

`tools/clean-bramble-sprites.ps1` is the only supported runtime-atlas builder.
Do not edit `player.pam` or `friends.pam` by hand and do not add another sprite
generator. Treat the checked-in PAM files as approved visual references. A
future regeneration check should build to a temporary directory and require a
byte-for-byte match unless a human has approved an intentional visual change.

If atlas geometry changes, update all of these together:

- `tools/clean-bramble-sprites.ps1`
- `game.c` asset dimension validation
- `render.c` destination rectangles
- `tests/bramble_runtime.c`
- `tests/asset_store.c` shipped-asset expectation
- `ARTWORK.md`

The runtime test deliberately requires source and destination sprite rectangles
to match. Player frames must remain horizontally centered and share the same
feet line; this prevents the bear from jumping around during the walk cycle.
The bicycle test also requires matching lower wheel geometry. Wheels should be
drawn from shared circle primitives, never estimated independently by eye.

The cat's cheek whiskers are an identity feature, not optional noise. Its final
subject is 30x40 inside the common 50x45 friend cell, and the atlas builder
preserves three strokes on each cheek after reduction. Keep the semantic pixel
assertions in `tests/bramble_runtime.c` when changing that crop or palette.

## Visual Definition Of Done

Before describing a Bramble visual change as complete:

1. Run the official atlas builder.
2. Compare each affected polished source crop with its PAM cell at 10x nearest
   zoom and at native 1x size.
3. Inspect the affected sprite in the live browser, including animation and
   composed states such as the bear riding the bicycle.
4. Confirm the canvas is at a 320x240 integer display multiple and that the game
   is using the host-reported logical viewport.
5. Run the Bramble runtime test and the full repository test suite.
6. After a requested Pi deployment, launch Bramble, check `/api/status`, inspect
   a Pi snapshot, and compare local and remote atlas hashes.

Passing tests establish structural correctness, not artistic quality. Never
approve an asset solely because it loads, has transparent gutters, or passes a
hash check; compare source, runtime cell, live browser, and Pi output.

## World And Host Coordinates

Bramble world coordinates are converted to the logical viewport by subtracting
the camera. They must not include the physical CRT inset. The native host owns
the calibrated inset, while `web/host.c` supplies its browser equivalent.

The Pi's calibrated logical viewport may differ from the browser's 288x216
default. Layout should use the host-reported dimensions for clipping and UI,
while sprite pixels remain native-size.

## Living-World Director

The game can hot-load `runtime/director.conf`; static defaults live in
`assets/director.conf`. `tools/bramble-director.mjs` is an optional external LLM
director, not proof that an LLM is currently connected. Before claiming the LLM
is active, verify its process, credentials, output file, and successful runtime
reload.

Keep director output bounded to validated long-term, medium-term, and short-term
state. The deterministic game remains authoritative for movement, collision,
inventory, time, and other moment-to-moment mechanics.
