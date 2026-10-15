# Console controls and Pi operation

See the [project README](../README.md) for builds and the browser quick start.
Controls and input setup are shared by both hosts; display calibration,
snapshots and Pi administration below apply to the native console.

## Default controls

Chirky exposes eight logical inputs: **Left, Right, Up, Down, Primary,
Secondary, Start and Menu**. Games use these names; physical controllers and
keyboards are mapped by the host. Both platforms use the same game-facing API.
On-screen prompts show the mapped physical control, rather than these internal
action names, and follow the last input device used. Remapping changes prompts
immediately without changing game logic. Setup uses the eight logical input names
listed above; normal menus and games show the assigned button/key.

| Chirky input | Default SNES controller | Default keyboard |
| --- | --- | --- |
| Arrows | D-pad | Arrow keys |
| Primary | B | N |
| Secondary | Y | M |
| Start | Start | Enter |
| Menu | Select | Escape |

## Controller setup

New controllers default to SNES bindings and labels on both platforms. Saved
profiles, including Generic controller mappings, take precedence.

Both platforms offer the same controller setup: **Settings > Input Settings >
Map controller 1** (or another connected controller's numbered entry).
Choose **SNES preset** for B/Y, Start/Select and arrow labels with default bindings,
or **Generic controller** to map all eight inputs with neutral button/axis labels.
**Remap buttons** changes bindings while retaining the selected label profile.
Directions and keyboard arrow keys use `← → ↑ ↓`; letter keys keep their letters.
Use a keyboard or an already configured controller to navigate setup for an
unmapped controller.

The SNES preset matches the tested [SNES/Pico adapter](snes-pico-usb-controller.md)
and browser-standard gamepad positions. Other USB adapters can expose different
button numbers; use Generic controller to configure those devices. Remap buttons
lets you customize the bindings within an existing profile.
Profiles are saved per controller model on both platforms. Two identical
controllers share a profile but remain independent input devices; different models
can use different profiles. Settings are local to each Pi or browser, not synced.
Existing Pi SNES/Pico bindings and browser controller mappings remain usable.

- Primary selects menu items; Secondary goes back. Menu pauses gameplay.
- Rosey Chop: Primary chops and Secondary jumps.
- Phosphor Run: Primary jumps, Secondary dashes, Start uses a life to retry.
- Bramble Hollow: Primary interacts/closes dialogue, Secondary mounts/dismounts the bicycle or uses
  a dialogue service, Start opens/closes world controls.
- Start, Primary or Secondary begins a title screen after inputs are released.
- F1 is native recovery/cancel, F12 captures a Pi snapshot. The physical
  Start + Select recovery chord remains available outside input setup.

## Keyboard mapping and input testing

Choose **Settings > Input Settings**, then **Map keyboard 1**, **Map keyboard 2**, or a numbered **Map controller** entry.
Keyboard setup and the Generic controller/Remap buttons wizards capture the eight
Chirky inputs in the order above, with Primary before Secondary. Release inputs between prompts. Duplicate bindings are
rejected; a key used by the other player shows "ALREADY USED" and keeps the current
prompt open. Completed mappings save atomically. F1 cancels keyboard setup; holding
two controller buttons for one second cancels controller setup. F1/F12 remain
reserved. Cancellation and failed saves preserve the previous mapping.

P1 is the default keyboard layout everywhere: arrow keys with N/M. P2 uses
WASD with F/G. Enter (Start) and Esc (Menu) are shared. There is no separate solo
layout: changing P1 also changes menu and single-player controls. Player layouts
must use distinct keys except matching Start/Menu bindings. The fighter joins
with each player's action button, so Enter does not claim both slots. Prompts
and keyboard help follow the saved mappings.

The menu order is Test inputs, Map keyboard 1, Map keyboard 2, a numbered entry
for each connected controller, then Back. No controller entries appear when
none are connected. Selecting a controller entry opens its options directly; pressing a
controller button briefly marks its menu row to help identify it. Esc/Menu,
Secondary or F1 returns from its options. Disconnecting the selected controller
returns to input settings and cancels any unfinished mapping.

In the browser touch layout, Input Settings is hidden until a controller is
detected or a physical keyboard is used. Disconnecting the last controller before
keyboard use returns an open input screen to Settings. Keyboard mapping entries
are initially hidden: the menu is Test inputs, connected controllers, Back.
Keyboard use reveals both mapping entries before controller entries and test rows
for the rest of the current page session. Physical keyboards still control games normally.
Desktop browser and Pi keyboard setup remain available as before.

**Test inputs** gives each connected source a separate labelled, coloured row,
including both keyboard layouts and each controller (abbreviated CTRL1, CTRL2,
and so on in the compact table). Fixed touch controls are not listed.
The table always shows configured physical labels under D-pad, Prim,
Sec, Start and Menu. Up sits above Left/Down/Right when row space permits.
Held or newly pressed inputs highlight their cells.
This screen suspends navigation; hold any device's Menu binding for one second or press F1
to return. Screen transitions consume the opening press and wait for two neutral
updates before accepting input on the next screen.

## Display calibration (Pi)

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

## Saved settings

Pi controller profiles live in `config/controllers.conf`, keyed by USB identity
and device name. `config/host.conf` retains legacy `bind_*` and
`controller_labels` values as a fallback for the tested SNES/Pico model until
that model has a saved profile. `input_version=5` keyboard settings use `key_*` for P1 and
`p2_key_*` for P2. The former arrow/X/Z default migrates to arrow/N/M. Existing SNES B/Y/Select
bindings migrate to Primary/Secondary/Menu; earlier action mappings still migrate.
Explicit new names take precedence. Conflicting new defaults are left unbound.
Retired A/X/L/R bindings are removed when saving. Host and all games must be
rebuilt together for ABI 16. `safe_x` and `safe_y` retain display margins.

## Game configuration and Pi snapshots

Each game owns one inspectable directory under `games/`. For example,
`games/rosey-chop/` contains `game.conf` with plain `key=value` settings and
ordinary WAV sound files under `assets/`. Relaunch the game after changing its
configuration or assets. Restart the native host after manually editing
`config/host.conf`. The native executable takes no command-line arguments.

Snapshots are written as lossless PPM files under `snapshots/`. Press `F12`, or
send `SIGUSR1` to the running process. The latter is the dashboard integration
point:

```sh
pkill -USR1 -x chirky-host
```

## Dashboard and Pi administration

Start the dashboard with `node dashboard/server.js` from the repository root,
then open <http://127.0.0.1:3030/>. **Console** shows the snapshot, current state,
health, startup, installation, restart, power, and logs. **Launcher** mirrors the
CRT menu: games, Settings, and Power Down. Browsing its Settings submenu does
not change the CRT until a screen is selected. **Edit** lists the games and opens
a dedicated editing page for each.

**Edit launcher** changes menu names, order, and visibility. **Apply to console**
saves `config/launcher.conf` on both the laptop and Pi and reloads the menu without
restarting the running game. Each section must retain at least one visible item. Settings always includes a Back row,
selected with Primary; Secondary also goes back.
Names support up to 24 characters using the CRT font. Cancel discards the draft.
Deployment preserves the Pi’s existing launcher configuration; use Apply to console
to change it. Missing or invalid configuration falls back to the default menu.

**Install project on Pi** copies and builds the laptop project, then restarts the
console. **Restart console software** uses the existing Pi build. Put local SSH key paths in
`dashboard/config.local.json`; that file is intentionally ignored by Git.

The dashboard and CRT launcher both include a deliberate Pi power-down action.
The dashboard asks for confirmation; the launcher keeps `POWER DOWN` separate
from the game list and activates it with Primary.
