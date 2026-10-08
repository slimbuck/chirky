# Platform API 16

`include/chirky.h` defines the shared native/browser contract. Both host and
game report `CHIRKY_ABI_VERSION == 16`. Native games export
`chirky_game_entry()` from a shared library; browser builds link the same game
entry point into a WASM side module loaded by the persistent Emscripten host.
`src/runtime.c` owns the same lifecycle on both platforms; `src/console.c` owns
menus, pause and input setup outside the game ABI.

## Game callbacks

The game returns a `struct chirky_game_api` with `init(host, config_path)`,
`shutdown()`, `update(input)`, and `render()`. The host calls these on its main
thread. `init` returns success or failure; `shutdown` releases game allocations
and asset references, including partial initialization. Games must make
shutdown safe to repeat. Browser side modules can remain compiled in memory, so
`init` must explicitly reset game state on every launch. Shutdown must not enqueue new sound playback.

`struct chirky_host_api` supplies an opaque `context`, viewport dimensions,
rectangle/text drawing, button labels, an optional profiling callback, and the
asset operations below. Pass `context` back unchanged. Coordinates use the
logical playable viewport; the host applies its physical offset and clipping.
Games consume logical buttons and button edges from `struct chirky_input`.
Raw keyboard and controller-device state belongs to the platform host and is
not exposed through the portable ABI.

## Rendering contract

`screen_width` and `screen_height` describe the logical playable viewport, not
necessarily the physical display mode. The native host subtracts the calibrated
CRT-safe margins from 320x240 and translates all game drawing into that region.
The browser host presents the same arrangement using a 288x216 logical viewport
inside a 320x240 framebuffer. A game must never add a safe-area border or display
offset itself.

Drawing uses integer logical pixels with `(0, 0)` at the bottom left. Rectangles
cover the half-open area `[x, x + width)` by `[y, y + height)`. The host clips
destination geometry to the logical viewport while preserving the original
sprite mapping, then applies the physical safe-area translation. Games may use
their own top-left world convention, but must perform that conversion before
calling the host, as Bramble Hollow and Rosey Chop do.

Decoded images are top-down RGBA8. A sprite source rectangle therefore uses a
top-left `(sx, sy)` with positive `sw` and `sh`; its destination `(x, y)` uses the
bottom-left logical convention. Invalid or out-of-bounds source rectangles,
non-positive dimensions, zero alpha, failed assets, and fully clipped draws are
no-ops. Tint and alpha are applied during drawing, and `flip_x` mirrors the
source horizontally without changing its destination bounds.

The host supports nearest-neighbour scaling when destination and source sizes
differ. That capability is useful for full-screen art and intentional scaling,
but it is not an automatic pixel-art cleanup step. For native pixel sprites,
use `width == sw` and `height == sh`; reduce source artwork once offline, keep
transparent gutters inside fixed atlas cells, and draw each complete cell at
its native size. Fractional ratios such as reducing a large generated character
to a small destination distribute source texels unevenly even with nearest
sampling and can make lines, wheels, and animation alignment appear malformed.

The browser page enlarges the completed 320x240 framebuffer at integer multiples
where space permits and uses `image-rendering: pixelated`. This page-level scale
does not change game coordinates. Native CRT calibration likewise changes only
the position and dimensions of the logical viewport, never individual sprites.

`draw_text` uses the built-in 5x7 font. `x` is its left edge and `y` identifies
the bottom-left coordinate of the glyphs' top pixel row; the remaining rows
descend in logical coordinates. Each character advances by `6 * scale` and each
set font pixel occupies `scale` square logical pixels. Both text renderers also
support the UTF-8 direction arrows `← → ↑ ↓`, each occupying one glyph cell.
Use `text_encoding.h` for glyph counts and clipping without splitting a symbol.
Use `button_label` when
showing controls: it resolves the logical action to the mapped physical control
on the most recently used device. Query it during rendering so remapping and
device changes are reflected immediately. Examples are `B` / `SELECT` on the
configured SNES/Pico, `N` / `ESC` on the default keyboard, and `A` / `MENU` on
touch. Unknown controls use button/axis identifiers, and missing bindings return
`UNBOUND`. With nonzero capacity the callback writes a NUL-terminated label.
`include/input_labels.h` also groups direction labels without assuming arrow keys.
Labels are presentation only; logical enums and combined button semantics stay
unchanged. Controller legends belong to host profiles, never game code.

## Asset operations

`chirky_asset` is a 32-bit opaque handle. Zero is invalid. Paths are exact cache
keys together with asset type; equivalent spellings are not normalized.
Relative paths resolve from the repository root in the normal host setup.

