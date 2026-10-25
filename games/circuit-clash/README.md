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
air guard, throws, dedicated crouch/air hit reactions and knockdown animations remain future work.
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

Arthur's Quick and planted balance, in simulation frames at nominal 60 Hz:

| Move | Startup / active / recovery | Damage | Reach | On block, earliest contact |
| --- | --- | --- | --- | --- |
| Standing punch | 3 / 2 / 8 | 14 | 38 px | -5 |
| Standing kick | 6 / 3 / 12 | 23 | 41 px | -7 |
| Crouching punch | 3 / 2 / 6 | 10 | 32 px | -4 |
| Crouching kick | 6 / 3 / 12 | 18 | 36 px | -8 |
| Air punch | 3 / 3 / 7 | 12 | 41 px | -4 |
| Air kick | 5 / 4 / 10 | 20 | 41 px | -6 |

Kicks commit longer for more damage; punches recover sooner. The on-block
comparison is `blockstun - active - recovery`, assuming first-active-frame
contact. Later contact improves it, and landing can shorten an air move.
Actual punishes also depend on spacing, pushback and guard-release timing.
These are starting values, not a claim of final competitive balance.

- All six move prefixes (`punch`, `kick`, `crouch_punch`, `crouch_kick`,
  `air_punch`, `air_kick`) own fourteen independent fields: startup, active,
  recovery, visual followthrough, damage, reach, bottom/top, hitstun, blockstun,
  knockback, hit-stop, block hit-stop and recoil duration.
- Frame zero is the first startup frame; contact is possible for
  `startup <= action_tick < startup + active`. Zero startup hits immediately.
  Visual followthrough holds the strike pose into recovery without extending
  the hit window, total move duration or action lock. Standing punch adds five
  followthrough frames, crouching punch four and air punch four: their strike
  drawings stay visible for seven, six and seven frames. Standing/crouching kicks
  add five followthrough frames and air kick four, holding each extension for
  eight frames before retracting. Each move owns its
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
  it cannot cancel a zero-startup move. `block_release_ticks=2` is guard recovery.
- Crouch entry and exit each take two frames. Its smaller hurtbox applies
  immediately; attacking waits until the entry transition finishes.
- Jump uses one takeoff frame, approximately 39 px of rise, 23 frames from
  launch to touchdown and two landing frames. `gravity_fp8` controls ascent,
  `fall_gravity_fp8` descent, and `max_fall_speed_fp8` caps falling velocity.
  `jump_buffer_ticks=6` retains fresh Up presses; holding Up does not repeat.
- `air_attack_landing_ticks=4` applies to unfinished attacks on touchdown,
  taking the larger of that and normal landing recovery.
- Idle holds are 16/5/10/5 frames. Walk holds (4/3/5/3) are measured at
  full forward speed: each drawing covers `hold * walk_speed_fp8` eighth-pixels.
  Actual travel advances the cycle, so acceleration, retreat and collisions
  change pose progression. Retreat reverses the gait. Position remains integer
  at draw time; the existing one-pixel body dip is baked into the artwork.
- `push_off_ticks=2` holds the planted walk stance before the first passing foot.
  Movement begins immediately. Releasing movement or meeting an obstacle plants
  the wide-contact drawing for at least `stop_plant_ticks=2`, continuing while
  braking displacement remains. `stop_sink_ticks=2` then uses idle's low drawing,
  and `stop_rise_ticks=4` uses its rise drawing before restarting idle at neutral.
  Each visual hold may be zero. New movement interrupts settling, reversal starts
  a fresh push-off when velocity changes direction, and attacks, jumps, crouch,
  guard and hit reactions always take priority. These phases add no action lock.

Definitions use `key=value` and whole-line `#` comments. All fields are required;
unknown, duplicate, missing and invalid values fail initialization. Character
names are 1-23 printable ASCII bytes and atlas paths stay under `assets/`.
`content-schema.json` is generated from the C field list in `tuning.h` and
supplies the editor's ranges, move groups and cross-field constraints.

