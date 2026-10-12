# Circuit Clash

A local fighting-game movement prototype with Arthur's approved 64-pixel-tall
idle sprite, solo practice, and two independent local players. Native Pi and
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
| Jump | Up, fresh press | Configured takeoff delay, gravity, air steering and landing recovery |
| Crouch | Hold Down | Stops walking and lowers the collision body after selecting crouch; transition timers lock attacks |
| Punch | Primary | Shorter/faster attack with startup, contact window and recovery |
| Kick | Secondary | Slower attack with longer reach |
| Block | Hold away from opponent | Retreating guard; Down + away crouch-blocks; no chip damage |
| Reset practice | Start | Resets health, positions and countdown |
| Pause | Menu | Normal console pause, available to either player |

Attacks are grounded only for now; crouching uses the same two attacks. One
press gives one strike. Attack contacts resolve simultaneously, allowing trades
and double KOs. Both players press Primary to rematch; practice needs one.
Blocking covers both current attacks; high/low mixups and air attacks are future
work. Missing devices freeze play, preserve the round, and allow an unassigned
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
`schema_version=1` identifies the current format. Files use `key=value` and
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
- Movement: forward/back speed, ground acceleration/braking, air speed/control,
  jump impulse/gravity/fall cap, takeoff, landing and crouch transition delays.
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

The shipped atlas preserves the approved four idle frames exactly, including
the one-pixel eye-highlight correction. Cells are 96x96 with transparent
padding; the visible neutral character is 64 pixels tall. Source texels and
destination pixels are always 1:1; feet share row 79, with origin at (48,80).
There is no runtime scaling or added idle bob.

**Only idle is authored.** Walking currently plays the idle while translating;
jump and attacks hold the neutral pose; crouch/takeoff hold the low idle pose.
Crouch collision is shorter but the placeholder art is not yet a deep crouch.
The state labels and optional boxes make these mechanics inspectable. Dedicated
walk, jump, crouch, attack, block, reaction and knockdown drawings are still
needed; the prototype does not procedurally squash or redraw Arthur.

One production export command (Python with Pillow installed):

```sh
python tools/build-circuit-art.py
```

It copies `art/arthur/idle-approved.png` into the portable RGBA PAM atlas, then
writes `art/manifest.json` and `content-schema.json`. It does **not** regenerate
AI artwork, resize the sprite or change its palette. `--output <directory>`
exports into a temporary directory for byte comparisons. `art/manifest.json`
records dimensions, frame order, baseline, hashes and pending clips. Intentional
art changes require reviewing and replacing the approved source, then exporting.
The generation prompts are retained in `art/arthur/generation-prompts.json`;
exploratory high-resolution sources/reduction scripts remain in `output/` and
are not part of the runtime pipeline or the published package.

## Validation

```sh
wsl make test NODE=node.exe
wsl make web NODE=node.exe
node tools/circuit-clash-browser.cjs http://127.0.0.1:3030/play/
```

Tests cover device gates, disconnects/rejoins, rematches, practice reset,
movement, jump arc, crouch transitions, independent tuning, exact attack windows,
blocking through blockstun, weight, collisions and 1:1 draws. Asset checks decode
the approved PNG and byte-compare runtime pixels, both foot baselines, binary
alpha, gutters and the corrected highlight. Browser checks run real WASM with
simulated input devices; physical Pi/controller testing remains a separate step.