| Host callback | Behavior |
| --- | --- |
| `asset_request(context, path, type)` | Acquires a caller reference and returns a handle, or zero when the request cannot be accepted. An accepted request can still fail to load. |
| `image_create(context, width, height, rgba, size)` | Copies a load-time RGBA8 image into a READY immutable handle. Zero on invalid input or exhaustion; release normally. |
| `asset_status(context, handle)` | Returns `CHIRKY_ASSET_LOADING`, `CHIRKY_ASSET_READY`, or `CHIRKY_ASSET_FAILED`. Invalid/stale handles report failed. |
| `asset_data(context, handle)` | Returns an immutable borrowed `struct chirky_asset_view`; the view is empty unless ready. |
| `asset_release(context, handle)` | Releases one caller reference. Balance each successful request, even when multiple requests return the same handle. |
| `draw_sprite(context, image, x, y, width, height, sx, sy, sw, sh, r, g, b, a, flip_x)` | Draws an IMAGE source rectangle with tint, alpha, horizontal flip, scaling, and viewport clipping. An unready image is skipped. |
| `draw_sprite_projected(context, image, x, y, scale, sx, sy, sw, sh, r, g, b, a, flip_x)` | Draws with rounded camera-projected texel edges; retains fractional camera coordinates until sampling. |
| `sound_play(context, sound)` | Plays a preloaded SOUND handle. There is no device/path argument or playback-completion result. Unavailable sound may be skipped. |

The asset view contains `const void *data`, byte count `size`, and unsigned
`width`, `height`, `rate`, and `channels` metadata:

| Type | Data and metadata |
| --- | --- |
| `CHIRKY_ASSET_BLOB` | Original bytes plus an extra NUL terminator; `size` excludes that terminator. Used for config, level, sprite, and robot-model parsers. |
| `CHIRKY_ASSET_IMAGE` | Decoded top-down RGBA8 pixels, with `width` and `height`. The decoder reads opaque P6 PPM and RGBA P7 PAM. |
| `CHIRKY_ASSET_SOUND` | Decoded native-endian signed PCM16, interleaved, with `rate` and `channels`; `size` is bytes, not frames. The current decoder accepts uncompressed PCM WAV with 8- or 16-bit samples. Native playback supports mono/stereo. |

Retain a caller reference while reading a borrowed view. Do not modify or free
its data. Store clear invalidates all handles and views, regardless of their
reference counts. Handles encode a generation so a released slot cannot silently
become a different asset under its old handle.

`asset_store_retain` is an internal host/store operation for acquiring another
reference, including native sound pins. It is not a game-facing callback.

## Local persistent records

ABI 13 adds optional `save_read(context, game, key, data, capacity)` and
`save_write(context, game, key, data, size)` callbacks. Games own versioned
record formats and check whether the callbacks exist. Game and key identifiers
contain only letters, digits, hyphens or underscores, up to 95 bytes each.
Records contain 1 to 65,536 bytes. Reads return the byte count, or zero for a
missing, invalid, unavailable or oversized record. Writes return success/failure
and replace a record atomically; games must handle failure explicitly.

Linux stores `saves/<game>/<key>` with a flushed temporary file and rename.
The browser stores the same bytes under `chirky.save.v1.<game>.<key>` in
localStorage. Phosphor Run uses this for per-level high scores. These records
are local to the installation or browser origin, with no player authentication,
cloud synchronization or global ranking. Game/key names organize trusted game
records; they are not a sandbox for third-party executable game modules.

## World director transport

Introduced in ABI 11, the optional, eventually-consistent transport supplies bounded external
world direction. Games call `director_connect` with an HTTP base URL, game id,
and world id. A successful return means the host accepted the configuration,
not that the service is currently online. The native host performs network I/O
on a background thread; the browser host uses asynchronous `fetch`. Neither may
block a game update or render call.

`director_event` queues one bounded UTF-8 JSON object. It returns false when the
transport is unavailable or its queue is full; games must remain playable and
may keep a local diagnostic log. The host adds a per-process session and
monotonic sequence number. `/v1/sync` acknowledges those numbers so retries are
idempotent.

`director_state` copies an opaque NUL-terminated UTF-8 state document only when
the host has a complete revision newer than `after_revision`. It returns the
byte count excluding the terminator and writes the matching revision. Zero means
there is no newer state. Games validate the document and retain their previous
state if parsing fails.

The host owns HTTP, retries, queue bounds, and service lifecycle. The standalone
director owns persistence and model credentials. Games never call a model API
or contain an API key. Loss of either the network or director must not stop the
deterministic simulation.

## Native loading phases

1. The main thread unloads the previous game and starts prefetch for the selected
   game's directory. The host draws a loading screen while it polls readiness.
2. One asset-store pthread traverses the directory, reads files, and decodes PPM
   and WAV data. It never calls game code or accesses game state, GL, or ALSA.
