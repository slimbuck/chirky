#ifndef PHOSPHOR_GAME_STATE_H
#define PHOSPHOR_GAME_STATE_H
#include "two_forty.h"
#include "assets.h"

/* Internal runtime boundary. Gameplay owns mutable state; settings initializes
   configuration, assets owns loaded data, and rendering only reads state.
   Collision dimensions deliberately stay independent of animation artwork. */

#define TILE 8
#define MAX_PARTICLES 96
#define PLAYER_WIDTH 12
#define PLAYER_HEIGHT 14

enum phase { PHASE_TITLE, PHASE_PLAY, PHASE_DEAD, PHASE_WIN };

struct colour { unsigned char r, g, b; };

struct settings {
    float move_accel, max_run_speed, ground_friction, air_drag;
    float gravity, max_fall_speed, jump_speed, dash_speed, camera_lag;
    int coyote_frames, jump_buffer_frames, dash_frames;
    char content[512], sound_device[128];
    int start_level;
    char jump_sound[512], dash_sound[512], shard_sound[512];
    char checkpoint_sound[512], death_sound[512], win_sound[512];
    struct colour background, deep, platform, edge, phosphor, amber, hazard, paper;
};

struct level {
    int width, height;
    char *tiles;
    char *original;
    int total_shards;
    float initial_x, initial_y;
};

struct particle {
    float x, y, vx, vy;
    int life;
    struct colour colour;
};

extern const struct two_forty_host_api *host;
extern struct settings settings;
extern struct level level;
extern struct content content;
extern int current_level;
extern struct particle particles[MAX_PARTICLES];
extern unsigned int random_state;
extern enum phase phase;
extern float player_x, player_y, velocity_x, velocity_y, camera_x, camera_y;
extern float respawn_x, respawn_y;
extern int collected_shards, deaths, coyote_timer, jump_buffer, dash_timer;
extern int death_timer, title_timer, win_timer, frame_number, facing;
extern bool on_ground, touching_left, touching_right, dash_available;


float absolute(float value);
float clampf(float value, float low, float high);
float fmax_zero(float value);
void copy_text(char *destination, size_t capacity, const char *source);
bool load_settings(const char *path);
char tile_at(int x, int y);
void render_title(void);
void render_game(void);
#endif
