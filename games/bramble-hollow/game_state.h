#ifndef BRAMBLE_HOLLOW_STATE_H
#define BRAMBLE_HOLLOW_STATE_H

#include "chirky.h"
#include <stdbool.h>
#include <stdint.h>

#define BRAMBLE_WORLD_W 768
#define BRAMBLE_WORLD_H 600
#define BRAMBLE_TREES 10
#define BRAMBLE_PLANTS 12
#define BRAMBLE_NPCS 6

enum bramble_phase { BRAMBLE_TITLE, BRAMBLE_PLAY, BRAMBLE_DIALOG, BRAMBLE_DIRECTOR };
enum bramble_weather { BRAMBLE_SUN, BRAMBLE_RAIN, BRAMBLE_MIST, BRAMBLE_WIND, BRAMBLE_WEATHER_COUNT };
enum bramble_facing { BRAMBLE_DOWN, BRAMBLE_LEFT, BRAMBLE_RIGHT, BRAMBLE_UP };
enum bramble_service { BRAMBLE_NO_SERVICE, BRAMBLE_SHOP, BRAMBLE_BAKERY, BRAMBLE_REST };

struct bramble_tree {
    int x, y;
    int hits;
    int regrow;
};

struct bramble_plant {
    int x, y;
    int stage;
    int growth;
};

struct bramble_npc {
    const char *name;
    int x, y;
    int sprite;
    enum bramble_service service;
};

struct bramble_director_state {
    unsigned long revision;
    char long_theme[96];
    char long_church_goal[96];
    char medium_event[96];
    char medium_shop_special[96];
    char short_focus[96];
    char zebra_line[128];
    char turtle_line[128];
    char cat_line[128];
    char sheep_line[128];
    char nun_line[128];
    int weather;
    int growth_boost;
};

struct bramble_state {
    enum bramble_phase phase;
    float x, y;
    int camera_x, camera_y;
    enum bramble_facing facing;
    bool moving, cycling;
    unsigned tick;
    unsigned day;
    int minute;
    enum bramble_weather weather;
    int weather_age;
    int growth_boost;
    int wood, seeds, coins, buns;
    int fire_ticks;
    int action_cooldown;
    int director_cursor;
    struct bramble_tree trees[BRAMBLE_TREES];
    struct bramble_plant plants[BRAMBLE_PLANTS];
    struct bramble_npc npcs[BRAMBLE_NPCS];
    struct bramble_director_state director;
    char config_dir[512];
    char director_path[640];
    char event_path[640];
    char dialog_name[32];
    char dialog_text[256];
    enum bramble_service dialog_service;
    char message[96];
    int message_ticks;
    chirky_asset player_art;
    chirky_asset friend_art;
};

extern struct bramble_state bramble;
extern const struct chirky_host_api *bramble_host;

void bramble_render(void);
void bramble_title_load(const char *config);
void bramble_title_free(void);
const char *bramble_context_prompt(void);

#endif
