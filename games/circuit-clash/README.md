# Circuit Clash

A local fighting-game movement prototype with Arthur's approved 64-pixel-tall
idle, bobbing walk, tucked jump, punch, kick and kneeling guard, solo practice,
and two independent local players. Native Pi and
browser use the same game code and tuning files. It is not a finished fighter.

## Play

Select Circuit Clash in the launcher. Release the launch button, then press
Primary or Secondary to join. After joining alone, press Start for practice
against a stationary dummy; Start resets the practice round. Alternatively,
join a second input source for a local versus match.

Default keyboard: P1 arrows + N/M; P2 WASD + F/G. Enter is Start, Escape is Menu.
Bindings are remappable through the console. Controllers and browser touch use
the same logical actions. A device can only own one player slot.

| Action | Control | Current behaviour |
| --- | --- | --- |
| Walk | Left/right | Acceleration, braking, slower retreat; opposing directions cancel |
| Jump | Up, fresh press | Quick takeoff, tucked rise, heavier descent, buffered input and landing recovery |
| Kneel | Hold Down | One-knee guard; stops walking and lowers the collision body; transition timers lock attacks |
| Punch | Primary | Fast attack on the ground or in the air, with startup, contact window and recovery |
| Kick | Secondary | Stronger, compact side kick on the ground or in the air |
| Block | Hold away from opponent | Retreating guard; Down + away crouch-blocks; no chip damage |
| Reset practice | Start | Resets health, positions and countdown |
| Pause | Menu | Normal console pause, available to either player |

Punch and kick can start while rising or falling. Air attacks retain jump
momentum, steering and gravity, and use the approved attack drawings with
separate phase timings. Their hitboxes follow their height, allowing air-to-air
and jumping attacks against grounded opponents. Landing cancels an unfinished
air attack before contact resolution and applies landing recovery. Attacking from kneel rises into the standing
punch or kick, including the full standing collision body. Holding Down returns
to kneel after recovery; dedicated low attacks are future work. One
press gives one strike. Attack contacts resolve simultaneously, allowing trades
and double KOs. Both players press Primary to rematch; practice needs one.
Ground guard covers both grounded and aerial attacks; high/low mixups and air
blocking are future work. Missing devices freeze play, preserve the round, and allow an unassigned
source to claim the missing slot followed by release and a resume countdown.

## Character and arena tuning

`game.conf` selects `player1_character`, `player2_character` and `stage` by stable
ID. IDs resolve to `assets/characters/<id>.conf` and `assets/stages/<id>.conf`.
The two player slots load independent definitions even when both select Arthur.
To experiment with a different fighter's balance, duplicate Arthur's definition
under a new ID, adjust it, and select that ID for either slot. It may initially
share Arthur's atlas. Character selection UI is not implemented yet.

All character/stage fields are required and validated on load. Unknown,
duplicate, missing, out-of-range and nonnumeric numeric fields are rejected;
invalid content fails initialization instead of silently playing with defaults.
Characters use `schema_version=5`; stages remain at `schema_version=1`.
Version 2 added four required walk durations and the eleven-cell animation atlas.
Version 3 adds `fall_gravity_fp8`, `jump_buffer_ticks` and a twelfth cell for
the airborne tuck. To migrate v2, add the two fields, select a compatible atlas
and update the version. Version 4 adds three kick cells and one kneeling cell;
v3 characters need a compatible sixteen-cell atlas and a version update, with
no additional numeric fields. Version 5 adds `air_punch_startup_ticks`,
`air_punch_active_ticks`, `air_punch_recovery_ticks`, the corresponding three
`air_kick_*` fields, and `air_attack_landing_ticks`. To migrate v4, add these
seven fields and update the version; the sixteen-cell atlas is unchanged.
Tune crouching height to match the new pose
(Arthur uses 47 px for his 53-pixel-tall kneel). To migrate v1, also add `walk_0_ticks` through
`walk_3_ticks`. Files use `key=value` and
whole-line `#` comments. Character display names are 1–23 printable ASCII bytes;
atlas paths stay under the game's assets directory.

