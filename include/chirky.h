#ifndef CHIRKY_H
#define CHIRKY_H

#ifndef __EMSCRIPTEN__
#include <linux/input.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHIRKY_ABI_VERSION 9

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
    CHIRKY_BUTTON_Y,
    CHIRKY_BUTTON_B,
    CHIRKY_BUTTON_A,
    CHIRKY_BUTTON_X,
    CHIRKY_BUTTON_L,
    CHIRKY_BUTTON_R,
    CHIRKY_BUTTON_START,
    CHIRKY_BUTTON_SELECT,
    CHIRKY_BUTTON_COUNT
};

struct chirky_input {
    /* Raw keyboard state is reserved for host setup/recovery. Games use buttons. */
    bool keys[0x300]; /* Linux KEY_MAX + 1; keep the native ABI layout. */
    bool pressed[0x300];
    bool buttons[CHIRKY_BUTTON_COUNT];
    bool button_pressed[CHIRKY_BUTTON_COUNT];
    bool controller_pressed;
};

struct chirky_host_api {
    unsigned int abi_version;
    /* Logical playable viewport, excluding the CRT-safe border. Drawing is
       clipped to these bounds and translated to physical output by the host. */
    int screen_width;
    int screen_height;
    void *context;
    void (*fill_rect)(void *context, int x, int y, int width, int height,
                      unsigned char red, unsigned char green,
                      unsigned char blue);
    void (*play_sound)(void *context, const char *device, const char *path);
    void (*draw_text)(void *context, int x, int y, const char *text, int scale,
                      unsigned char red, unsigned char green,
                      unsigned char blue);
    void (*button_label)(void *context, enum chirky_button action,
                         char *text, size_t capacity);
    /* Optional nested CPU scopes. NULL outside a bounded diagnostic capture. */
    void (*profile_scope)(void *context, const char *name, bool begin);
    chirky_asset (*asset_request)(void *context,const char *path,enum chirky_asset_type type);
    enum chirky_asset_state (*asset_status)(void *context,chirky_asset asset);
    struct chirky_asset_view (*asset_data)(void *context,chirky_asset asset);
    void (*asset_release)(void *context,chirky_asset asset);
    void (*draw_sprite)(void *context,chirky_asset image,int x,int y,int width,int height,
                        int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,
                        unsigned char b,unsigned char a,bool flip_x);
    void (*sound_play)(void *context,chirky_asset sound);
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
