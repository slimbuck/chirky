#define _POSIX_C_SOURCE 200809L
#include "../src/asset_store.h"
#include "../games/bramble-hollow/game_state.h"
#include <assert.h>
#include "text_encoding.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const struct chirky_game_api *chirky_game_entry(void);
static struct asset_store *store;
static int rectangles, sprites, texts;
static char emitted_event[256];
static const char *network_state;
static uint32_t network_revision;

static chirky_asset request(void *context, const char *path, enum chirky_asset_type type)
{ return asset_store_request(context, path, type); }
static enum chirky_asset_state status(void *context, chirky_asset asset)
{ return asset_store_state(context, asset); }
static struct chirky_asset_view data(void *context, chirky_asset asset)
{ return asset_store_view(context, asset); }
static void release(void *context, chirky_asset asset)
{ asset_store_release(context, asset); }
static void rectangle(void *context, int x, int y, int width, int height,
                      unsigned char r, unsigned char g, unsigned char b)
{
    (void)context; (void)x; (void)y; (void)r; (void)g; (void)b;
    assert(width >= 0 && height >= 0); rectangles++;
}
static void lettering(void *context, int x, int y, const char *value, int scale,
                      unsigned char r, unsigned char g, unsigned char b)
{
    (void)context; (void)r; (void)g; (void)b;
    assert(value && scale > 0 && x >= 0 && x + (int)chirky_text_length(value) * 6 * scale - scale <= 288);
    assert(y - 6 * scale >= 0 && y + scale <= 216); texts++;
}
static void sprite(void *context, chirky_asset asset, int x, int y, int width, int height,
                   int sx, int sy, int sw, int sh, unsigned char r, unsigned char g,
                   unsigned char b, unsigned char a, bool flip)
{
    (void)x; (void)y; (void)r; (void)g; (void)b; (void)a; (void)flip;
    struct chirky_asset_view view = asset_store_view(context, asset);
    assert(view.data && width > 0 && height > 0 && sx >= 0 && sy >= 0 && sw > 0 && sh > 0);
    assert((unsigned)(sx + sw) <= view.width && (unsigned)(sy + sh) <= view.height); sprites++;
    if (asset == bramble.player_art) assert(width == sw && height == sh);
    if (asset == bramble.friend_art) assert(width == sw && height == sh);
}
static void scope(void *context, const char *name, bool begin)
{ (void)context; (void)name; (void)begin; }
static bool connect_director(void *context, const char *url, const char *game, const char *world)
{
    (void)context;
    assert(!strcmp(url, "http://192.168.137.1:3040"));
    assert(!strcmp(game, "bramble-hollow") && !strcmp(world, "bramble-hollow-main"));
    return true;
}
static bool emit_director(void *context, const char *json, size_t size)
{
    (void)context;
    assert(size + 1 <= sizeof(emitted_event));
    memcpy(emitted_event, json, size); emitted_event[size] = 0;
    return true;
}
static size_t state_director(void *context, uint32_t after_revision, char *text,
                             size_t capacity, uint32_t *revision)
{
    (void)context;
    if (!network_state || network_revision <= after_revision) return 0;
    size_t size = strlen(network_state);
    assert(size + 1 <= capacity);
    memcpy(text, network_state, size + 1); *revision = network_revision;
    return size;
}

static void validate_atlas(chirky_asset asset, unsigned columns, unsigned rows)
{
    struct chirky_asset_view view = asset_store_view(store, asset);
    assert(view.data && view.width % columns == 0 && view.height % rows == 0);
    unsigned cell_width = view.width / columns, cell_height = view.height / rows;
    const unsigned char *pixels = view.data;
    for (unsigned row = 0; row < rows; row++) for (unsigned column = 0; column < columns; column++) {
        unsigned opaque = 0;
        for (unsigned y = 0; y < cell_height; y++) for (unsigned x = 0; x < cell_width; x++) {
            unsigned px = column * cell_width + x, py = row * cell_height + y;
            unsigned char alpha = pixels[(py * view.width + px) * 4 + 3];
            assert(alpha == 0 || alpha == 255);
            if (x == 0 || y == 0 || x + 1 == cell_width || y + 1 == cell_height) assert(alpha == 0);
            opaque += alpha == 255;
        }
        assert(opaque > 100);
    }
}