- `_ticks`: simulation updates, nominally 60 per second. Browser runs at 60 Hz;
  native currently follows display presentation, as documented by the platform.
  No claim of cross-platform replay determinism is made.
- `_px`: framebuffer pixels. Collision dimensions are independent of art padding.
- `_fp8`: eighths of a framebuffer pixel; velocities per update, acceleration
  and gravity per update squared. `walk_speed_fp8=12` means 1.5 px/update
  (90 px/second at 60 Hz). Drawing rounds to integer positions.
- `weight_percent`: received knockback multiplier is `100 / weight_percent`.
  200 means half normal knockback. Jump gravity is tuned separately.
- `idle_0_ticks` through `idle_3_ticks`: each drawing's duration. The approved
  200/160/220/160 ms preview is rounded to 12/10/13/10 updates. It is never used
  to determine an attack's startup, hit window or recovery.
- Punch/kick: startup, active and recovery durations; damage; horizontal reach
  from the body centre; bottom/top of the hitbox above the feet; hitstun,
  blockstun and knockback. Tick zero is the action's first startup tick; contact
  is possible for `startup <= action_tick < startup + active`. Zero startup
  allows immediate contact. Stun/recovery prevent starting a new action.
- `walk_0_ticks` through `walk_3_ticks`: durations of contact, lead lift, wide
  contact and rear follow. Arthur uses 10/4/10/4 ticks: held contacts with quick
  passing poses. Retreat traverses the drawings in reverse. The cycle restarts
  when movement starts or reverses, and stops at a wall or on interruption.
- Arthur's primary punch holds wind-up/strike/retract for 4/2/7 ticks, about
  0.22 seconds at 60 Hz. The strike drawing shares the exact active hit window;
  changing combat timing also changes pose timing. There is no interpolation.
- Secondary kick holds chamber/strike/recoil for 6/3/9 ticks, 0.30 seconds at
  60 Hz. Recoil reuses the chamber drawing. The extended leg is shown only
  during the active hit window. Kneel entry and exit retain their three-tick
  action locks; the silhouette changes immediately with the collision body.
- Air punch holds wind-up/strike/retract for 3/3/6 ticks; air kick uses 4/4/7.
  These six `air_punch_*`/`air_kick_*` phase fields can be tuned independently
  per character. Damage, reach, hitbox height, stun and knockback currently share
  the corresponding grounded move's values. Finishing recovery permits another
  fresh attack press in the same jump; holding a button never repeats it.
  Touchdown during an air attack cancels it and locks actions for the larger of
  `landing_ticks` and `air_attack_landing_ticks` (Arthur: four ticks). An attack
  that finishes before touchdown uses ordinary landing recovery. Hitstun
  interrupts air attacks. There is no air guard or attack-induced hover.
- Movement: forward/back speed, ground acceleration/braking, air speed/control,
  jump impulse/gravity/fall cap, takeoff, landing and crouch transition delays.
- Jump: Arthur has one takeoff tick, about 39 px of rise, 23 updates from launch
  to touchdown (about 0.38 seconds), and three landing ticks. `gravity_fp8`
  controls ascent; `fall_gravity_fp8` controls descent. `jump_buffer_ticks=6`
  retains a fresh Up press for up to 100 ms while waiting for a legal takeoff,
  including landing recovery. Zero disables buffering. Holding Up never repeats
  a jump. Receiving a hit clears the queued jump. Rise, apex, descent and landing
  drawings follow physics; the air pose does not stretch or move the sprite origin.
- Stage: floor height, wall inset, spawn percentages of the logical viewport,
  countdown duration. Stage geometry is game-local; the host still owns CRT
  safe-area centring. This prototype has one flat arena, not a tile level.

`content-schema.json` exposes field types/ranges and cross-field constraints for
future dashboard character/stage editors. It is generated from the same field
list as the C validator (`tuning.h`), with regression coverage against drift.
The existing dashboard can edit game settings; **character and stage editor UIs
are future work**. No dashboard or network service is needed during play.

