#ifndef CHIRKY_CONSOLE_H
#define CHIRKY_CONSOLE_H

#include "chirky.h"
#include "input_setup.h"
#include "input_gate.h"
#include "launcher_config.h"
#include "splash_art.h"

enum console_screen { SCREEN_LAUNCHER, SCREEN_SETTINGS, SCREEN_INPUT, SCREEN_SETUP,
    SCREEN_TEST, SCREEN_DISPLAY, SCREEN_GAME, SCREEN_PAUSE };
enum console_action { CONSOLE_SETTINGS=-1, CONSOLE_POWER=-2, CONSOLE_INPUT=-3,
    CONSOLE_DISPLAY=-4, CONSOLE_FULLSCREEN=-6, CONSOLE_SOUND=-7, CONSOLE_TIMING=-8 };
enum console_capability { CONSOLE_CAN_POWER=1, CONSOLE_CAN_DISPLAY=2,
    CONSOLE_CAN_FULLSCREEN=4, CONSOLE_CAN_SOUND=8, CONSOLE_CAN_TIMING=16 };
struct console_game { const char *id, *name; bool diagnostic; };
struct console_display { int x,y,offset_x,offset_y; };
/* Callbacks perform platform services only. A load completes with
   chirky_console_loaded; cancellation must discard any outstanding load. */
struct console_services {
    void *context;
    bool (*load)(void *,int);
    void (*unload)(void *);
    bool (*save_mapping)(void *,bool,const struct controller_binding *);
    void (*action)(void *,enum console_action);
    void (*display)(void *,const struct console_display *);
    bool (*save_display)(void *);
};
struct chirky_console {
    struct console_services services;
    unsigned capabilities;
    struct launcher_config launcher;
    int selected_game,settings_option,selected_option,pause_option,display_option;
    /* Presentation only: rows displaced from the fixed selection in shared menus. */
    float menu_offset,menu_velocity;
    bool settings_menu,controller_settings,display_settings,input_test,paused;
    bool game_active,loading,diagnostic,ui_wait_release;
    struct binding_setup setup;
    struct chirky_input_gate transition_gate;
    const char *settings_message;
    unsigned controller_menu_chord_frames;
    struct console_display display,saved_display;
    bool timing_start_held,timing_start_toggled;
    uint64_t timing_start_us;
};
struct console_input {
    const struct chirky_input *logical;
    bool keyboard_held,controller_held,cancel,recovery;
    unsigned controller_buttons;
    uint64_t now_us;
};
void chirky_console_catalog(struct chirky_console *,const struct console_game *,int,unsigned);
enum console_screen chirky_console_screen(const struct chirky_console *);
void chirky_console_block(struct chirky_console *);
bool chirky_console_launch(struct chirky_console *,int,bool);
void chirky_console_loaded(struct chirky_console *,bool);
void chirky_console_home(struct chirky_console *,bool);
void chirky_console_open(struct chirky_console *,enum console_action);
void chirky_console_pause(struct chirky_console *);
void chirky_console_capture(struct chirky_console *,struct controller_binding);
void chirky_console_timing(struct chirky_console *,bool,uint64_t);
/* True permits one game update. Rendering and I/O stay with the host. */
bool chirky_console_update(struct chirky_console *,const struct console_input *);
void chirky_console_render(struct chirky_console *,const struct chirky_host_api *,
    struct splash_art *,unsigned,unsigned,const char *,const char *);
void chirky_console_draw_display(const struct chirky_console *,const struct chirky_host_api *);
#endif
