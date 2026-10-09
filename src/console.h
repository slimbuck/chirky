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
/* Host presentation hints; device snapshots and game controls stay unchanged. */
enum console_input_option { CONSOLE_HIDE_KEYBOARD_MAPPING=1, CONSOLE_HIDE_KEYBOARD_TEST=2 };
struct console_game { const char *id, *name; bool diagnostic; };
struct console_display { int x,y,offset_x,offset_y; };
enum mapping_save_result { MAPPING_CONFLICT=-1, MAPPING_FAILED=0, MAPPING_SAVED=1 };
/* Callbacks perform platform services only. A load completes with
   chirky_console_loaded; cancellation must discard any outstanding load. */
struct console_services {
    void *context;
    bool (*load)(void *,int);
    void (*unload)(void *);
    enum mapping_save_result (*save_keyboard)(void *,unsigned,const struct controller_binding *);
    void (*action)(void *,enum console_action);
    void (*display)(void *,const struct console_display *);
    bool (*save_display)(void *);
    /* Returns the model profile, or -1 if this connection is gone. */
    int (*controller_info)(void *,uint32_t,char *,size_t);
    /* A NULL binding array requests the SNES preset. */
    enum mapping_save_result (*save_controller)(void *,uint32_t,enum controller_profile,const struct controller_binding *);
};
struct chirky_console {
    struct console_services services;
    unsigned capabilities;
    unsigned input_options;
    struct launcher_config launcher;
    int selected_game,settings_option,selected_option,pause_option,display_option;
    /* Presentation only: rows displaced from the fixed selection in shared menus. */
    float menu_offset,menu_velocity;
    bool settings_menu,controller_settings,display_settings,input_test,paused;
    bool game_active,loading,diagnostic,ui_wait_release;
    struct binding_setup setup;
    bool controller_options;
    uint32_t controller_id;
    unsigned controller_count;
    uint32_t controller_ids[CHIRKY_INPUT_DEVICES],controller_activity_id;
    unsigned controller_activity_frames;
    int controller_option;
    char controller_name[96];
    struct chirky_input_gate transition_gate;
    const char *settings_message;
    unsigned controller_menu_chord_frames;
    struct console_display display,saved_display;
    bool timing_start_held,timing_start_toggled;
    uint64_t timing_start_us;
};
struct console_input {
    const struct chirky_input *logical;
    unsigned options;
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
void chirky_console_controller_press(struct chirky_console *,uint32_t);
void chirky_console_capture_controller(struct chirky_console *,uint32_t,struct controller_binding);
void chirky_console_timing(struct chirky_console *,bool,uint64_t);
/* True permits one game update. Rendering and I/O stay with the host. */
bool chirky_console_update(struct chirky_console *,const struct console_input *);
void chirky_console_render(struct chirky_console *,const struct chirky_host_api *,
    struct splash_art *,const struct chirky_input *);
void chirky_console_draw_display(const struct chirky_console *,const struct chirky_host_api *);
#endif