Characters now use `schema_version=10`; stages remain at 1. Migrate v9 by adding
`hit_reaction_style=1`, `hit_impact_ticks=2`, `hit_recover_ticks=2`, selecting the
28-cell atlas and setting the version to 10. Style 0 retains the borrowed poses;
1 is restrained and 2 exaggerated. Migrate v8 by adding
`hitstop_ticks`, `blockstop_ticks` and `recoil_ticks` for each of the six move
prefixes, then setting the version to 9. Zero for all three preserves the old
unfrozen, instantaneous knockback. Migrate v7 by adding
`push_off_ticks`, `stop_plant_ticks`, `stop_sink_ticks`, `stop_rise_ticks` and
setting the version to 8. Zero disables each transition hold; the walk still
uses travel rather than elapsed time. Migrate v6 by adding
`followthrough_ticks` for each of the six move prefixes (zero preserves the old
pose timing) and setting the version to 7. Migrate v5 by first adding
the full crouching move definitions, seven missing power/geometry fields for
each air move, and the three block fields, then select the 22-cell atlas.
Older versions also require walk durations, fall gravity, jump buffering and
the kick/kneel layout introduced in versions 2-4. Use Arthur's complete current
definition as a reference. No dashboard or network connection is needed to play.

## Contact feedback

All six moves share the same native/browser contact response. A hit applies
health/stun once, then holds both fighters for `hitstop_ticks` extra updates;
guarded contact uses `blockstop_ticks` and a miss never freezes. Position,
velocity, gravity, animation, attack, stun and round timers all stop. Fresh input
edges are collected during the freeze and offered once when play resumes; normal
action locks still apply. This is not an attack buffer through all of recovery.
Simultaneous hits use the larger freeze, preserving trades; reset clears it.

`knockback_px` is still total travel, scaled by the recipient's weight and guard.
`recoil_ticks` spreads that distance over a fast-starting quadratic ease-out,
using integer eighth-pixel arithmetic and integer sprite draws. Zero is immediate.
Duration is capped at `stun - 1`, so displacement finishes before control returns.
Walls consume blocked travel without storing a later burst. Air reactions retain
gravity outside hit-stop. Grounded hits now have two authored three-pose studies:
restrained and exaggerated, selected by the recipient's `hit_reaction_style`.
Impact holds throughout hit-stop and then for `hit_impact_ticks` stun updates.
Recoil fills the middle; recover uses the final `hit_recover_ticks`. Drawing
holds are capped to fit the existing hitstun, reserving one middle frame and one
recovery frame when stun is at least three updates. Shorter stuns may truncate
the sequence. This never extends stun or changes knockback. Style 0 retains the
earlier wide-contact/low/rise study. Guard, crouch and air keep their existing
poses. Every frame uses the same 96x96 native cell with integer drawing.

Arthur's standing punch starts with 3 hit-stop frames (1 on block), 8 px of recoil
over 6 frames, and 8 hitstun frames. Hit-stop adds real time equally to attacker
and defender; active-frame counts and on-block differences above exclude it.

## Animation workbench

Build the local preview with `wsl make animation-workbench NODE=node.exe`, then
open `/animation-workbench.html?game=circuit-clash` in the dashboard. The game
tools and Character balance page both link to it. This development module is
separate from the public player and is not installed on the Pi.

The initial study holds idle for 45 frames, walks forward for 18, releases
movement for 12, presses punch once, then returns to idle for 45. The sequence
allows the longer A/B attack, hit-stop and defender stun before the final idle hold. Sequence lengths are
editable; both takes always receive identical inputs. It isolates one fighter
with real wall limits, without an opponent's collision or combat reactions.

