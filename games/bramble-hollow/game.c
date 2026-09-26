#include "game_state.h"
#include "asset_file.h"
#include "input_gate.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bramble_state bramble;
const struct chirky_host_api *bramble_host;
static struct chirky_input_gate input_gate;
static int director_refresh_ticks = 300;

static int clampi(int value, int low, int high)
{ return value < low ? low : value > high ? high : value; }

static int distance2(float x, float y, int bx, int by)
{
    int dx = (int)x - bx, dy = (int)y - by;
    return dx * dx + dy * dy;
}

static char *trim(char *text)
{
    text += strspn(text, " \t\r\n");
    size_t length = strlen(text);
    while (length && strchr(" \t\r\n", text[length - 1])) text[--length] = 0;
    return text;
}

static void copy_clean(char *target, size_t capacity, const char *source)
{
    size_t out = 0;
    for (; *source && out + 1 < capacity; source++) {
        unsigned char c = (unsigned char)*source;
        if (c >= 32 && c < 127 && c != '=') {
            if (strchr(",:;'\"<>", c)) c = ' ';
            target[out++] = (char)c;
        }
    }
    target[out] = 0;
}

static int weather_value(const char *name)
{
    static const char *names[] = {"sun", "rain", "mist", "wind"};
    for (int i = 0; i < BRAMBLE_WEATHER_COUNT; i++) if (!strcmp(name, names[i])) return i;
    return -1;
}

static const char *weather_name(enum bramble_weather weather)
{
    static const char *names[] = {"sun", "rain", "mist", "wind"};
    return names[clampi(weather, 0, BRAMBLE_WEATHER_COUNT - 1)];
}

static void show_message(const char *message)
{
    copy_clean(bramble.message, sizeof(bramble.message), message);
    bramble.message_ticks = 180;
}

static void log_event(const char *kind, const char *detail)
{
    FILE *file = fopen(bramble.event_path, "ab");
    if (!file) return;
    fprintf(file, "{\"tick\":%u,\"day\":%u,\"kind\":\"%s\",\"detail\":\"%s\"}\n",
            bramble.tick, bramble.day, kind, detail);
    fclose(file);
}

static void director_defaults(struct bramble_director_state *state)
{
    memset(state, 0, sizeof(*state));
    state->weather = BRAMBLE_SUN;
    state->growth_boost = 1;
    copy_clean(state->long_theme, sizeof(state->long_theme),
               "Small kindnesses are restoring Bramble Hollow.");
    copy_clean(state->long_church_goal, sizeof(state->long_church_goal),
               "The stained-glass window needs seven bright memories.");
    copy_clean(state->medium_event, sizeof(state->medium_event),
               "Market day is being prepared beside the bridge.");
    copy_clean(state->medium_shop_special, sizeof(state->medium_shop_special),
               "Zara trades three seed packets for two pieces of wood.");
    copy_clean(state->short_focus, sizeof(state->short_focus),
               "Meet the neighbours and warm the cottage fire.");
    copy_clean(state->zebra_line, sizeof(state->zebra_line),
               "Morning! The lane is perfect for a bicycle ride.");
    copy_clean(state->turtle_line, sizeof(state->turtle_line),
               "Rain helps seedlings, but a kind word helps gardeners.");
    copy_clean(state->cat_line, sizeof(state->cat_line),
               "The first berry buns are nearly cool enough to share.");
    copy_clean(state->sheep_line, sizeof(state->sheep_line),
               "I found a story about the church window in the library.");
    copy_clean(state->nun_line, sizeof(state->nun_line),
               "Every coloured pane remembers someone who helped here.");
}

