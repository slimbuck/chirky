# Circuit Clash

A local fighting-game prototype with Arthur's authored pixel art, six attacks,
standing/crouching guard, solo practice and two independent local players.
Native Pi and browser use the same portable game code and tuning definitions.

## Play

Select Circuit Clash in the launcher. Release the launch button, then press
Primary or Secondary to join. After joining alone, press Start for practice
against a stationary dummy; Start resets the practice round. Alternatively,
join a second input source. Both players press Primary to rematch after a KO.

Default keyboard: P1 arrows + N/M; P2 WASD + F/G. Enter is Start, Escape is Menu.
Controller and browser touch controls use the same remappable logical actions.

| Action | Control | Behaviour |
| --- | --- | --- |
| Walk | Left/right | Acceleration, braking and slower retreat |
| Jump | Fresh Up press | Quick launch, tucked rise, heavier descent and buffered input |
| Crouch | Hold Down | One-knee pose and lower collision body |
| Punch | Primary | Quick attack; different moves standing, crouching and airborne |
| Kick | Secondary | Slower, stronger attack in each stance |
| Block | Hold Primary + Secondary | Stationary frontal guard; add Down for crouching guard |
| Practice reset | Start | Resets health, positions and countdown |
| Pause | Menu | Shared console pause |

Backwards movement no longer blocks. The two-button chord tolerates slightly
staggered presses by cancelling only the first configured startup frames,
strictly before the attack becomes active. Active frames and recovery cannot
be cancelled into guard. Releasing guard briefly locks actions; releasing one
button does not attack. A fresh press is required.

Crouching attacks remain low for their full duration, including the collision
body. Air attacks use tucked legs and retain steering, momentum and gravity.
Their hitboxes follow their height, allowing air-to-air contacts. Touchdown
cancels an unfinished air attack before contact resolution and applies landing
recovery. One press gives one strike; another fresh press is allowed after
recovery. Hitstun interrupts attacks and cannot be converted into guard.

Holding guard continues protection through blockstun. Standing and crouching
guard currently cover all attack heights, without chip damage. High/low mixups,
air guard, throws, hit-reaction and knockdown animations remain future work.
Contacts resolve simultaneously, allowing trades and double KOs. Missing input
devices freeze the round and allow an unassigned device to claim the vacant
slot, followed by release and a resume countdown.

## Character balance

Open **Edit games / Circuit Clash / Character balance** in the dashboard, or
`/balance.html?game=circuit-clash`. It lists every character definition and
shows all six moves side by side. Numeric controls cover timings, damage,
reach, hitbox height, stun, knockback, movement, weight, guard and animation.
Saves stay local, preserve comments, validate ranges/cross-field rules and
reject stale writes. Restart the browser game after saving. Pi and public-site
updates still require explicit deployment.

`game.conf` selects `player1_character`, `player2_character` and `stage` by ID.
Definitions live in `assets/characters/<id>.conf` and `assets/stages/<id>.conf`.
Both player slots load independent values. Duplicate a character definition
and select its ID in game settings to compare different balances. It can share
Arthur's atlas. Character creation/roster selection and level-editor UIs are
future work; this prototype has one flat arena.

Starting Arthur balance, in simulation frames at nominal 60 Hz:

| Move | Startup / active / recovery | Damage | Reach | On block, earliest contact |
| --- | --- | --- | --- | --- |
| Standing punch | 4 / 2 / 8 | 14 | 38 px | -5 |
| Standing kick | 7 / 3 / 14 | 23 | 41 px | -9 |
| Crouching punch | 3 / 2 / 7 | 10 | 32 px | -5 |
| Crouching kick | 8 / 3 / 15 | 18 | 36 px | -11 |
| Air punch | 4 / 3 / 9 | 12 | 41 px | -6 |
| Air kick | 7 / 4 / 12 | 20 | 41 px | -8 |

Kicks commit longer for more damage; punches recover sooner. The on-block
comparison is `blockstun - active - recovery`, assuming first-active-frame
contact. Later contact improves it, and landing can shorten an air move.
Actual punishes also depend on spacing, pushback and guard-release timing.
These are starting values, not a claim of final competitive balance.

- All six move prefixes (`punch`, `kick`, `crouch_punch`, `crouch_kick`,
  `air_punch`, `air_kick`) own eleven independent fields: startup, active,
  recovery, visual followthrough, damage, reach, bottom/top, hitstun, blockstun and knockback.
- Frame zero is the first startup frame; contact is possible for
  `startup <= action_tick < startup + active`. Zero startup hits immediately.
  Visual followthrough holds the strike pose into recovery without extending
  the hit window, total move duration or action lock. Standing/crouching punches
  add four followthrough frames and air punch adds three: each strike drawing
  stays visible for six frames. Kicks currently add zero. Each move owns its
  own `followthrough_ticks`, which must be smaller than its recovery so at least
  one frame remains for retraction. Interrupted moves still leave the pose immediately.
