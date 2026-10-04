#ifndef CHIRKY_H
#define CHIRKY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHIRKY_ABI_VERSION 15

/* Unindexed triangle list in logical pixels; z is [-1,1], larger is nearer.
   Normals are in screen axes (+x right, +y up, +z toward viewer).
   Emissive is 0 or 255. Opaque triangles are depth-tested within one call. */
struct chirky_mesh_vertex {
    float x,y,z,nx,ny,nz;
    uint8_t r,g,b,emissive;
};

typedef uint32_t chirky_asset;
enum chirky_asset_type { CHIRKY_ASSET_BLOB, CHIRKY_ASSET_IMAGE, CHIRKY_ASSET_SOUND };
enum chirky_asset_state { CHIRKY_ASSET_FAILED, CHIRKY_ASSET_LOADING, CHIRKY_ASSET_READY };
/* Immutable until the owning handle is released. Images are top-down RGBA8;
   sounds are interleaved signed PCM16; blobs include an extra NUL terminator. */
struct chirky_asset_view {
    const void *data;
    size_t size;
    unsigned width,height,rate,channels;
};

enum chirky_button {
    CHIRKY_BUTTON_LEFT,
    CHIRKY_BUTTON_RIGHT,
    CHIRKY_BUTTON_UP,
    CHIRKY_BUTTON_DOWN,
    CHIRKY_BUTTON_PRIMARY,
    CHIRKY_BUTTON_SECONDARY,
    CHIRKY_BUTTON_START,
    CHIRKY_BUTTON_MENU,
    CHIRKY_BUTTON_COUNT
};

struct chirky_input {
    bool buttons[CHIRKY_BUTTON_COUNT];
    bool button_pressed[CHIRKY_BUTTON_COUNT];
};

struct chirky_host_api {
    unsigned int abi_version;
    /* Logical playable viewport, excluding the CRT-safe border. Drawing is
       clipped to these bounds and translated to physical output by the host.
       Games must not add the physical border or calibrated display offset. */
    int screen_width;
    int screen_height;
    void *context;
    /* Drawing uses integer logical pixels with a bottom-left origin. Rectangle
       coverage is [x,x+width) by [y,y+height). The host clips it to the logical
       viewport before applying the physical CRT offset. */
    void (*fill_rect)(void *context, int x, int y, int width, int height,
                      unsigned char red, unsigned char green,
                      unsigned char blue);
    void (*play_sound)(void *context, const char *device, const char *path);
    /* Draws the built-in 5x7 font. It supports A-Z, a-z, 0-9, space, dash,
       slash, dot, colon and UTF-8 arrows U+2190 through U+2193; unsupported
       characters use the question glyph.
       x is the left edge and y is the bottom-left coordinate of the glyphs'
       top pixel row; subsequent rows descend by scale pixels. Characters
       advance by 6*scale. */
    void (*draw_text)(void *context, int x, int y, const char *text, int scale,
                      unsigned char red, unsigned char green,
                      unsigned char blue);
    /* Writes the mapped physical control for the most recently used input
       device (e.g. B, SELECT, X), NUL-terminated when capacity is nonzero.
       Query while drawing: remapping/device changes update the label live.
       Unknown controls use button/axis identifiers; unavailable is UNBOUND.
       This is presentation only; games still use logical button enums. */
    void (*button_label)(void *context, enum chirky_button action,
                         char *text, size_t capacity);
    /* Optional nested CPU scopes. NULL outside a bounded diagnostic capture. */
    void (*profile_scope)(void *context, const char *name, bool begin);
    chirky_asset (*asset_request)(void *context,const char *path,enum chirky_asset_type type);
    enum chirky_asset_state (*asset_status)(void *context,chirky_asset asset);
    struct chirky_asset_view (*asset_data)(void *context,chirky_asset asset);
    void (*asset_release)(void *context,chirky_asset asset);
    /* IMAGE source rectangles use top-left RGBA8 coordinates. Destination
       coordinates use the bottom-left logical viewport above. Source and
       destination sizes may differ and are nearest sampled, but pixel art
       should normally use width==sw and height==sh to preserve 1:1 texels. */
    void (*draw_sprite)(void *context,chirky_asset image,int x,int y,int width,int height,
                        int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,
                        unsigned char b,unsigned char a,bool flip_x);
    void (*sound_play)(void *context,chirky_asset sound);
    /* Optional eventually-consistent world-director transport. The host owns
       networking and credentials; games emit bounded JSON events and consume
       opaque, revisioned UTF-8 state without blocking a frame. */
    bool (*director_connect)(void *context,const char *url,const char *game,const char *world);
    bool (*director_event)(void *context,const char *json,size_t size);
    size_t (*director_state)(void *context,uint32_t after_revision,char *text,
                             size_t capacity,uint32_t *revision);
    /* Optional local persistent records, isolated by game and key. Names use
       letters, digits, '-' or '_', at most 95 bytes. Records are 1..65536 bytes.
       Read returns 0 if missing, invalid, unavailable or larger than capacity.
       Write replaces one record atomically and reports whether it was saved. */
    size_t (*save_read)(void *context,const char *game,const char *key,void *data,size_t capacity);
    bool (*save_write)(void *context,const char *game,const char *key,const void *data,size_t size);
    /* Optional GPU mesh service. count is a multiple of 3, at most 12288.
       Consumes vertices synchronously; does not retain the pointer. Calls are
       composited in submission order with sprites/rectangles. ambient: [0,1].
       Returns false when unsupported/invalid; no software rasterizer fallback. */
    bool (*draw_mesh)(void *context,const struct chirky_mesh_vertex *vertices,size_t count,float ambient);
    /* Copy a tightly packed top-down RGBA8 image into a READY, immutable asset.
       Intended for load-time atlases, not per-frame uploads. Returns 0 on invalid
       input or exhaustion. Release through asset_release; normal IMAGE lifetime. */
    chirky_asset (*image_create)(void *context,unsigned width,unsigned height,const void *rgba,size_t size);
    /* Same 5x7 font/advance as draw_text, with a black one-logical-pixel outline
       independent of scale. Optional; callers may compose the outline otherwise. */
    void (*draw_text_outlined)(void *context,int x,int y,const char *text,int scale,
                              unsigned char r,unsigned char g,unsigned char b);
    /* Camera-scaled sprite: each texel edge is rounded after x/y + edge*scale.
       Finite logical coordinates, uniform scale [0.25,16]; source/tint/flip as
       draw_sprite. Host applies its viewport offset, never the game. */
    void (*draw_sprite_projected)(void *context,chirky_asset image,float x,float y,float scale,
                                 int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,
                                 unsigned char b,unsigned char a,bool flip_x);
};

static inline void chirky_scope(const struct chirky_host_api *api,const char *name,bool begin)
{
    if(api->profile_scope)api->profile_scope(api->context,name,begin);
}

struct chirky_game_api {
    unsigned int abi_version;
    bool (*init)(const struct chirky_host_api *host, const char *config_path);
    void (*shutdown)(void);
    void (*update)(const struct chirky_input *input);
    void (*render)(void);
};

typedef const struct chirky_game_api *(*chirky_game_entry_fn)(void);

#endif