3. Once prefetch completes, the main thread loads the game module and calls
   `init`. Game-specific config, level, sprite, and robot parsing still happens
   here, using cached BLOB bytes. Phosphor Run parses all campaign levels during
   init and reuses its level buffers during play.
4. After successful game init, the host reads cached `sound_device` configuration
   and starts or reuses the native mixer. Startup returns after creating the
   worker, without waiting for ALSA setup. The host polls STARTING/READY/FAILED
   status and logs worker setup wall/CPU time separately. A failed mixer is
   retried on the next game activation.
5. IMAGE textures are uploaded lazily on their first draw, on the main thread
   with its current GL context. Decoding completion does not mean a texture has
   already been uploaded. Subsequent draws reuse the host's texture cache.

Prefetch covers `.conf`, `.txt`, `.sprite`, `.robot`, `.ppm`, `.pam`, and `.wav` regular
files below the game directory. Every selected file has a BLOB entry; PPM/PAM
and WAV also have decoded entries. Prefetch retains ownership until store clear,
independently of game references. Symlinks are not traversed. The store has 1024
slots and a 64 MiB payload budget, including in-flight allocations. Traversal or
BLOB failures fail the bundle; a decoded IMAGE/SOUND failure can leave its BLOB
available and does not by itself fail the bundle.

`include/asset_file.h` adapts existing `FILE *` parsers through
`chirky_file_open` and `chirky_file_close`. With asset callbacks, it requests a
BLOB and opens a read-only `fmemopen` stream only if that handle is already
ready. It does not wait for a pending native request. Close the stream before
releasing its handle. Without an asset API, the helper uses ordinary `fopen`.
Consequently, init-time parser paths must match prefetched paths exactly;
out-of-directory paths or alternate spellings need preparation before init.

## Native audio and unload

The host resolves SOUND handles and pins each distinct sound used for playback.
The mixer copies a PCM pointer and metadata into a bounded 32-command queue;
it does not own or copy the sample allocation. `audio_mixer_play` uses try-lock
enqueue without allocation, file reads, or process creation. Busy/full queues
can drop requests. One persistent audio worker owns ALSA and up to eight voices,
replacing the oldest voice on overload. Repeats of an active sample are ignored,
not restarted or layered; distinct samples can still mix. Output is 48 kHz stereo with linear
resampling and PCM16 clipping. ALSA is loaded from `libasound.so.2` at runtime;
the default device is `plughw:0,0`. Audio startup failure leaves the game usable.
The bounded command queue also accepts sounds during startup; setup failure
discards those commands. During startup, reset clears the queue immediately
because the worker has not consumed sample pointers yet. After readiness,
reset remains a synchronous ownership barrier. Stop/device replacement still
joins the old worker; teardown is not an asynchronous operation.

On normal game unload, the host resets the mixer synchronously, then releases
its sound pins and calls game shutdown. It unloads the module, deletes cached
textures on the main thread, and clears the asset store. Reset discards queued,
active, and buffered audio and acknowledges that sample references are no
longer in use. Only then may sample storage be released. Host exit also stops
and joins the audio worker and destroys the asset store before graphics teardown.

`play_sound(context, device, path)` remains in the ABI for compatibility,
including Hardware Test. When the native host has an asset store, this callback
requests the path as SOUND, forwards the handle to `sound_play`, and releases
the temporary caller reference. The playback pin keeps ready sample data alive
until mixer reset. This route uses the same persistent mixer and its configured
`sound_device`; the per-call device argument does not open another device.
Only the no-asset-store legacy/test path retains the `fork`/`aplay` fallback.

The compatibility route expects an already-ready, prefetched sound path. Its
temporary request/release does not keep an unprefetched asynchronous load alive:
if the sound is still loading, playback is skipped and the final release can
discard that request. Phosphor Run and Rosey Chop instead keep SOUND handles
requested during init and call `sound_play` directly for game actions, with a
compatibility fallback for hosts lacking that callback.

## Browser loading and playback

`web/player.js` fetches the asset manifest and selected game's files
asynchronously, then writes the downloaded bytes into Emscripten MEMFS. The
persistent console is initialized once through `web_init`; each game starts
through `web_load` after its downloads finish. The browser asset-store implementation
then performs prefetch, reads, and PPM/WAV decoding synchronously against MEMFS
on the browser main thread. There is no browser asset worker. Game init/parsing
and WebGL texture uploads also run on that thread, with uploads lazy as above.