static bool load_director(void)
{
    FILE *file = fopen(bramble.director_path, "rb");
    if (!file) return false;
    struct bramble_director_state next = bramble.director;
    bool version = false;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *key = trim(line), *separator = strchr(key, '=');
        if (!separator || !*key || *key == '#' || *key == ';') continue;
        *separator = 0;
        char *value = trim(separator + 1);
        key = trim(key);
        if (!strcmp(key, "version")) version = !strcmp(value, "1");
        else if (!strcmp(key, "revision")) next.revision = strtoul(value, NULL, 10);
        else if (!strcmp(key, "weather")) {
            int weather = weather_value(value);
            if (weather >= 0) next.weather = weather;
        } else if (!strcmp(key, "growth_boost")) next.growth_boost = clampi(atoi(value), 1, 3);
#define COPY_FIELD(name) else if (!strcmp(key, #name)) copy_clean(next.name, sizeof(next.name), value)
        COPY_FIELD(long_theme);
        COPY_FIELD(long_church_goal);
        COPY_FIELD(medium_event);
        COPY_FIELD(medium_shop_special);
        COPY_FIELD(short_focus);
        COPY_FIELD(zebra_line);
        COPY_FIELD(turtle_line);
        COPY_FIELD(cat_line);
        COPY_FIELD(sheep_line);
        COPY_FIELD(nun_line);
#undef COPY_FIELD
    }
    bool complete = !ferror(file) && version;
    fclose(file);
    if (!complete || next.revision < bramble.director.revision) return false;
    bool changed = next.revision > bramble.director.revision;
    bramble.director = next;
    if (changed) {
        bramble.weather = (enum bramble_weather)next.weather;
        bramble.growth_boost = next.growth_boost;
    }
    return changed;
}

static void init_world(void)
{
    static const int tree_positions[BRAMBLE_TREES][2] = {
        {74, 280}, {125, 338}, {205, 70}, {270, 118}, {282, 390},
        {402, 470}, {590, 410}, {680, 330}, {710, 500}, {610, 525}
    };
    for (int i = 0; i < BRAMBLE_TREES; i++) {
        bramble.trees[i].x = tree_positions[i][0];
        bramble.trees[i].y = tree_positions[i][1];
    }
    for (int i = 0; i < BRAMBLE_PLANTS; i++) {
        bramble.plants[i].x = 176 + (i % 4) * 18;
        bramble.plants[i].y = 168 + (i / 4) * 18;
        bramble.plants[i].stage = i < 2 ? i + 1 : 0;
        bramble.plants[i].growth = 0;
    }
    bramble.npcs[0] = (struct bramble_npc){"Zara", 385, 205, 0, BRAMBLE_SHOP};
    bramble.npcs[1] = (struct bramble_npc){"Moss", 225, 220, 1, BRAMBLE_NO_SERVICE};
    bramble.npcs[2] = (struct bramble_npc){"Maple", 408, 286, 2, BRAMBLE_BAKERY};
    bramble.npcs[3] = (struct bramble_npc){"Woolsey", 345, 365, 3, BRAMBLE_NO_SERVICE};
    bramble.npcs[4] = (struct bramble_npc){"Sister Wren", 650, 205, 4, BRAMBLE_NO_SERVICE};
    bramble.npcs[5] = (struct bramble_npc){"Sister Pippa", 688, 226, 5, BRAMBLE_NO_SERVICE};
}

static void begin_play(void)
{
    bramble.phase = BRAMBLE_PLAY;
    bramble.x = 142; bramble.y = 145;
    bramble.facing = BRAMBLE_DOWN;
    bramble.wood = 0; bramble.seeds = 5; bramble.coins = 0; bramble.buns = 0;
    bramble.cycling = false; bramble.fire_ticks = 0;
    init_world();
    chirky_gate_begin(&input_gate);
    show_message(bramble.director.short_focus);
    log_event("begin", "new_walk_through_bramble_hollow");
}

static const char *npc_dialog(int index)
{
    const char *lines[] = {
        bramble.director.zebra_line, bramble.director.turtle_line,
        bramble.director.cat_line, bramble.director.sheep_line,
        bramble.director.nun_line, bramble.director.nun_line
    };
    return lines[clampi(index, 0, BRAMBLE_NPCS - 1)];
}

static void open_dialog(const char *name, const char *text, enum bramble_service service)
{
    copy_clean(bramble.dialog_name, sizeof(bramble.dialog_name), name);
    copy_clean(bramble.dialog_text, sizeof(bramble.dialog_text), text);
    bramble.dialog_service = service;
    bramble.phase = BRAMBLE_DIALOG;
    chirky_gate_begin(&input_gate);
}

