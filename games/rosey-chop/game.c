#include "game_state.h"
#include "input_gate.h"
#include "asset_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct garden_state garden;
const struct chirky_host_api *host;
static struct chirky_input_gate transition_gate;
static const char *sound_names[]={"win","sting","storm","chop","swing","warning","jump"};
static chirky_asset sound_assets[7];

float clamp_value(float v, float low, float high) { return v < low ? low : v > high ? high : v; }
static float absolute(float v) { return v < 0 ? -v : v; }
static unsigned int random_value(void) { garden.random = garden.random * 1664525u + 1013904223u; return garden.random; }
static void sound(const char *name)
{
    if(host->sound_play) {
        for(int i=0;i<7;i++)if(!strcmp(name,sound_names[i])) {
            host->sound_play(host->context,sound_assets[i]);return;
        }
        return;
    }
    char path[640];
    snprintf(path, sizeof(path), "%s/%s.wav", garden.sound_root, name);
    if (host->play_sound) host->play_sound(host->context, garden.sound_device, path);
}

static char *trim(char *s)
{
    s += strspn(s, " \t\r\n");
    size_t n = strlen(s);
    while (n && strchr(" \t\r\n", s[n-1])) s[--n] = 0;
    return s;
}

static bool load_garden(const char *path)
{
    struct chirky_file source=chirky_file_open(host,path);
    FILE *f = source.stream;
    if (!f) return false;
    char line[256];
    int row = 0, spawns = 0;
    bool valid = true;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || (s[0] == '#' && s[1] == ' ')) continue;
        if (row >= GARDEN_ROWS || strlen(s) != GARDEN_COLS) { valid = false; break; }
        for (int col = 0; col < GARDEN_COLS; col++) {
            char c = s[col];
            if (!strchr(".Sdrpaw", c)) { valid = false; break; }
            if (c == 'S') { garden.spawn_x = col*TILE+8; garden.spawn_y = row*TILE+8; spawns++; }
            else if (c != '.') {
                garden.roses[garden.rose_count++] = (struct rose){col*TILE+8, row*TILE+8, c, false};
                if (c == 'd') garden.total++;
            }
        }
        row++;
    }
    valid = valid && !ferror(f) && row == GARDEN_ROWS && spawns == 1 && garden.total > 0;
    chirky_file_close(&source);
    return valid;
}

static void reset_run(void)
{
    garden.phase = PLAY;
    garden.elapsed = garden.chop = garden.jump_cooldown = garden.result_age = 0;
    garden.warning = garden.wasp_life = 0;
    garden.wasp_vx = garden.wasp_vy = garden.z = garden.vz = 0;
    garden.x = (float)garden.spawn_x; garden.y = (float)garden.spawn_y;
    garden.facing = 1; garden.moving = false;
    garden.remaining = garden.total;
    garden.next_wasp = garden.wasp_interval;
    garden.random = 0x726f7365u;
    memset(garden.petals, 0, sizeof(garden.petals));
    for (int i = 0; i < garden.rose_count; i++) garden.roses[i].cut = false;
}

static bool game_init(const struct chirky_host_api *api, const char *config)
{
    if (!api || api->abi_version != CHIRKY_ABI_VERSION || !api->fill_rect || !api->draw_text) return false;
    host = api;
    transition_gate=(struct chirky_input_gate){0};
    memset(&garden, 0, sizeof(garden));
    garden.storm_ticks = 75*60; garden.wasp_interval = 8*60; garden.wasp_duration = 6*60;
    char directory[512], level[640];
    if (!config || strlen(config) >= sizeof(directory)) return false;
    snprintf(directory, sizeof(directory), "%s", config);
    char *slash = strrchr(directory, '/');
    if (slash) *slash = 0; else snprintf(directory, sizeof(directory), ".");
    snprintf(level, sizeof(level), "%s/assets/level-01.txt", directory);
    if (snprintf(garden.sound_root, sizeof(garden.sound_root), "%s/assets", directory) >= (int)sizeof(garden.sound_root)) return false;
    snprintf(garden.sound_device, sizeof(garden.sound_device), "plughw:0,0");
    struct chirky_file source=chirky_file_open(host,config);
    FILE *f = source.stream;
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *key = trim(line), *equals = strchr(key, '=');
        if (!equals || *key == '#' || *key == ';') continue;
        *equals = 0; key = trim(key); char *value = trim(equals+1);
        int low = 0, high = 0, *setting = NULL;
        if (!strcmp(key, "storm_seconds")) { setting = &garden.storm_ticks; low = 15; high = 300; }
        if (!strcmp(key, "wasp_interval_seconds")) { setting = &garden.wasp_interval; low = 4; high = 60; }
        if (!strcmp(key, "wasp_duration_seconds")) { setting = &garden.wasp_duration; low = 2; high = 12; }
        if (setting) {
            char *end; long seconds = strtol(value, &end, 10);
            if (*end || end == value || seconds < low || seconds > high) { chirky_file_close(&source); return false; }
            *setting = (int)seconds*60;
        }
        if (!strcmp(key, "sound_device")) snprintf(garden.sound_device, sizeof(garden.sound_device), "%s", value);
    }
    bool valid = !ferror(f); chirky_file_close(&source);
    if (!valid || !load_garden(level)) return false;
    title_art_load(config);
    if(host->asset_request)for(int i=0;i<7;i++) {
        char path[640];snprintf(path,sizeof(path),"%s/%s.wav",garden.sound_root,sound_names[i]);
        sound_assets[i]=host->asset_request(host->context,path,CHIRKY_ASSET_SOUND);
    }
    reset_run(); garden.phase = TITLE;
    return true;
}

