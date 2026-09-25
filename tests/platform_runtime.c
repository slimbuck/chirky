#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "../src/asset_store.h"
#include "../games/rosey-chop/game_state.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Run from the repository root, against the actual shared game modules:
   make build/games/phosphor-run.so build/games/rosey-chop.so
   cc -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
      tests/platform_runtime.c src/asset_store.c -pthread -ldl -o /tmp/platform-runtime-test
   /tmp/platform-runtime-test [directory-containing-game-modules]
   Metrics cover asset-store I/O/decoding, not arbitrary libc I/O in a module. */

struct observed_asset {
    chirky_asset handle;
    enum chirky_asset_type type;
    struct chirky_asset_view view;
    unsigned refs, plays;
    bool pinned;
    uint64_t checksum;
    char path[512];
};

struct platform {
    struct asset_store *store;
    pthread_t main_thread;
    struct observed_asset assets[ASSET_STORE_SLOTS];
    size_t count;
    unsigned requests[3], rectangles, sprites, texts, sounds, scope_depth;
    bool steady;
    char scene[32];
    struct asset_store_metrics prefetched;
};

static void on_main(struct platform *p)
{ assert(pthread_equal(p->main_thread, pthread_self())); }

static uint64_t checksum(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static struct observed_asset *observed(struct platform *p, chirky_asset handle)
{
    for (size_t i = 0; i < p->count; i++) if (p->assets[i].handle == handle) return &p->assets[i];
    assert(!"Game used an asset it never requested");
    return NULL;
}

static struct chirky_asset_view ready_view(struct platform *p, chirky_asset handle)
{
    assert(handle && asset_store_state(p->store, handle) == CHIRKY_ASSET_READY);
    struct chirky_asset_view view = asset_store_view(p->store, handle);
    assert(view.data && view.size);
    return view;
}

static chirky_asset asset_request(void *context, const char *path, enum chirky_asset_type type)
{
    struct platform *p = context;
    on_main(p);
    assert(!p->steady && path && strlen(path) < sizeof(p->assets[0].path));
    assert(type == CHIRKY_ASSET_BLOB || type == CHIRKY_ASSET_IMAGE || type == CHIRKY_ASSET_SOUND);
    chirky_asset handle = asset_store_request(p->store, path, type);
    if (!handle || asset_store_state(p->store, handle) != CHIRKY_ASSET_READY)
        fprintf(stderr, "Game requested an unprefetched/unready asset: %s (type %d)\n", path, (int)type);
    struct chirky_asset_view view = ready_view(p, handle);
    struct observed_asset *asset = NULL;
    for (size_t i = 0; i < p->count; i++) if (p->assets[i].handle == handle) asset = &p->assets[i];
    if (!asset) {
        assert(p->count < ASSET_STORE_SLOTS);
        asset = &p->assets[p->count++];
        *asset = (struct observed_asset){.handle = handle, .type = type, .view = view,
            .checksum = checksum(view.data, view.size)};
        snprintf(asset->path, sizeof(asset->path), "%s", path);
        if (type == CHIRKY_ASSET_BLOB) {
            assert(((const unsigned char *)view.data)[view.size] == 0);
            assert(!view.width && !view.height && !view.rate && !view.channels);
        } else if (type == CHIRKY_ASSET_IMAGE) {
            assert(view.width && view.height && view.size == (size_t)view.width * view.height * 4);
            assert(!view.rate && !view.channels);
        } else {
            assert(view.rate && view.channels && view.size % (view.channels * sizeof(int16_t)) == 0);
            assert(!view.width && !view.height);
            const int16_t *samples = view.data;
            bool nonzero = false;
            for (size_t i = 0; i < view.size / sizeof(*samples); i++) nonzero |= samples[i] != 0;
            assert(nonzero);
        }
    }
    assert(asset->type == type && !strcmp(asset->path, path) && asset->view.data == view.data);
    asset->refs++;
    p->requests[type]++;
    return handle;
}

static enum chirky_asset_state asset_status(void *context, chirky_asset handle)
{
    struct platform *p = context;
    on_main(p);
    assert(observed(p, handle)->refs);
    enum chirky_asset_state state = asset_store_state(p->store, handle);
    assert(state == CHIRKY_ASSET_READY);
    return state;
}

static struct chirky_asset_view asset_data(void *context, chirky_asset handle)
{
    struct platform *p = context;
    on_main(p);
    struct observed_asset *asset = observed(p, handle);
    assert(asset->refs);
    struct chirky_asset_view view = ready_view(p, handle);
    assert(view.data == asset->view.data && view.size == asset->view.size);
    return view;
}

static void asset_release(void *context, chirky_asset handle)
{
    struct platform *p = context;
    on_main(p);
    if (!handle) return;
    struct observed_asset *asset = observed(p, handle);
    assert(asset->refs);
    asset->refs--;
    asset_store_release(p->store, handle);
    assert(ready_view(p, handle).data == asset->view.data); /* Prefetch still owns it. */
}

static void rectangle(void *context, int x, int y, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b)
{
    struct platform *p = context;
    on_main(p);
    (void)x; (void)y; (void)r; (void)g; (void)b;
    assert(w >= 0 && h >= 0);
    p->rectangles++;
}

static void text(void *context, int x, int y, const char *value, int scale,
                 unsigned char r, unsigned char g, unsigned char b)
{
    struct platform *p = context;
    on_main(p);
    (void)r; (void)g; (void)b;
    assert(value && scale > 0);
    assert(x >= 0 && x + (int)strlen(value) * 6 * scale - scale <= 288);
    assert(y - 6 * scale >= 0 && y + scale <= 216);
    p->texts++;
}

static void label(void *context, enum chirky_button button, char *value, size_t capacity)
{
    on_main(context);
    snprintf(value, capacity, "%s", button == CHIRKY_BUTTON_Y ? "Y" : "B");
}

static void sprite(void *context, chirky_asset handle, int x, int y, int w, int h,
                   int sx, int sy, int sw, int sh, unsigned char r, unsigned char g,
                   unsigned char b, unsigned char a, bool flip)
{
    struct platform *p = context;
    on_main(p);
    (void)x; (void)y; (void)r; (void)g; (void)b; (void)a; (void)flip;
    assert(observed(p, handle)->type == CHIRKY_ASSET_IMAGE);
    struct chirky_asset_view view = asset_data(context, handle);
    assert(w > 0 && h > 0 && sx >= 0 && sy >= 0 && sw > 0 && sh > 0);
    assert((uint64_t)sx + sw <= view.width && (uint64_t)sy + sh <= view.height);
    assert(view.size == (size_t)view.width * view.height * 4);
    p->sprites++;
}

static void sound(void *context, chirky_asset handle)
{
    struct platform *p = context;
    on_main(p);
    struct observed_asset *asset = observed(p, handle);
    assert(asset->type == CHIRKY_ASSET_SOUND);
    struct chirky_asset_view view = asset_data(context, handle);
    assert(view.rate && view.channels && view.size % (view.channels * sizeof(int16_t)) == 0);
    if (!asset->pinned) { assert(asset_store_retain(p->store, handle)); asset->pinned = true; }
    asset->plays++;
    p->sounds++;
}

static void legacy_sound(void *context, const char *device, const char *path)
{
    (void)context; (void)device; (void)path;
    assert(!"Game fell back to path-based sound with ABI9 callbacks present");
}

static void scope(void *context, const char *name, bool begin)
{
    struct platform *p = context;
    on_main(p);
    if (begin) {
        p->scope_depth++;
        if (!strncmp(name, "scene.", 6)) snprintf(p->scene, sizeof(p->scene), "%s", name);
    } else { assert(p->scope_depth); p->scope_depth--; }
}

static double now_ms(void)
{
    struct timespec ts;
    assert(!clock_gettime(CLOCK_MONOTONIC, &ts));
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void prefetch(struct platform *p, const char *directory)
{
    double deadline = now_ms() + 15000;
    assert(asset_store_prefetch(p->store, directory));
    while (asset_store_prefetch_state(p->store) == CHIRKY_ASSET_LOADING) {
        if (now_ms() >= deadline) fprintf(stderr, "Prefetch timed out: %s\n", directory);
        assert(now_ms() < deadline);
        struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }
    assert(asset_store_prefetch_state(p->store) == CHIRKY_ASSET_READY);
    p->prefetched = asset_store_get_metrics(p->store);
    assert(p->prefetched.bytes_read && p->prefetched.bytes_resident);
}

static void no_asset_work(struct platform *p)
{
    struct asset_store_metrics m = asset_store_get_metrics(p->store);
    assert(asset_store_prefetch_state(p->store) == CHIRKY_ASSET_READY);
    assert(m.bytes_read == p->prefetched.bytes_read);
    assert(m.worker_cpu_ms == p->prefetched.worker_cpu_ms);
    assert(m.bytes_resident == p->prefetched.bytes_resident);
    assert(!p->scope_depth);
}

static void *symbol(void *module, const char *name)
{
    dlerror();
    void *value = dlsym(module, name);
    const char *error = dlerror();
    if (error) fprintf(stderr, "%s: %s\n", name, error);
    assert(!error && value);
    return value;
}

static void tick(const struct chirky_game_api *game, struct platform *p,
                 const struct chirky_input *input)
{
    game->update(input);
    game->render();
    no_asset_work(p);
}

static void expect_played(struct platform *p, const char *suffix)
{
    for (size_t i = 0; i < p->count; i++) {
        struct observed_asset *asset = &p->assets[i];
        size_t n = strlen(asset->path), length = strlen(suffix);
        if (asset->type == CHIRKY_ASSET_SOUND && n >= length && !strcmp(asset->path + n - length, suffix)) {
            assert(asset->plays && asset->pinned);
            return;
        }
    }
    assert(!"Expected sound was not requested");
}

static void play_phosphor(void *module, const struct chirky_game_api *game, struct platform *p)
{
    const float *x = symbol(module, "player_x"), *y = symbol(module, "player_y");
    const bool *grounded = symbol(module, "on_ground");
    struct chirky_input input = {0};
    for (unsigned i = 0; i < 120 && !*grounded; i++) tick(game, p, &input);
    assert(*grounded && !strcmp(p->scene, "scene.play"));
    float old_y = *y;
    input.buttons[CHIRKY_BUTTON_B] = input.button_pressed[CHIRKY_BUTTON_B] = true;
    tick(game, p, &input);
    assert(*y > old_y);
    expect_played(p, "/jump.wav");
    input.button_pressed[CHIRKY_BUTTON_B] = false;
    input.buttons[CHIRKY_BUTTON_RIGHT] = true;
    float old_x = *x;
    for (unsigned i = 0; i < 6; i++) tick(game, p, &input);
    assert(*x > old_x);
    input.button_pressed[CHIRKY_BUTTON_Y] = true;
    tick(game, p, &input);
    expect_played(p, "/dash.wav");
    input = (struct chirky_input){0};
    input.button_pressed[CHIRKY_BUTTON_L] = true; /* Real respawn input. */
    tick(game, p, &input);
    input = (struct chirky_input){0};
    for (unsigned i = 0; i < 120; i++) tick(game, p, &input);
    assert(!strcmp(p->scene, "scene.play"));
}

static void play_rosey(void *module, const struct chirky_game_api *game, struct platform *p)
{
    /* Observe exported state to choose inputs; never write module state. */
    const struct garden_state *garden_state = symbol(module, "garden");
    assert(garden_state->phase == PLAY && garden_state->remaining == garden_state->total);
    struct chirky_input input = {0};
    input.button_pressed[CHIRKY_BUTTON_Y] = true;
    tick(game, p, &input);
    assert(garden_state->z > 0);
    expect_played(p, "/jump.wav");
    input = (struct chirky_input){0};
    unsigned limit = (unsigned)garden_state->storm_ticks;
    assert(limit > 0 && limit <= 18000);
    unsigned frames = 0;
    while (garden_state->phase == PLAY && frames++ < limit) {
        const struct rose *nearest = NULL;
        float best = 1e9f;
        for (int i = 0; i < garden_state->rose_count; i++) {
            const struct rose *rose = &garden_state->roses[i];
            float dx = rose->x - garden_state->x, dy = rose->y - garden_state->y;
            float distance = dx * dx + dy * dy;
            if (rose->kind == 'd' && !rose->cut && distance < best) { nearest = rose; best = distance; }
        }
        assert(nearest);
        input.buttons[CHIRKY_BUTTON_LEFT] = nearest->x < garden_state->x - 2;
        input.buttons[CHIRKY_BUTTON_RIGHT] = nearest->x > garden_state->x + 2;
        input.buttons[CHIRKY_BUTTON_UP] = nearest->y < garden_state->y - 2;
        input.buttons[CHIRKY_BUTTON_DOWN] = nearest->y > garden_state->y + 2;
        input.buttons[CHIRKY_BUTTON_B] = true;
        float dx = garden_state->wasp_x - garden_state->x, dy = garden_state->wasp_y - garden_state->y;
        input.button_pressed[CHIRKY_BUTTON_Y] = garden_state->wasp_life && dx * dx + dy * dy < 40 * 40;
        tick(game, p, &input);
    }
    assert(garden_state->phase == WON && !garden_state->remaining);
    for (int i = 0; i < garden_state->rose_count; i++)
        assert(garden_state->roses[i].cut == (garden_state->roses[i].kind == 'd'));
    expect_played(p, "/chop.wav");
    expect_played(p, "/win.wav");
    input = (struct chirky_input){0};
    for (unsigned i = 0; i < 45; i++) tick(game, p, &input);
    input.button_pressed[CHIRKY_BUTTON_B] = true;
    tick(game, p, &input);
    assert(garden_state->phase == PLAY && garden_state->remaining == garden_state->total);
}

static void run_cycle(struct asset_store *store, const char *module_directory,
                      const char *name, unsigned cycle, bool phosphor)
{
    struct platform p = {.store = store, .main_thread = pthread_self()};
    char directory[512], config[512], module_path[1024];
    assert(snprintf(directory, sizeof(directory), "games/%s", name) < (int)sizeof(directory));
    assert(snprintf(config, sizeof(config), "%s/game.conf", directory) < (int)sizeof(config));
    assert(snprintf(module_path, sizeof(module_path), "%s/%s.so", module_directory, name) < (int)sizeof(module_path));
    prefetch(&p, directory);
    void *module = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
    if (!module) fprintf(stderr, "dlopen %s: %s\n", module_path, dlerror());
    assert(module);
    void *entry_symbol = symbol(module, "chirky_game_entry");
    chirky_game_entry_fn entry;
    _Static_assert(sizeof(entry) == sizeof(entry_symbol), "POSIX function pointer size");
    memcpy(&entry, &entry_symbol, sizeof(entry));
    const struct chirky_game_api *game = entry();
    assert(game && game->abi_version == CHIRKY_ABI_VERSION);
    assert(game->init && game->update && game->render && game->shutdown);
    struct chirky_host_api api = {.abi_version = CHIRKY_ABI_VERSION,
        .screen_width = 288, .screen_height = 216, .context = &p,
        .fill_rect = rectangle, .draw_text = text, .button_label = label,
        .play_sound = legacy_sound, .profile_scope = scope,
        .asset_request = asset_request, .asset_status = asset_status,
        .asset_data = asset_data, .asset_release = asset_release,
        .draw_sprite = sprite, .sound_play = sound};
    assert(game->init(&api, config));
    assert(p.requests[CHIRKY_ASSET_BLOB] >= 2 && p.requests[CHIRKY_ASSET_IMAGE] == 1);
    assert(p.requests[CHIRKY_ASSET_SOUND] == (phosphor ? 6u : 7u));
    no_asset_work(&p);
    struct chirky_input input = {0};
    p.steady = true;
    for (unsigned frame = 0; frame < 180; frame++) tick(game, &p, &input);
    assert(p.sprites == 180 && p.texts && !p.sounds);
    if (phosphor) assert(!strcmp(p.scene, "scene.title"));
    else assert(((const struct garden_state *)symbol(module, "garden"))->phase == TITLE);
    /* Match the benchmark's frozen-gameplay transition, including gate release. */
    tick(game, &p, &input); tick(game, &p, &input);
    input.buttons[CHIRKY_BUTTON_B] = input.button_pressed[CHIRKY_BUTTON_B] = true;
    tick(game, &p, &input);
    unsigned title_sprites = p.sprites;
    p.steady = true;
    input = (struct chirky_input){0};
    tick(game, &p, &input); tick(game, &p, &input); /* Release the transition gate. */
    if (phosphor) play_phosphor(module, game, &p);
    else play_rosey(module, game, &p);
    assert(p.rectangles && p.sounds && p.sprites == title_sprites);
    game->shutdown();
    for (size_t i = 0; i < p.count; i++) assert(!p.assets[i].refs);
    assert(!dlclose(module));
    /* Playback holds may outlive the module. Reset them before store clear. */
    for (size_t i = 0; i < p.count; i++) {
        struct observed_asset *asset = &p.assets[i];
        struct chirky_asset_view view = ready_view(&p, asset->handle);
        assert(view.data == asset->view.data && checksum(view.data, view.size) == asset->checksum);
        if (asset->pinned) asset_store_release(store, asset->handle);
    }
    no_asset_work(&p);
    asset_store_clear(store);
    for (size_t i = 0; i < p.count; i++) {
        assert(asset_store_state(store, p.assets[i].handle) == CHIRKY_ASSET_FAILED);
        assert(!asset_store_view(store, p.assets[i].handle).data);
        assert(!asset_store_retain(store, p.assets[i].handle));
    }
    struct asset_store_metrics cleared = asset_store_get_metrics(store);
    assert(!cleared.bytes_read && !cleared.bytes_resident && !cleared.worker_cpu_ms);
    printf("Platform: %s cycle %u: %zu READY assets, %u sound plays; no post-prefetch asset I/O.\n",
        name, cycle + 1, p.count, p.sounds);
}

int main(int argc, char **argv)
{
    assert(argc <= 2);
    const char *modules = argc == 2 ? argv[1] : "build/games";
    struct asset_store *store = asset_store_create();
    assert(store);
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        run_cycle(store, modules, "phosphor-run", cycle, true);
        run_cycle(store, modules, "rosey-chop", cycle, false);
    }
    asset_store_destroy(store);
    puts("Platform: both real modules passed title/input/gameplay, playback pins, unload/clear/reload.");
    return 0;
}
