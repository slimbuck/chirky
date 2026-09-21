# Rosey Chop

A top-down, native-pixel garden game for the Two Forty CRT host. One complete
level: **The Rose Garden**, with 18 dead black roses among 78 living crimson,
pink, apricot and cream roses. Chop every black rose before the 75-second storm
timer expires. A wasp sting immediately ends the run.

- D-pad: run in all four directions.
- B: chop. Hold to
  repeat a circular sweep; nearby black roses are cut, healthy roses are safe.
- Y: jump.
  The middle of the jump clears a wasp; takeoff and landing are vulnerable.
- B on the title or result screen: begin or replay the same first level.
- Select: pause; choose Continue Game or Return to Launcher.

The game code reads SNES B and Y directly. The host maps controller and keyboard
inputs to SNES buttons; default keyboard keys are X for B, Z for Y, arrow keys
for the D-pad and Escape for Select. Game prompts always show SNES names.

Wasps first announce themselves after eight seconds, give a 1.5-second warning,
then pursue for six seconds. Keep moving or jump to dodge. Chopping does not kill
wasps. There is an eight-second respite between visits. Rain begins to drizzle
in the final 15 seconds; the full storm ends the run at zero. The small map shows
remaining black roses, your peach marker and an active wasp in yellow.

The garden scrolls to fit the host's calibrated viewport. The fountain, living
roses and stone paths are walkable. A win screen records completion time; replay
resets the timer, roses, wasp and particles. There are no further levels.

## Editing and building

`make` discovers the game automatically. Deploy with the existing dashboard's
**Deploy + build**, then select **Rosey Chop** in the launcher. This game uses
the existing host ABI and requires no new runtime libraries.

`game.conf` exposes storm duration (15–300 seconds), wasp interval (4–60 seconds),
wasp duration (2–12 seconds) and the sound device. Use Save + reload after changes.
`assets/level-01.txt` is editable in the dashboard's level editor. Keep its fixed
24 by 16 dimensions, exactly one `S` spawn and at least one `d` dead rose.
`r`, `p`, `a`, `w` are living roses and `.` is lawn. Art and the crossing paths are
drawn with native rectangles in `render.c`; roses do not add collision obstacles.

Original PCM sound effects are included. Regenerate them with
`python3 tools/generate_rosey_sounds.py` (standard library only).

`make test` includes a complete input-driven route with wasps enabled plus checks
for safe living roses, replay, jump avoidance, stings, storm expiry and viewport
layout. Software-rendered previews use the actual host font and are written to
`build/rosey-*.ppm`. These tests do not access the Pi or DRM hardware.

## Title artwork

`assets/artwork/splash.png` is the editable 288x216 image used by the dashboard;
`splash.ppm` is its runtime export. See [artwork exports and generation prompt](../ARTWORK.md).
