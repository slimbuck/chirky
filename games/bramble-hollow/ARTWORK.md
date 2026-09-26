# Artwork

The title and polished sprite sheets were created with the built-in image
generation tool from the user's supplied brown-bear reference. Source PNG files
are retained under `assets/artwork/sources/`. Runtime sprites are 8-bit RGBA PAM
files so Platform API 9 can preserve transparent pixels.

`tools/clean-bramble-sprites.ps1` isolates every generated frame, reduces it
once to its final pixel grid and palette, and packs it into exact runtime cells.
Atlas cells match their runtime draw sizes, avoiding uneven pixel scaling. The
Bramble runtime test checks gutters, alpha values, frame alignment, 1:1 drawing,
and bicycle-wheel geometry.

Run the asset pipeline from the repository root with:

```powershell
tools/clean-bramble-sprites.ps1
```

This is the only supported command for producing `assets/player.pam` and
`assets/friends.pam`. The generated PAM files are reviewed runtime artwork, not
disposable build output. Do not edit them manually or introduce a parallel
generator. Changes to the polished source, crops, reduction sizes, palette, or
semantic cleanup must be followed by a source-to-cell and live-game comparison.

## Runtime contract

| Asset | Layout | Cell | Atlas | Important alignment |
| --- | --- | --- | --- | --- |
| `player.pam` | 4 directions x 4 frames | 40x40 | 160x160 | 34-pixel subject, horizontally centred, shared feet row 38 |
| `friends.pam` | 4 columns x 2 rows | 50x45 | 200x90 | Subjects bottom-aligned inside transparent padding |

The cat source is reduced to a 30x40 subject within its 50x45 cell. Three dark
whisker strokes on each cheek are restored after palette reduction because they
are meaningful character details that otherwise disappear at this resolution.

## Acceptance procedure

For every affected subject, inspect the polished crop, a 10x nearest-neighbour
view of the final PAM cell, the native-size cell, and the sprite in motion in the
browser. Check the Pi snapshot after deployment. A byte-identical local/remote
hash proves that deployment copied the tested asset; it does not prove the art
is visually acceptable.

The long-term asset safeguard is a deterministic regeneration test: generate
both atlases into a temporary directory and compare them byte-for-byte with the
approved checked-in PAM files. Any intentional visual update should include the
new reference atlas and evidence of the live comparison.

Player prompt: create a strict 4x4 directional sprite sheet for the referenced
warm brown bear, with a moss-green satchel, berry-red neckerchief, and a gentle
bouncy walk cycle on a transparent background.

Neighbour prompt: create a strict 4x2 sheet containing a zebra shopkeeper,
turtle gardener, calico cat baker, sheep librarian, two penguin nuns, a red
basket bicycle, and a songbird, matching the player's woodland pixel-art style.

Title prompt: create a polished 4:3 16-bit storybook title scene showing the
bear cycling across a wooden bridge through the village, with the cottage,
shops, neighbours, stream, flowers, and stained-glass church. Exact title:
"BRAMBLE HOLLOW".
