#include "game_state.h"
#include "input_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct chirky_host_api *host;
static struct chirky_input_gate transition_gate;
struct settings settings;
struct level level;
struct content content;
int current_level;
struct particle particles[MAX_PARTICLES];
unsigned int random_state = 0x51a7u;
enum phase phase;
float player_x, player_y, velocity_x, velocity_y, camera_x, camera_y;
float respawn_x, respawn_y;
int collected_shards, deaths, coyote_timer, jump_buffer, dash_timer;
int death_timer, title_timer, win_timer, frame_number, facing;
bool on_ground, touching_left, touching_right, dash_available;

float fmax_zero(float value) { return value>0?value:0; }
float absolute(float value) { return value < 0 ? -value : value; }
float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}
static unsigned int random_value(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity > 0) snprintf(destination, capacity, "%s", source);
}

static bool load_level(const char *path)
{
    struct grid grid={0};
    if (!grid_load(path,&grid)) return false;
    struct level next={.width=grid.width,.height=grid.height,.tiles=grid.pixels};
    size_t size=(size_t)grid.width*grid.height;
    next.original=malloc(size);
    if (!next.original) { free(grid.pixels); return false; }
    for (int row=0;row<grid.height;row++) {
        for (int column=0;column<grid.width;column++) {
            char *tile=&grid.pixels[(size_t)row*grid.width+column];
            if (*tile=='S') {
                next.initial_x=column*TILE; next.initial_y=(grid.height-1-row)*TILE;
                *tile='.';
            } else if (*tile=='o') next.total_shards++;
        }
    }
    memcpy(next.original,next.tiles,size);
    free(level.tiles); free(level.original);
    level=next;
    return true;
}

static char *tile_pointer(int tile_x, int tile_y)
{
    if (tile_x < 0 || tile_x >= level.width || tile_y < 0 || tile_y >= level.height) return NULL;
    int row = level.height - 1 - tile_y;
    return &level.tiles[(size_t)row * level.width + tile_x];
}

char tile_at(int tile_x, int tile_y)
{
    char *tile = tile_pointer(tile_x, tile_y);
    if (tile == NULL) return tile_x < 0 || tile_x >= level.width ? '#' : '.';
    return *tile;
}

static bool solid_at_rect(float x, float y, int width, int height)
{
    int left = (int)x / TILE;
    int right = (int)(x + width - .01f) / TILE;
    int bottom = (int)y / TILE;
    int top = (int)(y + height - .01f) / TILE;
    for (int ty = bottom; ty <= top; ++ty)
        for (int tx = left; tx <= right; ++tx)
            if (tile_at(tx, ty) == '#') return true;
    return false;
}

static bool touching_tile(char wanted)
{
    int left = (int)player_x / TILE;
    int right = (int)(player_x + PLAYER_WIDTH - 1) / TILE;
    int bottom = (int)player_y / TILE;
    int top = (int)(player_y + PLAYER_HEIGHT - 1) / TILE;
    for (int ty = bottom; ty <= top; ++ty)
        for (int tx = left; tx <= right; ++tx)
            if (tile_at(tx,ty) == wanted) return true;
    return false;
}

static void add_particle(float x, float y, float vx, float vy, int life, struct colour colour)
{
    for (int index = 0; index < MAX_PARTICLES; ++index) {
        if (particles[index].life <= 0) {
            particles[index] = (struct particle){x,y,vx,vy,life,colour};
            return;
        }
    }
}

static void burst(float x, float y, int count, struct colour colour)
{
    for (int index = 0; index < count; ++index) {
        float vx = ((int)(random_value() % 200) - 100) / 70.0f;
        float vy = ((int)(random_value() % 150) + 30) / 70.0f;
        add_particle(x, y, vx, vy, 18 + (int)(random_value() % 18), colour);
    }
}

static void update_particles(void)
{
    for (int index = 0; index < MAX_PARTICLES; ++index) {
        struct particle *particle = &particles[index];
        if (particle->life <= 0) continue;
        particle->x += particle->vx; particle->y += particle->vy;
        particle->vy -= .05f; particle->life--;
    }
}

static bool move_axis(float amount, bool horizontal)
{
    int steps = (int)absolute(amount) + 1;
    float movement = amount / steps;
    for (int step = 0; step < steps; ++step) {
        float next_x = player_x + (horizontal ? movement : 0);
        float next_y = player_y + (horizontal ? 0 : movement);
        if (solid_at_rect(next_x, next_y, PLAYER_WIDTH, PLAYER_HEIGHT)) return false;
        player_x = next_x; player_y = next_y;
    }
    return true;
}

static void play(const char *path)
{
    host->play_sound(host->context, settings.sound_device, path);
}