For local balance changes, edit the relevant `.conf`, refresh the browser page,
and relaunch. Browser content is cached for the page lifetime, so pause/resume
alone does not reload tuning. C changes require `make web`; static packaged web
assets also need rebuilding. Pi changes use the dashboard's normal deployment
and reload path. Definitions are loaded once and stay fixed for a round/session.
Set `show_boxes=1` in `game.conf` to show collision bodies and active hitboxes.

## Artwork and remaining animation work

Animation direction: short, sharp limited animation, with strong held poses and
quick transitions, taking the original Gorillaz *Clint Eastwood* video as a
rhythm reference. Continue this approach for future moves; avoid soft tweening.

The shipped atlas preserves the approved four idle frames exactly, including
the one-pixel eye-highlight correction. Cells are 96x96 with transparent
padding; the visible neutral character is 64 pixels tall. Source texels and
destination pixels are always 1:1, with origin at (48,80). Grounded feet reach
row 79; lifted walk feet have explicit per-frame rows in the manifest. There
is no runtime scaling or additional bob. Walk contacts contain a one-pixel body
dip, with lower legs left in place. Revised punch/kick frames let the torso, shoulders, arms and head
move together: a pronounced backward lean counterbalances the kick, while the
punch has a smaller wind-up and forward shoulder drive. White eye highlights
are anchored to each moving face, rather than freezing the head in place.
Punch retains its previous native lower-body pixels from row 56 downward.
Kick v4 uses an intermediate leg length while retaining every other v2 pixel,
including the lean and support foot. Its toe ends at cell x88; gameplay reach is
41 px, nine pixels beyond v3 and five inside the original v2 kick.
Previous art remains as reference sheets. Shipped attack sources are
`punch-balanced-v2.png` and `kick-proportioned-v4.png`; the manifest records the
kick's exact edited region and extent for regression checks.

**Idle, walk, airborne tuck, punch, kick and kneel are authored.** Jump rises in the
tucked pose, extends into neutral on descent and uses low idle for the brief
takeoff/landing compression. Kneel uses a grounded one-knee guard with Arthur's
approved upper body moved down eleven whole pixels. His face, fists and headband
remain unchanged; the pose is retained through kneeling hit/block stun.
The state labels and optional boxes make these mechanics inspectable. Dedicated
air-specific attack silhouettes, standing block, low attacks, reaction and knockdown drawings are still
needed; the prototype does not procedurally squash or redraw Arthur.

One production export command (Python with Pillow installed):

```sh
python tools/build-circuit-art.py
```

It reads `art/clips.json`, copies the native idle, walk and punch PNG cells
plus jump, kick and kneel into the 1536x96 portable RGBA PAM atlas, then writes `art/manifest.json`,
`animation_layout.h` and `content-schema.json`. The layout assigns cells 0–3
to idle, 4–7 to walk, 8–10 to punch, 11 to jump, 12–14 to kick and 15 to kneel. It does **not** regenerate
AI artwork, resize the sprite or change its palette. `--output <directory>`
exports into a temporary directory for byte comparisons. `art/manifest.json`
records dimensions, frame order, baseline, hashes and pending clips. Intentional
art changes require reviewing and replacing the approved source, then exporting.
The generation prompts for all clips are retained in `art/arthur/*generation-prompts.json`;
exploratory high-resolution sources/reduction scripts remain in `output/` and
are not part of the runtime pipeline or the published package.

## Validation

```sh
wsl make test NODE=node.exe
wsl make web NODE=node.exe
node tools/circuit-clash-browser.cjs http://127.0.0.1:3030/play/
```

Tests cover device gates, disconnects/rejoins, rematches, practice reset,
movement, jump arc, crouch transitions, independent tuning, exact attack windows
and corresponding punch poses, configurable forward/reverse walk holds,
air attack timing/steering/contacts, anti-air interruption, landing cancellation,
blocking through blockstun, weight, collisions and 1:1 draws. Asset checks decode
the authored PNGs and byte-compare runtime pixels, both foot baselines, binary
alpha, gutters, the corrected highlight and preserved punch details. Browser
checks capture every walk/punch drawing in both facings and run real WASM with
simulated input devices; physical Pi/controller testing remains a separate step.