static bool blocked(float x, float y)
{
    if (x < 10 || y < 16 || x > BRAMBLE_WORLD_W - 10 || y > BRAMBLE_WORLD_H - 10) return true;
    if (x > 492 && x < 548 && !(y > 295 && y < 342)) return true;
    struct { int x, y, w, h; } buildings[] = {
        {74, 72, 92, 64}, {342, 142, 92, 58}, {374, 245, 92, 58},
        {302, 326, 82, 55}, {610, 82, 110, 94}
    };
    for (size_t i = 0; i < sizeof(buildings) / sizeof(buildings[0]); i++) {
        int margin = 8;
        if (x > buildings[i].x - margin && x < buildings[i].x + buildings[i].w + margin &&
            y > buildings[i].y - margin && y < buildings[i].y + buildings[i].h + margin) return true;
    }
    return false;
}

static void update_camera(void)
{
    int view_h = bramble_host->screen_height - 24;
    bramble.camera_x = clampi((int)bramble.x - bramble_host->screen_width / 2,
                             0, BRAMBLE_WORLD_W - bramble_host->screen_width);
    bramble.camera_y = clampi((int)bramble.y - view_h / 2,
                             0, BRAMBLE_WORLD_H - view_h);
}

static void advance_plants(int amount)
{
    for (int i = 0; i < BRAMBLE_PLANTS; i++) {
        struct bramble_plant *plant = &bramble.plants[i];
        if (plant->stage <= 0 || plant->stage >= 3) continue;
        plant->growth += amount;
        while (plant->growth >= 900 && plant->stage < 3) {
            plant->growth -= 900;
            plant->stage++;
        }
    }
}

static void advance_day(void)
{
    bramble.day++;
    bramble.minute = 8 * 60;
    advance_plants(900 * bramble.growth_boost);
    show_message("A new morning settles over Bramble Hollow.");
    log_event("day", "slept_until_morning");
}

static void use_service(void)
{
    if (bramble.dialog_service == BRAMBLE_SHOP) {
        if (bramble.wood < 2) { show_message("Zara needs two pieces of wood for the seed crates."); return; }
        bramble.wood -= 2; bramble.seeds += 3;
        show_message("Traded two wood for three seed packets.");
        log_event("shop", "traded_wood_for_seeds");
    } else if (bramble.dialog_service == BRAMBLE_BAKERY) {
        if (bramble.coins < 2) { show_message("A berry bun costs two little coins."); return; }
        bramble.coins -= 2; bramble.buns++;
        show_message("Maple wrapped a warm berry bun for the road.");
        log_event("shop", "bought_berry_bun");
    } else if (bramble.dialog_service == BRAMBLE_REST) {
        advance_day();
        bramble.phase = BRAMBLE_PLAY;
        chirky_gate_begin(&input_gate);
    }
}

static bool interact_plant(void)
{
    for (int i = 0; i < BRAMBLE_PLANTS; i++) {
        struct bramble_plant *plant = &bramble.plants[i];
        if (distance2(bramble.x, bramble.y, plant->x, plant->y) > 12 * 12) continue;
        if (!plant->stage) {
            if (!bramble.seeds) show_message("The seed pouch is empty. Zara can help.");
            else {
                bramble.seeds--; plant->stage = 1; plant->growth = 0;
                show_message("A seed is tucked into the soft earth.");
                log_event("garden", "planted_seed");
            }
        } else if (plant->stage == 3) {
            plant->stage = 0; plant->growth = 0; bramble.coins += 2;
            show_message("Harvested flowers. The village box left two coins.");
            log_event("garden", "harvested_flowers");
        } else show_message("A young plant is still growing.");
        return true;
    }
    return false;
}

static bool interact_tree(void)
{
    for (int i = 0; i < BRAMBLE_TREES; i++) {
        struct bramble_tree *tree = &bramble.trees[i];
        if (distance2(bramble.x, bramble.y, tree->x, tree->y) > 30 * 30) continue;
        if (tree->regrow) { show_message("A small green shoot is already returning."); return true; }
        if (bramble.cycling) { show_message("Hop off the bicycle before using the little axe."); return true; }
        tree->hits++; bramble.action_cooldown = 18;
        if (tree->hits >= 3) {
            tree->hits = 0; tree->regrow = 60 * 75; bramble.wood += 2;
            show_message("Two pieces of fallen wood, and the stump will regrow.");
            log_event("wood", "carefully_chopped_fallen_tree");
        } else show_message(tree->hits == 1 ? "Tap... two more careful chops." : "Thunk... one more chop.");
        return true;
    }
    return false;
}

