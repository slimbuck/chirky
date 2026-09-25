# Texture Renderer Integration

The authoritative signatures are in `src/rect_renderer.h`. Texture functions
require the renderer's current GLES2/WebGL1 context. Zero-initialize each
`struct rect_renderer_texture`; create with tightly packed top-down RGBA8 bytes
and their byte count. Pixels use straight alpha. Caller owns each texture and
must delete it before destroying the renderer/context. Delete flushes pending
draws referencing that texture. Recreating a live texture fails unchanged.

Call `rect_renderer_sprite_clipped` with the original integer destination plus
the physical safe-area offset. Its final four arguments are the physical clip
rectangle. Destination and clip coordinates are bottom-left; source pixel
regions are top-left. The clip is also intersected with the renderer viewport.
Do not pre-clip the destination or adjust the source region in the host.
Out-of-bounds source regions, empty rectangles and zero alpha are no-ops.

```c
rect_renderer_sprite_clipped(renderer, texture,
    x + safe_x, y + safe_y, width, height, sx, sy, sw, sh,
    red, green, blue, alpha, flip_x,
    safe_x, safe_y, api->screen_width, api->screen_height);
```

`rect_renderer_sprite` has the same arguments except the final clip rectangle
and uses the whole renderer viewport. White RGBA `(255,255,255,255)` preserves
the source. Clipping interpolates source coordinates from the original
destination, so partial clips retain the original scale and horizontal flip.
Different clips can share one batch; no per-sprite GL scissor is used.

Sprite capacity is 256 quads. Texture changes, transitions to/from rectangles,
and capacity flushes preserve submission order. Source alpha is multiplied by
tint alpha; RGB uses SRC_ALPHA / ONE_MINUS_SRC_ALPHA and output alpha uses
ONE / ONE_MINUS_SRC_ALPHA. Rectangle vertex layout, shader and 4096-quad
capacity remain unchanged. Sprite resources initialize on first texture create.
The renderer owns its GL draw state and disables blending after sprite batches.

Sampling follows the existing splash integer-edge mapping, not ordinary
center-aligned nearest scaling. For destination position `p`, source extent
`s`, and destination extent `d`, the source index is `((p+1)*s-1)/d` using
integer division. Horizontal flip changes it to `s-1-index`. Vertical sampling
maps top-down decoded rows to bottom-left drawing coordinates. The shader snaps
the selected texel to its center to avoid sampler rounding differences.
Exact pixels are verified on Mesa GLES2 and Chrome WebGL1 at 320x240,
288x216, 256x192 and 17x13, including flip and clipping. Fragment highp is used
when available; mediump-only devices may lose exact edge mapping precision.
No hardware Pi validation was performed.

Splash entry points are `splash_load_file_api(art, api, path)` and
`splash_load_api(art, api, config)`. Both return whether the request was accepted,
not whether decoding has completed. `art.image` is always present. With all
asset callbacks and `draw_sprite`, the helper requests IMAGE, retains the handle
until `splash_free`, and draws one sprite after READY. Pending, failed or rejected
requests never trigger synchronous fallback. Without the callbacks it uses
the existing PPM/rectangle path. `splash_load_file(art,path)` and
`splash_load(art,config)` remain explicit legacy entry points. Keep the requesting
context alive until `splash_free`.

## Tests

From the repository root under Linux/WSL with EGL/GLES development libraries:

```sh
sh tools/texture-tests.sh
TEXTURE_TEST_DIR=build/texture-tests sh tools/texture-tests.sh --web
node tools/texture-browser.cjs build/texture-tests /path/to/chrome
```

The browser executable is optional on Windows with standard Chrome installation;
otherwise set `CHROME` or pass the path. The browser runner uses a temporary
profile and local HTTP server, checks actual WebGL framebuffer pixels, then
closes both. Native tests use a surfaceless EGL pbuffer and software rendering.
The script also runs async splash lifecycle tests and the existing splash test.
Tests do not touch DRM, input devices, or a live host.

The existing host runtime test and `tools/render_benchmark.c` were also run
locally. The latter reported zero different channels for both its game frame
and rectangle overflow/clipping regression.