- The editor presents all timings as frames. Existing `_ticks` keys count these
  same simulation frames, nominally 60 per second. Native currently
  follows display presentation; cross-platform replay determinism is not claimed.
- `_px` measures framebuffer pixels. Reach starts at the body centre; hitbox
  bottom/top are relative to the fighter's ground origin, including in the air.
- `_fp8` is eighths of a framebuffer pixel: velocity per update, acceleration
  per update squared. Rendering rounds positions to integers.
- `weight_percent` scales received knockback by `100 / weight_percent`.
  `block_knockback_percent=50` halves it again when guarding.
- `block_chord_ticks=2` is the early-startup allowance for the second button;
  it cannot cancel a zero-startup move. `block_release_ticks=3` is guard recovery.
- Jump uses one takeoff frame, approximately 39 px of rise, 23 frames from
  launch to touchdown and three landing frames. `gravity_fp8` controls ascent,
  `fall_gravity_fp8` descent, and `max_fall_speed_fp8` caps falling velocity.
  `jump_buffer_ticks=6` retains fresh Up presses; holding Up does not repeat.
- `air_attack_landing_ticks=5` applies to unfinished attacks on touchdown,
  taking the larger of that and normal landing recovery.
- Idle holds are 12/10/13/10 frames; walk holds are 10/4/10/4. Retreat reverses
  the gait, which restarts on direction changes and stops at walls. The walk's
  one-pixel body dip is baked into the art; no extra runtime bob is added.

Definitions use `key=value` and whole-line `#` comments. All fields are required;
unknown, duplicate, missing and invalid values fail initialization. Character
names are 1-23 printable ASCII bytes and atlas paths stay under `assets/`.
`content-schema.json` is generated from the C field list in `tuning.h` and
supplies the editor's ranges, move groups and cross-field constraints.

Characters now use `schema_version=7`; stages remain at 1. Migrate v6 by adding
`followthrough_ticks` for each of the six move prefixes (zero preserves the old
pose timing) and setting the version to 7. Migrate v5 by first adding
the full crouching move definitions, seven missing power/geometry fields for
each air move, and the three block fields, then select the 22-cell atlas.
Older versions also require walk durations, fall gravity, jump buffering and
the kick/kneel layout introduced in versions 2-4. Use Arthur's complete current
definition as a reference. No dashboard or network connection is needed to play.

## Native artwork pipeline

Animation stays short and sharp: held poses with quick transitions, following
the rhythm of the original Gorillaz *Clint Eastwood* video. The visible neutral
Arthur is 64 pixels tall. All source and destination cells are exactly 96x96,
with anchor (48,80), transparent gutters and grounded feet at row 79. Airborne
and lifted feet have explicit per-frame rows in the manifest. No runtime
scaling, interpolation or procedural redrawing is used.

The revised ground punch has a small shoulder drive; the kick has a pronounced
backward lean. `kick-proportioned-v4.png` ends at cell x88, between the earlier
long/short variants. Air punch/kick tuck the supporting legs. Crouching punch
and sweep stay low. Block poses close both arms in front of the face. White eye
glints and the approved character palette are preserved at the native grid.

One production command, using Python with Pillow:

```sh
python tools/build-circuit-art.py
```

It reads `art/clips.json`, copies the approved PNG cells into a 1536x192 RGBA
PAM atlas, and writes `art/manifest.json`, `animation_layout.h` and the tuning
schema. `--output <directory>` supports temporary, byte-compared regeneration.
The 16-column atlas keeps indices 0-15 in their original order: idle, walk,
punch, jump, kick, kneel. Row two holds air punch/kick, crouching punch/kick
and standing/crouching block at indices 16-21; unused cells are transparent.
Air/low attacks use jump/kneel guard drawings during startup and recovery,
and their distinct strike drawing during active frames plus configured visual
followthrough. Followthrough is part of recovery and cannot hit.

The exporter never regenerates AI art, resizes sprites or changes their palette.
Reviewed native source sheets and generation prompts live in `art/arthur/`.
High-resolution sources and the established grid/palette reduction experiments
live under ignored `output/`. Intentional art changes require reviewing the
native result and updating the source and manifest, not manually patching PAMs.

## Validation

```sh
wsl make test NODE=node.exe
wsl make web NODE=node.exe
node tools/circuit-clash-browser.cjs http://127.0.0.1:3030/play/
node tools/character-balance-browser.cjs
```

Portable tests cover input ownership, guard chords/recovery, all six attack
windows, crouching collision height, air movement/landing, trades, damage,
weight, per-character tuning and 1:1 sprite draws. Asset tests compare runtime
pixels with reviewed PNGs, dimensions, gutters, baselines and semantic details.
Browser tests use real WASM, both facings and fractional desktop/mobile sizes.
The balance editor tests saving, validation and stale-write protection. Physical
Pi/controller verification remains a separate deployment step.