static void finish(enum phase result)
{
    garden.phase = result; garden.result_age = 0; garden.moving = false;
    sound(result == WON ? "win" : result == STUNG ? "sting" : "storm");
}

static void burst(const struct rose *rose)
{
    for (int i = 0, count = 0; i < MAX_PETALS && count < 9; i++) {
        if (garden.petals[i].life) continue;
        garden.petals[i] = (struct petal){(float)rose->x, (float)rose->y-9,
            ((int)(random_value()%101)-50)/35.f, -1.f-(random_value()%50)/40.f,
            25+(int)(random_value()%20), count%3 ? 0x45404e : 0xb6c889};
        count++;
    }
}

static void chop(void)
{
    garden.chop = 20;
    bool hit = false;
    for (int i = 0; i < garden.rose_count; i++) {
        struct rose *r = &garden.roses[i];
        float dx = r->x-garden.x, dy = r->y-garden.y;
        if (r->kind == 'd' && !r->cut && dx*dx+dy*dy <= 24*24) {
            r->cut = true; garden.remaining--; hit = true; burst(r);
        }
    }
    sound(hit ? "chop" : "swing");
}

static void update_wasp(void)
{
    if (!garden.wasp_life && !garden.warning && garden.elapsed >= garden.next_wasp) {
        garden.warning = 90;
        /* Arrive from the opposite horizontal edge, never on top of the player. */
        garden.wasp_x = garden.x < WORLD_W/2 ? WORLD_W-8.f : 8.f;
        garden.wasp_y = clamp_value(garden.y-36, 12, WORLD_H-12);
        garden.wasp_vx = garden.wasp_vy = 0;
        sound("warning");
    }
    if (garden.warning) {
        if (!--garden.warning) garden.wasp_life = garden.wasp_duration;
        return;
    }
    if (!garden.wasp_life) return;
    float dx = garden.x-garden.wasp_x, dy = garden.y-garden.wasp_y;
    /* L1 normalization deliberately keeps pursuit slower than running. */
    float distance = absolute(dx)+absolute(dy);
    if (distance < 1) distance = 1;
    garden.wasp_vx += (dx/distance*1.42f-garden.wasp_vx)*.055f;
    garden.wasp_vy += (dy/distance*1.42f-garden.wasp_vy)*.055f;
    garden.wasp_x += garden.wasp_vx; garden.wasp_y += garden.wasp_vy;
    dx = garden.x-garden.wasp_x; dy = garden.y-garden.wasp_y;
    if (dx*dx+dy*dy < 9*9 && garden.z < 7) { finish(STUNG); return; }
    if (!--garden.wasp_life) garden.next_wasp = garden.elapsed+garden.wasp_interval;
}

static void update_gameplay(const struct chirky_input *input)
{
    garden.tick++;
    for (int i = 0; i < MAX_PETALS; i++) {
        struct petal *p = &garden.petals[i];
        if (p->life) { p->life--; p->x += p->vx; p->y += p->vy; p->vy += .09f; }
    }
    if (garden.phase != PLAY) {
        garden.result_age++;
        if ((garden.phase == TITLE && chirky_title_pressed(input)) ||
            (garden.phase != TITLE && garden.result_age > 40 && input->button_pressed[CHIRKY_BUTTON_B])) reset_run();
        return;
    }
    /* A storm or sting takes precedence over a final chop on the same tick. */
    if (++garden.elapsed >= garden.storm_ticks) { finish(STORM); return; }
    int dx = input->buttons[CHIRKY_BUTTON_RIGHT]-input->buttons[CHIRKY_BUTTON_LEFT];
    int dy = input->buttons[CHIRKY_BUTTON_DOWN]-input->buttons[CHIRKY_BUTTON_UP];
    float speed = dx && dy ? 1.18f : 1.67f;
    garden.moving = dx || dy;
    if (dx) garden.facing = dx;
    garden.x = clamp_value(garden.x+dx*speed, 10, WORLD_W-10);
    garden.y = clamp_value(garden.y+dy*speed, 16, WORLD_H-10);
    if (garden.jump_cooldown) garden.jump_cooldown--;
    if (input->button_pressed[CHIRKY_BUTTON_Y] && garden.z == 0 && !garden.jump_cooldown) {
        garden.vz = 3.5f; garden.jump_cooldown = 36; sound("jump");
    }
    if (garden.vz || garden.z) {
        garden.z += garden.vz; garden.vz -= .25f;
        if (garden.z <= 0) { garden.z = garden.vz = 0; }
    }
    update_wasp();
    if (garden.phase != PLAY) return;
    if (garden.chop) garden.chop--;
    if (!garden.chop && (input->buttons[CHIRKY_BUTTON_B] || input->button_pressed[CHIRKY_BUTTON_B])) chop();
    if (!garden.remaining) finish(WON);
}

static void game_update(const struct chirky_input *input)
{
    enum phase before=garden.phase;
    struct chirky_input filtered;
    chirky_gate_filter(&transition_gate,input,&filtered);
    update_gameplay(&filtered);
    if(garden.phase!=before)chirky_gate_begin(&transition_gate);
}

static void game_shutdown(void)
{
    if(host && host->asset_release)for(int i=0;i<7;i++)host->asset_release(host->context,sound_assets[i]);
    memset(sound_assets,0,sizeof(sound_assets));
    title_art_free();memset(&garden,0,sizeof(garden));host=NULL;
}
static const struct chirky_game_api api = {
    .abi_version = CHIRKY_ABI_VERSION, .init = game_init, .shutdown = game_shutdown,
    .update = game_update, .render = garden_render
};
const struct chirky_game_api *chirky_game_entry(void) { return &api; }