static void interact(void)
{
    if (interact_plant() || interact_tree()) return;
    if (distance2(bramble.x, bramble.y, 132, 195) < 28 * 28) {
        if (!bramble.wood) show_message("The fire is ready, but the wood basket is empty.");
        else {
            bramble.wood--; bramble.fire_ticks += 60 * 45;
            show_message("The cottage fire crackles warmly.");
            log_event("home", "added_wood_to_fire");
        }
        return;
    }
    if (distance2(bramble.x, bramble.y, 122, 146) < 34 * 34) {
        open_dialog("Home", "Rest until morning? The garden and young trees will grow.", BRAMBLE_REST);
        return;
    }
    if (distance2(bramble.x, bramble.y, 665, 184) < 55 * 55) {
        open_dialog("Old Church", bramble.director.long_church_goal, BRAMBLE_NO_SERVICE);
        log_event("place", "visited_stained_glass_church");
        return;
    }
    for (int i = 0; i < BRAMBLE_NPCS; i++) {
        struct bramble_npc *npc = &bramble.npcs[i];
        if (distance2(bramble.x, bramble.y, npc->x, npc->y) > 34 * 34) continue;
        open_dialog(npc->name, npc_dialog(i), npc->service);
        log_event("talk", npc->name);
        return;
    }
    show_message("Only leaves and birds answer here.");
}

const char *bramble_context_prompt(void)
{
    static char prompt[64];
    if (distance2(bramble.x, bramble.y, 132, 195) < 28 * 28) return "B  TEND FIRE";
    if (distance2(bramble.x, bramble.y, 122, 146) < 34 * 34) return "B  REST AT HOME";
    if (distance2(bramble.x, bramble.y, 665, 184) < 55 * 55) return "B  VISIT CHURCH";
    for (int i = 0; i < BRAMBLE_PLANTS; i++) if (distance2(bramble.x, bramble.y,
        bramble.plants[i].x, bramble.plants[i].y) < 12 * 12)
        return bramble.plants[i].stage == 3 ? "B  HARVEST" : bramble.plants[i].stage ? "B  CHECK PLANT" : "B  PLANT SEED";
    for (int i = 0; i < BRAMBLE_TREES; i++) if (distance2(bramble.x, bramble.y,
        bramble.trees[i].x, bramble.trees[i].y) < 30 * 30) return "B  CHOP WOOD";
    for (int i = 0; i < BRAMBLE_NPCS; i++) if (distance2(bramble.x, bramble.y,
        bramble.npcs[i].x, bramble.npcs[i].y) < 34 * 34) {
        snprintf(prompt, sizeof(prompt), "B  TALK TO %s", bramble.npcs[i].name);
        return prompt;
    }
    return bramble.cycling ? "Y  HOP OFF    X  WORLD" : "Y  BICYCLE    X  WORLD";
}

static void update_world(void)
{
    bramble.tick++;
    if (bramble.message_ticks) bramble.message_ticks--;
    if (bramble.action_cooldown) bramble.action_cooldown--;
    if (bramble.fire_ticks) bramble.fire_ticks--;
    bramble.weather_age++;
    if (!(bramble.tick % 30)) {
        if (++bramble.minute >= 24 * 60) { bramble.minute = 0; bramble.day++; }
        int rate = bramble.growth_boost * (bramble.weather == BRAMBLE_RAIN ? 2 : 1);
        advance_plants(rate);
    }
    for (int i = 0; i < BRAMBLE_TREES; i++) if (bramble.trees[i].regrow) bramble.trees[i].regrow--;
    if (director_refresh_ticks > 0 && !(bramble.tick % (unsigned)director_refresh_ticks)) {
        if (load_director()) show_message("The village story has shifted a little.");
    }
}

