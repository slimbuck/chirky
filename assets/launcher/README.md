# Launcher artwork

`chirky-box.svg` preserves the maker's-badge logo as editable vector paths.
It matches the launcher's 3x pixel lettering, colours, frame and shadow, with
a transparent canvas and no font dependencies. Regenerate it from the native
glyph definitions with `node tools/export-logo.js`.

The shared console header uses `mascot.ppm` at exactly 64x64 framebuffer pixels,
inline with the wordmark or page title on a solid navy background. The launcher
keeps the wordmark's original glyphs and colours, drawn at 2x to fit the header.
`mascot.png` retains binary transparency and two
pixels of gutter. `source/mascot.png` is the approved RGB-feather source, isolated
from runtime assets. It is reduced once offline; neither host scales it.

Build with `python tools/build-launcher-art.py` (Pillow 12.3.0). This is the single
pipeline for the mascot and the 11/13/17-pixel Fredoka menu masks in
`include/launcher_font.h`. The original font and SIL licence live in `source/`.
`manifest.json` records dimensions, palette, baseline, frame order and approved
source/output hashes. After visual review, regenerate the files and manifest
together. `python tools/build-launcher-art.py --check` regenerates into a temporary
directory and byte-compares all runtime output with the reviewed files.

`src/console.c` owns the scrolling spring shared by the launcher, Settings,
Input settings and Display area. The gold selection stays fixed while
entries scroll underneath it, stopping at the first and last items without
wrapping. Logical selection is independent of visual
settling. `src/console_ui.h` rounds final pixel positions, clips the list within
the safe viewport and draws the wordmark and mascot unchanged during scrolling.
Settings opens through its normal scrolling catalog entry.
Mapping and button-test screens use the same header, typography and palette;
the in-game pause overlay uses matching rounded cards and gold selection.

Each game's `launcher_order` in `game.conf` supplies its default position;
`launcher_icon` selects its 20x20 picture. The same build command above crops
existing game art at native resolution according to `icon-sources.json`, writes
the reviewed PNGs and compiles their pixels into `include/launcher_icons.h`.
The icon registry is discovered from game manifests rather than a game list in
the host. No game artwork is altered, resized or loaded into a second renderer.
Icons move with their rows, retain 1:1 pixels and clip to the list bounds.

The older `splash.png`/`splash.ppm` circuit-board backgrounds below are retained as
historical source material; the launcher no longer loads or publishes them.

Created with the built-in image generation tool and reduced with ImageMagick.
The console name is deliberately not baked into the artwork.

## Historical background generation prompt

Use case: stylized-concept. Create a beautiful 4:3 landscape pixel-art background for an actual 320x240 CRT homebrew game-console launcher. Elaborate miniature circuit-board landscape with green-teal PCB traces, gold contacts, black microchips with silver legs, chunky capacitors, resistors, connectors and ribbon cables. Dark midnight teal with jewel-like cyan and amber LEDs. Very cool retro electronic hardware illustration, deliberate chunky pixel clusters with strong silhouettes that remain legible at 320x240, subtle atmospheric depth, not a photograph. Composition for a working menu: keep the entire left 65 percent dark and very sparse, especially upper-left title area and central left list; concentrate vivid detailed components along the right-hand edge and bottom-right corner. Thin glowing circuit traces may lead into the dark empty space. No text, no letters, no logos, no UI, no border, no watermark. A coherent polished image, not a mockup.

## Current flat 2D revision

Generated with the built-in image editing tool using the previous background as
the edit target, then exported at 320x240 with ImageMagick.

Use case: style-transfer. Edit target: the supplied Chirky Box launcher circuit-board background. Replace the angled three-dimensional hardware scene with a strictly flat 2D circuit-board illustration viewed straight down, orthographic, zero perspective. Output one 4:3 landscape image designed to be reduced to exactly 320x240 pixels. Keep the existing dark midnight-teal, turquoise, muted gold and amber palette and menu-friendly composition: left two-thirds predominantly dark and sparse for overlaid menu rows; upper area especially quiet for the existing logo; most detailed circuitry concentrated in the right-hand third and bottom edge. Draw beautiful tidy PCB traces with right-angle and 45-degree corners, circular vias and solder pads, flat rectangular black IC packages with silver pins seen from directly overhead, small flat resistor and capacitor footprints, clean connector contact rows, and a few tiny amber indicator dots. True deliberate retro pixel art with crisp stepped lines and strong shapes that survive native resolution. Absolutely no visible component side walls, no tilted chips, no horizon, no depth of field, no volumetric glow, no extrusion, no 3D rendering or photography. Flat shapes and limited shading only, like a stylish PCB layout illustrated for a retro game. No words, letters, labels, logo, border or menu controls: those are rendered separately by the launcher. Retain the quiet dark space so cream and amber text remains easy to read.
