#ifndef CHIRKY_H
#define CHIRKY_H

#include <linux/input.h>
#include <stdbool.h>
#include <stddef.h>

#define CHIRKY_ABI_VERSION 7

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
    bool keys[KEY_MAX + 1];
    bool pressed[KEY_MAX + 1];
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
};

struct chirky_game_api {
    unsigned int abi_version;
    bool (*init)(const struct chirky_host_api *host, const char *config_path);
    void (*shutdown)(void);
    void (*update)(const struct chirky_input *input);
    void (*render)(void);
};

typedef const struct chirky_game_api *(*chirky_game_entry_fn)(void);

#endif