static void respawn(void)
{
    player_x = respawn_x; player_y = respawn_y;
    velocity_x = velocity_y = 0;
    dash_timer = jump_buffer = coyote_timer = 0;
    dash_available = true;
    phase = PHASE_PLAY; death_timer = 0;
    on_ground = touching_left = touching_right = false;
}

static void begin_level(void)
{
    memcpy(level.tiles, level.original, (size_t)level.width * level.height);
    collected_shards = 0;
    respawn_x = level.initial_x; respawn_y = level.initial_y;
    camera_x = clampf(respawn_x - 60, 0, fmax_zero(level.width * TILE - host->screen_width));
    camera_y = clampf(respawn_y - 60, 0, fmax_zero(level.height * TILE - host->screen_height));
    memset(particles, 0, sizeof(particles));
    respawn();
}

static void new_run(void)
{
    if (!load_level(content.levels[settings.start_level].path)) return;
    current_level=settings.start_level; deaths=0; facing=1;
    begin_level();
}

static void advance_level(void)
{
    if (current_level+1==content.level_count) { new_run(); return; }
    if (!load_level(content.levels[current_level+1].path)) return;
    current_level++;
    begin_level();
}

static void die(void)
{
    if (phase != PHASE_PLAY) return;
    phase = PHASE_DEAD; death_timer = 34; deaths++;
    burst(player_x + 6, player_y + 7, 20, settings.hazard);
    play(settings.death_sound);
}

static void collect_world_items(void)
{
    int left = (int)player_x / TILE;
    int right = (int)(player_x + PLAYER_WIDTH - 1) / TILE;
    int bottom = (int)player_y / TILE;
    int top = (int)(player_y + PLAYER_HEIGHT - 1) / TILE;
    for (int ty = bottom; ty <= top; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            char *tile = tile_pointer(tx,ty);
            if (tile == NULL) continue;
            if (*tile == 'o') {
                *tile = '.'; collected_shards++;
                burst(tx*TILE+4,ty*TILE+4,10,settings.phosphor);
                play(settings.shard_sound);
                dash_available = true;
            } else if (*tile == 'C' && (respawn_x != tx*TILE - 2 || respawn_y != (ty+1)*TILE)) {
                respawn_x = tx*TILE - 2; respawn_y = (ty+1)*TILE;
                burst(tx*TILE+4,ty*TILE+8,18,settings.amber);
                play(settings.checkpoint_sound);
            } else if (*tile == 'E' && collected_shards == level.total_shards) {
                phase = PHASE_WIN; win_timer = 0;
                burst(tx*TILE+4,ty*TILE+8,32,settings.phosphor);
                play(settings.win_sound);
            }
        }
    }
}

static bool pressed_jump(const struct chirky_input *input)
{
    return input->button_pressed[CHIRKY_BUTTON_B];
}

static bool held_jump(const struct chirky_input *input)
{
    return input->buttons[CHIRKY_BUTTON_B];
}

