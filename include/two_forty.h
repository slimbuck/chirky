#ifndef TWO_FORTY_H
#define TWO_FORTY_H

#include <linux/input.h>
#include <stdbool.h>
#include <stddef.h>

#define TWO_FORTY_ABI_VERSION 6

enum two_forty_button {
    TWO_FORTY_BUTTON_LEFT,
    TWO_FORTY_BUTTON_RIGHT,
    TWO_FORTY_BUTTON_UP,
    TWO_FORTY_BUTTON_DOWN,
    TWO_FORTY_BUTTON_Y,
    TWO_FORTY_BUTTON_B,
    TWO_FORTY_BUTTON_A,
    TWO_FORTY_BUTTON_X,
    TWO_FORTY_BUTTON_L,
    TWO_FORTY_BUTTON_R,
    TWO_FORTY_BUTTON_START,
    TWO_FORTY_BUTTON_SELECT,
    TWO_FORTY_BUTTON_COUNT
};

struct two_forty_input {
    /* Raw keyboard state is reserved for host setup/recovery. Games use buttons. */
    bool keys[KEY_MAX + 1];
    bool pressed[KEY_MAX + 1];
    bool buttons[TWO_FORTY_BUTTON_COUNT];
    bool button_pressed[TWO_FORTY_BUTTON_COUNT];
    bool controller_pressed;
};

struct two_forty_host_api {
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
    void (*button_label)(void *context, enum two_forty_button action,
                         char *text, size_t capacity);
};

struct two_forty_game_api {
    unsigned int abi_version;
    bool (*init)(const struct two_forty_host_api *host, const char *config_path);
    void (*shutdown)(void);
    void (*update)(const struct two_forty_input *input);
    void (*render)(void);
};

typedef const struct two_forty_game_api *(*two_forty_game_entry_fn)(void);

#endif
