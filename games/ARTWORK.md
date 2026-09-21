# Game artwork

Each game keeps two files with the same 288x216 pixels:

- `assets/artwork/splash.png`: viewable/editable artwork, also shown by the dashboard.
- `assets/artwork/splash.ppm`: binary P6 RGB export read by the game.

After editing a PNG, run `powershell -File tools/export-splash.ps1` to update
both game exports. Keep the PNG at 288x216. No large masters are stored.
Controls are drawn separately in the host font. Other calibrated viewport sizes
scale the image to fit. Missing or invalid images fall back to the text title.

Rosey Chop was generated with the built-in image generation tool using the
existing Phosphor Run illustration as a style reference. Both were reduced
to runtime size with ImageMagick.

## Rosey Chop generation prompt

Use case: stylized-concept. Create a finished 4:3 landscape pixel-art title illustration for the retro game "ROSEY CHOP". Reference image is style inspiration only: match the professional richly shaded 16-bit pixel-art game-cover treatment and bold readable title, but depict a very different warm rose garden. Exact text: "ROSEY CHOP" in large beautiful cream and pink chunky pixel lettering across the upper third. Below: a tiny charming gardener wearing a straw hat with a pink ribbon and dusty pink clothes, carrying a small pruning tool, in a lush walled garden with crimson, pink, apricot, cream and a few withered black roses, winding stone paths and a little lily fountain. A single yellow-black wasp adds playful tension; gathering rain clouds beyond the garden hint at an approaching storm. Deep forest greens, warm cream, dusty rose and soft gold. Clean strong silhouettes, readable even reduced to 288x216; no tiny text, no controls or UI, no watermark. Keep all title letters inside a generous 7% safe margin. Compose as one cohesive game title screen, not a mockup or framed poster. Save the generated image.