static void update_play(const struct chirky_input *input)
{
    if (input->button_pressed[CHIRKY_BUTTON_L]) { respawn(); return; }
    int direction = 0;
    if (input->buttons[CHIRKY_BUTTON_LEFT]) direction--;
    if (input->buttons[CHIRKY_BUTTON_RIGHT]) direction++;
    if (direction) facing = direction;

    if (pressed_jump(input)) jump_buffer = settings.jump_buffer_frames;
    else if (jump_buffer > 0) jump_buffer--;
    if (on_ground) coyote_timer = settings.coyote_frames;
    else if (coyote_timer > 0) coyote_timer--;

    if (jump_buffer > 0 && (coyote_timer > 0 || touching_left || touching_right)) {
        velocity_y = settings.jump_speed;
        if (!on_ground && (touching_left || touching_right))
            velocity_x = touching_left ? settings.max_run_speed * 1.25f : -settings.max_run_speed * 1.25f;
        jump_buffer = coyote_timer = 0;
        play(settings.jump_sound);
        burst(player_x+6,player_y,5,settings.edge);
    }

    bool dash_pressed = input->button_pressed[CHIRKY_BUTTON_Y];
    if (dash_pressed && dash_available && dash_timer == 0) {
        dash_timer = settings.dash_frames;
        dash_available = false;
        velocity_x = facing * settings.dash_speed;
        velocity_y = 0;
        play(settings.dash_sound);
        burst(player_x+6,player_y+7,10,settings.amber);
    }

    if (dash_timer > 0) {
        dash_timer--;
        velocity_x = facing * settings.dash_speed;
        velocity_y = 0;
        if ((frame_number & 1) == 0)
            add_particle(player_x + (facing < 0 ? PLAYER_WIDTH : 0), player_y+7,
                         -facing*.3f, 0, 10, settings.edge);
    } else {
        velocity_x += direction * settings.move_accel;
        velocity_x = clampf(velocity_x, -settings.max_run_speed, settings.max_run_speed);
        velocity_x *= on_ground && direction == 0 ? settings.ground_friction : settings.air_drag;
        velocity_y -= settings.gravity;
        if (velocity_y < -settings.max_fall_speed) velocity_y = -settings.max_fall_speed;
        if (!held_jump(input) && velocity_y > 2.0f) velocity_y *= .58f;
    }

    if (!move_axis(velocity_x, true)) velocity_x = 0;
    if (!move_axis(velocity_y, false)) velocity_y = 0;
    on_ground = solid_at_rect(player_x,player_y-1,PLAYER_WIDTH,PLAYER_HEIGHT);
    touching_left = solid_at_rect(player_x-1,player_y,PLAYER_WIDTH,PLAYER_HEIGHT);
    touching_right = solid_at_rect(player_x+1,player_y,PLAYER_WIDTH,PLAYER_HEIGHT);
    if (on_ground) dash_available = true;
    if ((touching_left || touching_right) && velocity_y < -1.2f) velocity_y = -1.2f;

    if (touching_tile('^') || player_y < -24) die();
    else collect_world_items();

    float maximum_camera = fmax_zero(level.width * TILE - host->screen_width);
    float target = clampf(player_x - host->screen_width*.42f, 0, maximum_camera);
    camera_x += (target-camera_x)*settings.camera_lag;
    /* Leave room above the level and below the HUD, even during an upward jump.
       Smooth ordinary tracking, but never let camera lag hide the player's head. */
    const float top_margin=40;
    float minimum_camera_y=fmax_zero(player_y+PLAYER_HEIGHT+top_margin-host->screen_height);
    float maximum_camera_y=fmax_zero(level.height*TILE+top_margin-host->screen_height);
    if(minimum_camera_y>maximum_camera_y)maximum_camera_y=minimum_camera_y;
    target=clampf(player_y-host->screen_height*.42f,0,maximum_camera_y);
    camera_y += (target-camera_y)*settings.camera_lag;
    if(camera_y<minimum_camera_y)camera_y=minimum_camera_y;
}

static void game_shutdown(void)
{
    title_art_free();
    free(level.tiles); free(level.original);
    memset(&level,0,sizeof(level)); content_free(&content); host=NULL;
}

static bool game_init(const struct chirky_host_api *host_api, const char *config_path)
{
    transition_gate=(struct chirky_input_gate){0};
    host=host_api;
    if (host->abi_version!=CHIRKY_ABI_VERSION || !load_settings(config_path) ||
        !content_load(settings.content,&content)) { game_shutdown(); return false; }
    const char *required[]={"player-idle","player-run","player-jump","player-fall","player-dash",
        "player-death","platform","platform-top","platform-detail","hazard","shard",
        "checkpoint","checkpoint-active","portal-locked","portal-active","dash-trail",
        "particle","machinery","lamp","spark"};
    for (size_t i=0;i<sizeof(required)/sizeof(*required);i++) {
        if (!content_animation(&content,required[i])) {
            fprintf(stderr,"phosphor-run: missing animation %s\n",required[i]);
            game_shutdown(); return false;
        }
    }
    if (settings.start_level<0 || settings.start_level>=content.level_count) settings.start_level=0;
    current_level=settings.start_level;
    if (!load_level(content.levels[current_level].path)) { game_shutdown(); return false; }
    title_art_load(config_path);
    begin_level();
    frame_number=0; phase=PHASE_TITLE; title_timer=0; facing=1; deaths=0;
    return true;
}

static void update_gameplay(const struct chirky_input *input)
{
    frame_number++; update_particles();
    if (phase==PHASE_TITLE) {
        title_timer++;
        if (chirky_title_pressed(input)) new_run();
    } else if (phase==PHASE_PLAY) update_play(input);
    else if (phase==PHASE_DEAD) {
        if (--death_timer<=0) respawn();
    } else if (phase==PHASE_WIN) {
        win_timer++;
        if (input->button_pressed[CHIRKY_BUTTON_B]) advance_level();
    }
}

static void game_render(void)
{
    if (phase==PHASE_TITLE) render_title(); else render_game();
}

static void game_update(const struct chirky_input *input)
{
    enum phase before=phase;int level_before=current_level;
    struct chirky_input filtered;
    chirky_gate_filter(&transition_gate,input,&filtered);
    update_gameplay(&filtered);
    if(phase!=before || current_level!=level_before)chirky_gate_begin(&transition_gate);
}

static const struct chirky_game_api api={
    .abi_version=CHIRKY_ABI_VERSION,.init=game_init,.shutdown=game_shutdown,
    .update=game_update,.render=game_render
};

const struct chirky_game_api *chirky_game_entry(void) { return &api; }
