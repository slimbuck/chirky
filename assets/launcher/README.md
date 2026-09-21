# Launcher artwork

`splash.png` is the editable 320x240 image for the Pi's launcher menu.
`splash.ppm` is its runtime export. Both contain the same pixels; no large
master is kept in the project. Run `powershell -File tools/export-splash.ps1`
after editing the PNG. The host fits the art into the calibrated viewport,
adds black borders, and draws readable menu labels separately. Missing art
falls back to the plain menu. Both installation paths include this folder.

Created with the built-in image generation tool and reduced with ImageMagick.
The console name is deliberately not baked into the artwork.

## Generation prompt

Use case: stylized-concept. Create a beautiful 4:3 landscape pixel-art background for an actual 320x240 CRT homebrew game-console launcher. Elaborate miniature circuit-board landscape with green-teal PCB traces, gold contacts, black microchips with silver legs, chunky capacitors, resistors, connectors and ribbon cables. Dark midnight teal with jewel-like cyan and amber LEDs. Very cool retro electronic hardware illustration, deliberate chunky pixel clusters with strong silhouettes that remain legible at 320x240, subtle atmospheric depth, not a photograph. Composition for a working menu: keep the entire left 65 percent dark and very sparse, especially upper-left title area and central left list; concentrate vivid detailed components along the right-hand edge and bottom-right corner. Thin glowing circuit traces may lead into the dark empty space. No text, no letters, no logos, no UI, no border, no watermark. A coherent polished image, not a mockup.