static void validate_player_alignment(chirky_asset asset)
{
    struct chirky_asset_view view = asset_store_view(store, asset);
    const unsigned char *pixels = view.data;
    for (unsigned frame = 0; frame < 16; frame++) {
        unsigned left = 40, right = 0, top = 40, bottom = 0;
        unsigned cell_x = frame % 4 * 40, cell_y = frame / 4 * 40;
        for (unsigned y = 0; y < 40; y++) for (unsigned x = 0; x < 40; x++) {
            if (!pixels[((cell_y + y) * view.width + cell_x + x) * 4 + 3]) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
        assert(bottom == 38);
        assert(bottom - top + 1 == 34);
        assert(left + right >= 38 && left + right <= 40);
    }
}

static void validate_bicycle_wheels(chirky_asset asset)
{
    struct chirky_asset_view view = asset_store_view(store, asset);
    const unsigned char *pixels = view.data;
    unsigned cell_x = 2 * 50, cell_y = 45;
    unsigned count[2] = {0}, top[2] = {45, 45}, bottom[2] = {0, 0};
    for (unsigned wheel = 0; wheel < 2; wheel++) {
        unsigned start = wheel ? 26 : 1, end = wheel ? 49 : 25;
        for (unsigned y = 22; y < 44; y++) for (unsigned x = start; x < end; x++) {
            size_t offset = ((cell_y + y) * view.width + cell_x + x) * 4;
            if (!pixels[offset + 3]) continue;
            count[wheel]++;
            if (y < top[wheel]) top[wheel] = y;
            if (y > bottom[wheel]) bottom[wheel] = y;
        }
    }
    assert(count[0] > 90 && count[1] > 90);
    assert(top[0] == top[1] && bottom[0] == bottom[1]);
    assert(count[0] + 40 > count[1] && count[1] + 40 > count[0]);
}

static void validate_cat_whiskers(chirky_asset asset)
{
    struct chirky_asset_view view = asset_store_view(store, asset);
    const unsigned char *pixels = view.data;
    static const unsigned points[][2] = {
        {11, 21}, {10, 23}, {11, 25}, {38, 21}, {39, 23}, {38, 25}
    };
    for (size_t i = 0; i < sizeof(points) / sizeof(*points); i++) {
        size_t offset = (points[i][1] * view.width + 100 + points[i][0]) * 4;
        assert(pixels[offset] < 130 && pixels[offset + 1] < 100 &&
               pixels[offset + 2] < 80 && pixels[offset + 3] == 255);
    }
}

static void neutral(const struct chirky_game_api *game, struct chirky_input *input)
{
    memset(input, 0, sizeof(*input)); game->update(input); game->update(input);
}

static void press(const struct chirky_game_api *game, struct chirky_input *input, enum chirky_button button)
{
    memset(input, 0, sizeof(*input)); input->buttons[button] = input->button_pressed[button] = true;
    game->update(input); neutral(game, input);
}

int main(void)
{
    store = asset_store_create(); assert(store);
    assert(asset_store_prefetch(store, "games/bramble-hollow"));
    for (int tries = 0; asset_store_prefetch_state(store) == CHIRKY_ASSET_LOADING; tries++) {
        assert(tries < 15000); struct timespec delay = {.tv_nsec = 1000000}; nanosleep(&delay, NULL);
    }
    assert(asset_store_prefetch_state(store) == CHIRKY_ASSET_READY);
    struct chirky_host_api host = {.abi_version = CHIRKY_ABI_VERSION, .screen_width = 288,
        .screen_height = 216, .context = store, .fill_rect = rectangle, .draw_text = lettering,
        .profile_scope = scope, .asset_request = request, .asset_status = status,
        .asset_data = data, .asset_release = release, .draw_sprite = sprite,
        .director_connect = connect_director, .director_event = emit_director,
        .director_state = state_director};
    const struct chirky_game_api *game = chirky_game_entry();
    assert(game && game->abi_version == CHIRKY_ABI_VERSION);
    assert(game->init(&host, "games/bramble-hollow/game.conf"));
    assert(bramble.phase == BRAMBLE_TITLE && bramble.player_art && bramble.friend_art);
    assert(asset_store_view(store, bramble.player_art).width == 160);
    assert(asset_store_view(store, bramble.player_art).height == 160);
    assert(asset_store_view(store, bramble.friend_art).width == 200);
    assert(asset_store_view(store, bramble.friend_art).height == 90);
    validate_atlas(bramble.player_art, 4, 4);
    validate_atlas(bramble.friend_art, 4, 2);
    validate_player_alignment(bramble.player_art);
    validate_cat_whiskers(bramble.friend_art);
    validate_bicycle_wheels(bramble.friend_art);
    game->render(); assert(sprites == 1 && rectangles >= 1 && texts >= 2);
    snprintf(bramble.event_path, sizeof(bramble.event_path), "/tmp/bramble-runtime-events.log");
    unlink(bramble.event_path);
    struct chirky_input input = {0};
    press(game, &input, CHIRKY_BUTTON_PRIMARY);
    assert(bramble.phase == BRAMBLE_PLAY && bramble.seeds == 5 && bramble.day == 1);
    assert(strstr(emitted_event, "\"kind\":\"begin\""));
    network_state = "version=1\nrevision=2\nlong_theme=Rain is waking the old paths.\n"
        "long_church_goal=Bring a garden memory to the stained glass.\n"
        "medium_event=Maple is baking beside the bridge.\nmedium_shop_special=Zara has fresh seeds.\n"
        "short_focus=Visit Maple after checking the garden.\nweather=mist\ngrowth_boost=3\n"
        "zebra_line=The mist makes every bell sound near.\nturtle_line=The seedlings are listening.\n"
        "cat_line=Maple's buns, warm and bright.\nsheep_line=I found another window story.\n"
        "nun_line=The blue pane remembers rain.\n";
    network_revision = 2; bramble.tick = 299;
    memset(&input, 0, sizeof(input)); game->update(&input);
    assert(bramble.director.revision == 2 && bramble.weather == BRAMBLE_MIST && bramble.growth_boost == 3);
    assert(!strcmp(bramble.director.cat_line, "Maple's buns, warm and bright."));
    float old_x = bramble.x;
    input.buttons[CHIRKY_BUTTON_RIGHT] = true;
    for (int i = 0; i < 10; i++) game->update(&input);
    assert(bramble.x > old_x);
    neutral(game, &input); press(game, &input, CHIRKY_BUTTON_SECONDARY);
    assert(bramble.cycling);
    press(game, &input, CHIRKY_BUTTON_START); assert(bramble.phase == BRAMBLE_DIRECTOR);
    enum bramble_weather old_weather = bramble.weather;
    press(game, &input, CHIRKY_BUTTON_RIGHT);
    assert(bramble.weather == (old_weather + 1) % BRAMBLE_WEATHER_COUNT);
    press(game, &input, CHIRKY_BUTTON_START); assert(bramble.phase == BRAMBLE_PLAY);
    bramble.cycling = false;
    bramble.x = bramble.trees[0].x; bramble.y = bramble.trees[0].y;
    for (int i = 0; i < 3; i++) { bramble.action_cooldown = 0; press(game, &input, CHIRKY_BUTTON_PRIMARY); }
    assert(bramble.wood == 2 && bramble.trees[0].regrow > 0);
    bramble.x = bramble.plants[2].x; bramble.y = bramble.plants[2].y;
    bramble.action_cooldown = 0;
    press(game, &input, CHIRKY_BUTTON_PRIMARY);
    assert(bramble.seeds == 4 && bramble.plants[2].stage == 1);
    bramble.plants[2].stage = 3;
    press(game, &input, CHIRKY_BUTTON_PRIMARY);
    assert(bramble.coins == 2 && bramble.plants[2].stage == 0);
    bramble.x = bramble.npcs[2].x; bramble.y = bramble.npcs[2].y;
    press(game, &input, CHIRKY_BUTTON_PRIMARY); assert(bramble.phase == BRAMBLE_DIALOG);
    press(game, &input, CHIRKY_BUTTON_SECONDARY); assert(bramble.buns == 1 && bramble.coins == 0);
    press(game, &input, CHIRKY_BUTTON_PRIMARY); assert(bramble.phase == BRAMBLE_PLAY);
    bramble.x = 122; bramble.y = 146;
    unsigned old_day = bramble.day;
    press(game, &input, CHIRKY_BUTTON_PRIMARY);
    assert(bramble.phase == BRAMBLE_DIALOG && bramble.dialog_service == BRAMBLE_REST);
    press(game, &input, CHIRKY_BUTTON_SECONDARY);
    assert(bramble.phase == BRAMBLE_PLAY && bramble.day == old_day + 1 && bramble.minute == 8 * 60);
    assert(strstr(bramble.message, "new morning"));
    game->render(); assert(rectangles > 30 && sprites > 3 && texts > 3);
    game->shutdown(); game->shutdown();
    asset_store_destroy(store); unlink("/tmp/bramble-runtime-events.log");
    puts("Bramble Hollow: isolated sprite cells, title, movement, bicycle, world controls, rest, wood, plants and shop passed.");
    return 0;
}
