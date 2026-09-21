# Launcher artwork

`chirky-box.svg` preserves the maker's-badge logo as editable vector paths.
It matches the launcher's 3x pixel lettering, colours, frame and shadow, with
a transparent canvas and no font dependencies. Regenerate it from the native
glyph definitions with `node tools/export-logo.js`.

`splash.png` is the editable 320x240 image for the Pi's launcher menu.
`splash.ppm` is its runtime export. Both contain the same pixels; no large
master is kept in the project. Run `powershell -File tools/export-splash.ps1`
after editing the PNG. The host fits the art into the calibrated viewport,
adds black borders, and draws readable menu labels separately. Missing art
falls back to the plain menu. Both installation paths include this folder.

Created with the built-in image generation tool and reduced with ImageMagick.
The console name is deliberately not baked into the artwork.

## Original generation prompt

Use case: stylized-concept. Create a beautiful 4:3 landscape pixel-art background for an actual 320x240 CRT homebrew game-console launcher. Elaborate miniature circuit-board landscape with green-teal PCB traces, gold contacts, black microchips with silver legs, chunky capacitors, resistors, connectors and ribbon cables. Dark midnight teal with jewel-like cyan and amber LEDs. Very cool retro electronic hardware illustration, deliberate chunky pixel clusters with strong silhouettes that remain legible at 320x240, subtle atmospheric depth, not a photograph. Composition for a working menu: keep the entire left 65 percent dark and very sparse, especially upper-left title area and central left list; concentrate vivid detailed components along the right-hand edge and bottom-right corner. Thin glowing circuit traces may lead into the dark empty space. No text, no letters, no logos, no UI, no border, no watermark. A coherent polished image, not a mockup.

## Current flat 2D revision

Generated with the built-in image editing tool using the previous background as
the edit target, then exported at 320x240 with ImageMagick.

Use case: style-transfer. Edit target: the supplied Chirky Box launcher circuit-board background. Replace the angled three-dimensional hardware scene with a strictly flat 2D circuit-board illustration viewed straight down, orthographic, zero perspective. Output one 4:3 landscape image designed to be reduced to exactly 320x240 pixels. Keep the existing dark midnight-teal, turquoise, muted gold and amber palette and menu-friendly composition: left two-thirds predominantly dark and sparse for overlaid menu rows; upper area especially quiet for the existing logo; most detailed circuitry concentrated in the right-hand third and bottom edge. Draw beautiful tidy PCB traces with right-angle and 45-degree corners, circular vias and solder pads, flat rectangular black IC packages with silver pins seen from directly overhead, small flat resistor and capacitor footprints, clean connector contact rows, and a few tiny amber indicator dots. True deliberate retro pixel art with crisp stepped lines and strong shapes that survive native resolution. Absolutely no visible component side walls, no tilted chips, no horizon, no depth of field, no volumetric glow, no extrusion, no 3D rendering or photography. Flat shapes and limited shading only, like a stylish PCB layout illustrated for a retro game. No words, letters, labels, logo, border or menu controls: those are rendered separately by the launcher. Retain the quiet dark space so cream and amber text remains easy to read.