When the browser host requests a ready SOUND, it calls the JavaScript
`onAssetReady` bridge. JavaScript copies the interleaved PCM16 into an
`AudioBuffer` cached by handle. Later `sound_play` calls reach `onAssetSound` and
create WebAudio sources from that buffer, with at most eight active sources on
this path. As on native, repeats of an active sample are ignored without
restarting it; distinct buffers can overlap. The copied buffer does not borrow
WASM sample memory. User interaction
creates/resumes the `AudioContext`; playback is skipped before audio is unlocked
or while muted, paused, or leaving the page. The legacy path uses cached WAV
bytes and `decodeAudioData` instead.

Page teardown stops WebAudio sources before calling `web_destroy` for an
initialized game. The C teardown releases game assets, textures, the asset store,
and WebGL resources; JavaScript closes the audio context. Games use the same
asset handles and callbacks in both hosts; native mixer/store lifecycle functions
and the JavaScript bridge remain platform internals.

## GPU meshes (introduced in ABI 14)

`draw_mesh(context, vertices, count, ambient)` renders an opaque, unindexed
triangle list. Both Linux GLES2 and browser WebGL1 support it. A vertex contains
logical pixel x/y, depth z in [-1,1] (larger is nearer), a screen-space normal,
RGB bytes and an emissive byte (0 or 255). Count must be a multiple of three,
from 3 through 12,288; ambient is a finite value in [0,1]. Inputs are consumed
synchronously. The host applies calibrated offsets and the logical viewport
scissor, so games must not add them. The GPU applies a shared directional light,
depth-tests triangles within the call, and composites the call in drawing order
with 2D content. Separate calls have independent depth layers. Rectangles and
sprites after a mesh cover it normally. The callback returns false if unavailable
or invalid; hosts do not rasterize triangles in software.

Phosphor Run evaluates its small rigid skeleton on the CPU and submits the actor
once. Lighting, triangle coverage and depth resolution run on the GPU. All hosts
and game modules must be rebuilt together for ABI 16.

## Load-time images and atlas text (ABI 15)

`image_create(context, width, height, rgba, size)` copies tightly packed top-down
straight-alpha RGBA8 into a READY, immutable IMAGE handle. Dimensions must be
1–2048 and size must equal width × height × 4. Invalid input, exhausted handles
or the shared 64 MiB asset budget return zero. The source buffer can be freed
immediately. These assets support the normal status/data/release callbacks,
are invalidated on game unload, and upload lazily once through the image cache.
Use this for load-time atlas construction, not streaming pixels every frame.

`draw_text_outlined` uses the same 5×7 font and advance as `draw_text`, with a
black one-logical-pixel outline independent of font scale. Both hosts cache
plain/outlined glyph atlases in the shared renderer for scales 1–8. Each visible
character is one textured quad; foreground tint leaves the black outline black.
Unsupported larger scales retain the pixel helper. Fonts persist across game
switches and are released when the renderer/context is destroyed.

`draw_sprite_projected` accepts finite logical x/y and a uniform scale from
0.25 to 16, plus the same source rectangle, tint and horizontal flip as
`draw_sprite`. Each source texel boundary is rounded after the camera transform;
it does not stretch a pre-rounded destination. The host adds calibrated offsets
and clips coverage while preserving the original UV mapping. This preserves
Phosphor Run's fractional level-start zoom. Ordinary integer sprites continue
to use `draw_sprite` unchanged. Textures use nearest filtering, explicit texel
centres, no mipmaps, clamp-to-edge and transparent atlas gutters on both hosts.

## Local device input (ABI 16)

`chirky_input.buttons` and `button_pressed` retain their combined input semantics.
New `device_count` and `devices[CHIRKY_INPUT_DEVICES]` expose up to eight independent
logical sources. Each `chirky_device_input` supplies a nonzero opaque connection
`id`, a `kind` (keyboard/controller/touch), held buttons, press edges, and eight
NUL-terminated physical `labels`, each at most 23 UTF-8 bytes. Snapshots are valid
only for the current update; copy any state needed for rendering. Array positions
can change. Use IDs, never array positions or kinds, to assign players.

A missing ID means disconnected; reconnecting a pad gets a new ID even at the same
USB/Gamepad index. Keyboard interfaces feed two logical sources with independently saved player
layouts (browser APIs cannot distinguish physical keyboards). Both have keyboard
kind and distinct opaque IDs; browser touch is one source. IDs are
local to this host session, not persistent identities or network player IDs.

A press edge can be true while held is false for a tap between ticks. Hosts
consume these edges every console tick, including pause/loading, rather than
queuing stale actions for the game. Opposite directions may both be held; games
choose their conflict rule. Combined controls and console pause remain available
from unassigned sources too. P1 keyboard movement/actions also drive combined input for menus and solo
games; P2 adds only its Start/Menu bindings to combined input. Start/Menu may share keys across player layouts; other keys cannot overlap.
No network service is introduced.

Player assignment, joining and disconnect policy belong to the consuming game.
Rebuild the host and all modules together for ABI 16.