static void update_play(const struct chirky_input *input)
{
    update_world();
    if (input->button_pressed[CHIRKY_BUTTON_X]) {
        bramble.phase = BRAMBLE_DIRECTOR; bramble.director_cursor = 0;
        chirky_gate_begin(&input_gate); return;
    }
    if (input->button_pressed[CHIRKY_BUTTON_Y]) {
        bramble.cycling = !bramble.cycling;
        show_message(bramble.cycling ? "Bell bright, basket ready. Off we go!" : "Bicycle parked beside the path.");
        log_event("travel", bramble.cycling ? "mounted_bicycle" : "dismounted_bicycle");
    }
    int dx = input->buttons[CHIRKY_BUTTON_RIGHT] - input->buttons[CHIRKY_BUTTON_LEFT];
    int dy = input->buttons[CHIRKY_BUTTON_DOWN] - input->buttons[CHIRKY_BUTTON_UP];
    bramble.moving = dx || dy;
    if (dx) bramble.facing = dx < 0 ? BRAMBLE_LEFT : BRAMBLE_RIGHT;
    else if (dy) bramble.facing = dy < 0 ? BRAMBLE_UP : BRAMBLE_DOWN;
    float speed = bramble.cycling ? 2.65f : 1.35f;
    if (dx && dy) speed *= .72f;
    float nx = bramble.x + dx * speed, ny = bramble.y + dy * speed;
    if (!blocked(nx, bramble.y)) bramble.x = nx;
    if (!blocked(bramble.x, ny)) bramble.y = ny;
    if (input->button_pressed[CHIRKY_BUTTON_B] && !bramble.action_cooldown) interact();
    update_camera();
}

static void update_dialog(const struct chirky_input *input)
{
    update_world();
    if (input->button_pressed[CHIRKY_BUTTON_Y] && bramble.dialog_service != BRAMBLE_NO_SERVICE) use_service();
    if (input->button_pressed[CHIRKY_BUTTON_A] || input->button_pressed[CHIRKY_BUTTON_B]) {
        bramble.phase = BRAMBLE_PLAY; chirky_gate_begin(&input_gate);
    }
}

static void update_director(const struct chirky_input *input)
{
    update_world();
    if (input->button_pressed[CHIRKY_BUTTON_X] || input->button_pressed[CHIRKY_BUTTON_A]) {
        bramble.phase = BRAMBLE_PLAY; chirky_gate_begin(&input_gate); return;
    }
    if (input->button_pressed[CHIRKY_BUTTON_UP]) bramble.director_cursor = (bramble.director_cursor + 2) % 3;
    if (input->button_pressed[CHIRKY_BUTTON_DOWN]) bramble.director_cursor = (bramble.director_cursor + 1) % 3;
    int change = input->button_pressed[CHIRKY_BUTTON_RIGHT] - input->button_pressed[CHIRKY_BUTTON_LEFT];
    if (!change) return;
    if (bramble.director_cursor == 0) {
        bramble.weather = (enum bramble_weather)((bramble.weather + change + BRAMBLE_WEATHER_COUNT) % BRAMBLE_WEATHER_COUNT);
        bramble.weather_age = 0; show_message("Weather changed.");
        log_event("world_control", weather_name(bramble.weather));
    } else if (bramble.director_cursor == 1) {
        bramble.minute = (bramble.minute + change * 180 + 24 * 60) % (24 * 60);
        show_message("The village clock turns.");
    } else {
        bramble.growth_boost = clampi(bramble.growth_boost + change, 1, 3);
        show_message("Garden growth changed.");
    }
}

static void game_update(const struct chirky_input *input)
{
    struct chirky_input filtered;
    chirky_gate_filter(&input_gate, input, &filtered);
    if (bramble.phase == BRAMBLE_TITLE) {
        bramble.tick++;
        if (chirky_title_pressed(&filtered)) begin_play();
    } else if (bramble.phase == BRAMBLE_PLAY) update_play(&filtered);
    else if (bramble.phase == BRAMBLE_DIALOG) update_dialog(&filtered);
    else update_director(&filtered);
}