- Each A/B preview now contains an attacker and an opponent using that take's
  character settings. **Takes the hit**, **Blocks** and **Solo movement** select
  the setup; the shared starting gap is in pixels. **Try a miss** uses a 120 px
  gap. **Jump to contact** seeks to B's first hit or block event without guessing
  a frame. Gap and scenario are shared preview settings, not saved character data.
- Separate P1/P2 motion and pose tracks plus a contact/hit-stop track share one
  time ruler. Readouts show health, stun, recoil progression and freeze remaining.
  The contact frame is followed by the configured number of extra frozen samples.
- Punch contact controls tune hit-stop, block hit-stop, knockback distance,
  recoil duration, hitstun and blockstun. The Hit reaction artwork group compares
  borrowed, restrained and exaggerated drawings with editable impact/recovery
  holds, independently of combat timings. Pre-v10 takes retain borrowed poses.
  Every numeric character field is retained
  in named takes; the balance editor exposes the equivalent fields for all moves.
- B is the working draft: ground speed, acceleration/braking, push-off and
  plant/sink/rise frames, punch phases, follow-through, and each idle/walk
  drawing's hold can be changed immediately. The motion timeline distinguishes
  push-off, walk, plant/brake, sink and rise from the actual drawing names.
- A starts from the character loaded from disk. Compare side by side, or choose
  a named take as A. Saving B does not replace A during that session.
- Pause, single-frame step, scrub or slow playback to 1/2, 1/4 or 1/10 speed.
  The input, motion-phase and sprite timelines share the same frame cursor.
- Onion skins show samples two frames before/after the cursor, where available.
  Ground/anchor guides expose foot sliding; hitboxes distinguish active frames
  from visual follow-through. Positions and velocities are reported in pixels.
- Live controls record the same arrows and N/M input into both takes. The
  on-screen buttons support touch and multiple simultaneous inputs. Scrubbing
  replays the recording; Restart clears it. Recording pauses at 600 frames.
- Named takes are stored in this browser, scoped to game, character and schema
  version (up to 20). Keep a take before discarding or reloading an unsaved draft.
  v7/v8 takes remain available in v9: their values are retained and missing
  visual/contact fields are inherited from the current character when loaded.
  They use the current simulation, not a recording of an older engine.
- Only **Save character locally** writes character data, using the same
  validation and stale-write checks as the balance editor. Reload the game to
  use saved settings; deploying remains separate.

`tools/circuit-workbench.c` compiles the actual game simulation and pose selector
into a small development WASM module. It captures the game's own sprite draw
rectangle, source cell and facing. The dashboard displays those existing atlas
pixels at 1:1 inside the native viewport; CSS presentation uses integer physical
pixel scales. Movement and pose selection are not duplicated in JavaScript.
Each A/B instance has isolated simulation state. Deterministic reset/replay
makes this useful for timing work; it is not a claim of platform-wide replay
determinism or an implementation of a different character renderer.

Verify with `node tools/animation-workbench-browser.cjs`. Native adapter checks
also run under `make test`.

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
The hit-reaction import is part of the same build tool. Generated sources are
retained in `output/arthur-hit-v1/source/` (local, not needed for ordinary builds).
To re-import them for review, with Pillow, NumPy and SciPy installed:

```sh
python tools/build-circuit-art.py --import-hit-sources output/arthur-hit-v1/source --output output/arthur-hit-v1/review
```

`art/arthur/hit-reduction.json` records the established sampling grid and foot
registration. The importer translates source frames to that shared anchor,
reduces once into the approved palette and preserves native lower-leg pixels
from row 68. The torso, arms, expression and upper legs are generated artwork;
no runtime resampling or extra body bob is applied. Review both 1x and enlarged
sheets before replacing the named native sources and rebuilding normally.
Prompts are in `art/arthur/hit-generation-prompts.json`. The two candidate sheets
are retained as visual references; tests compare the atlas to those exact pixels
and separately protect the planted feet. Choosing a style changes character data,
not the atlas. The existing 22 frames are byte-for-byte unchanged.

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
