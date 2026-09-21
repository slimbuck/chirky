#ifndef ROSEY_CHOP_STATE_H
#define ROSEY_CHOP_STATE_H
#include "chirky.h"

#define GARDEN_COLS 24
#define GARDEN_ROWS 16
#define TILE 16
#define WORLD_W (GARDEN_COLS * TILE)
#define WORLD_H (GARDEN_ROWS * TILE)
#define MAX_ROSES (GARDEN_COLS * GARDEN_ROWS)
#define MAX_PETALS 64

enum phase { TITLE, PLAY, WON, STUNG, STORM };
struct rose { int x, y; char kind; bool cut; };
struct petal { float x, y, vx, vy; int life; unsigned int colour; };
struct garden_state {
    enum phase phase;
    int tick, elapsed, remaining, total, rose_count, chop, jump_cooldown, result_age;
    int storm_ticks, wasp_interval, wasp_duration, next_wasp, warning, wasp_life;
    float x, y, z, vz, wasp_x, wasp_y, wasp_vx, wasp_vy;
    int spawn_x, spawn_y, facing;
    bool moving;
    unsigned int random;
    struct rose roses[MAX_ROSES];
    struct petal petals[MAX_PETALS];
    char sound_root[512], sound_device[128];
};
extern struct garden_state garden;
extern const struct chirky_host_api *host;
float clamp_value(float value, float low, float high);
void garden_render(void);
void title_art_load(const char *config);
void title_art_free(void);
#endif