static bool read_config(const char *config)
{
    struct chirky_file source = chirky_file_open(bramble_host, config);
    if (!source.stream) return false;
    char line[256];
    while (fgets(line, sizeof(line), source.stream)) {
        char *key = trim(line), *separator = strchr(key, '=');
        if (!separator || !*key || *key == '#' || *key == ';') continue;
        *separator = 0;
        char *value = trim(separator + 1); key = trim(key);
        if (!strcmp(key, "start_hour")) bramble.minute = clampi(atoi(value), 0, 23) * 60;
        else if (!strcmp(key, "start_weather")) {
            int weather = weather_value(value); if (weather >= 0) bramble.weather = (enum bramble_weather)weather;
        } else if (!strcmp(key, "director_refresh_seconds"))
            director_refresh_ticks = clampi(atoi(value), 1, 60) * 60;
    }
    bool valid = !ferror(source.stream);
    chirky_file_close(&source);
    return valid;
}

static void release_assets(void)
{
    if (!bramble_host || !bramble_host->asset_release) return;
    if (bramble.player_art) bramble_host->asset_release(bramble_host->context, bramble.player_art);
    if (bramble.friend_art) bramble_host->asset_release(bramble_host->context, bramble.friend_art);
    bramble.player_art = bramble.friend_art = 0;
}

static bool game_init(const struct chirky_host_api *api, const char *config)
{
    if (!api || api->abi_version != CHIRKY_ABI_VERSION || !api->fill_rect || !api->draw_text ||
        !api->asset_request || !api->asset_status || !api->asset_data || !api->asset_release ||
        !api->draw_sprite || !config) return false;
    memset(&bramble, 0, sizeof(bramble));
    bramble_host = api; input_gate = (struct chirky_input_gate){0};
    director_defaults(&bramble.director);
    bramble.day = 1; bramble.minute = 8 * 60; bramble.weather = BRAMBLE_SUN; bramble.growth_boost = 1;
    snprintf(bramble.config_dir, sizeof(bramble.config_dir), "%s", config);
    char *slash = strrchr(bramble.config_dir, '/');
    if (slash) *slash = 0; else snprintf(bramble.config_dir, sizeof(bramble.config_dir), ".");
    snprintf(bramble.director_path, sizeof(bramble.director_path), "%s/assets/director.conf", bramble.config_dir);
    snprintf(bramble.event_path, sizeof(bramble.event_path), "%s/runtime/events.log", bramble.config_dir);
    if (!read_config(config)) { bramble_host = NULL; return false; }
    load_director();
    snprintf(bramble.director_path, sizeof(bramble.director_path), "%s/runtime/director.conf", bramble.config_dir);
    load_director();
    char path[640];
    snprintf(path, sizeof(path), "%s/assets/player.pam", bramble.config_dir);
    bramble.player_art = api->asset_request(api->context, path, CHIRKY_ASSET_IMAGE);
    snprintf(path, sizeof(path), "%s/assets/friends.pam", bramble.config_dir);
    bramble.friend_art = api->asset_request(api->context, path, CHIRKY_ASSET_IMAGE);
    if (!bramble.player_art || !bramble.friend_art ||
        api->asset_status(api->context, bramble.player_art) != CHIRKY_ASSET_READY ||
        api->asset_status(api->context, bramble.friend_art) != CHIRKY_ASSET_READY) {
        release_assets(); bramble_host = NULL; return false;
    }
    struct chirky_asset_view player = api->asset_data(api->context, bramble.player_art);
    struct chirky_asset_view friends = api->asset_data(api->context, bramble.friend_art);
    if (player.width != 160 || player.height != 160 || friends.width != 200 || friends.height != 90) {
        release_assets(); bramble_host = NULL; return false;
    }
    bramble_title_load(config);
    bramble.phase = BRAMBLE_TITLE;
    return true;
}

static void game_shutdown(void)
{
    bramble_title_free();
    release_assets();
    memset(&bramble, 0, sizeof(bramble));
    bramble_host = NULL;
}

static const struct chirky_game_api game_api = {
    .abi_version = CHIRKY_ABI_VERSION,
    .init = game_init,
    .shutdown = game_shutdown,
    .update = game_update,
    .render = bramble_render
};

const struct chirky_game_api *chirky_game_entry(void) { return &game_api; }
