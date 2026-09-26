#include "game_state.h"
#include "splash_art.h"
#include <stdio.h>
#include <string.h>

static struct splash_art title_art;

void bramble_title_load(const char *config) { splash_load_api(&title_art, bramble_host, config); }
void bramble_title_free(void) { splash_free(&title_art); }

static int clampi(int value, int low, int high)
{ return value < low ? low : value > high ? high : value; }

static unsigned shade(unsigned colour)
{
    int minute = bramble.minute;
    int light = minute < 300 || minute >= 1260 ? 46 :
                minute < 420 ? 70 : minute >= 1140 ? 66 : 100;
    if (bramble.weather == BRAMBLE_RAIN) light = light * 84 / 100;
    else if (bramble.weather == BRAMBLE_MIST) light = light * 92 / 100;
    int r = ((colour >> 16) & 255) * light / 100;
    int g = ((colour >> 8) & 255) * light / 100;
    int b = (colour & 255) * light / 100;
    if (light < 70) { b += 16; g += 5; }
    return (unsigned)(clampi(r, 0, 255) << 16 | clampi(g, 0, 255) << 8 | clampi(b, 0, 255));
}

static void rect(int x, int y, int width, int height, unsigned colour)
{
    if (width <= 0 || height <= 0 || x + width <= 0 || y + height <= 0 ||
        x >= bramble_host->screen_width || y >= bramble_host->screen_height) return;
    bramble_host->fill_rect(bramble_host->context, x, bramble_host->screen_height - y - height,
                            width, height, colour >> 16, colour >> 8, colour);
}

static void world_rect(int x, int y, int width, int height, unsigned colour)
{
    int sx = x - bramble.camera_x, sy = y - bramble.camera_y + 12;
    if (sx + width <= 0 || sy + height <= 12 || sx >= bramble_host->screen_width ||
        sy >= bramble_host->screen_height - 12) return;
    rect(sx, sy, width, height, shade(colour));
}

static void text(int x, int y, const char *value, int scale, unsigned colour)
{
    bramble_host->draw_text(bramble_host->context, x, bramble_host->screen_height - y - scale,
                            value, scale, colour >> 16, colour >> 8, colour);
}

static void center_text(int y, const char *value, int scale, unsigned colour)
{
    int width = ((int)strlen(value) * 6 - 1) * scale;
    text((bramble_host->screen_width - width) / 2, y, value, scale, colour);
}

static void world_sprite(chirky_asset art, int columns, int rows, int index,
                         int x, int y, int width, int height, unsigned char alpha)
{
    int sx = x - bramble.camera_x, sy = y - bramble.camera_y + 12;
    if (sx + width <= 0 || sy + height <= 12 || sx >= bramble_host->screen_width ||
        sy >= bramble_host->screen_height - 12) return;
    struct chirky_asset_view view = bramble_host->asset_data(bramble_host->context, art);
    int source_x = (index % columns) * (int)view.width / columns;
    int source_y = (index / columns) * (int)view.height / rows;
    int source_right = (index % columns + 1) * (int)view.width / columns;
    int source_bottom = (index / columns + 1) * (int)view.height / rows;
    bramble_host->draw_sprite(bramble_host->context, art, sx,
        bramble_host->screen_height - sy - height, width, height,
        source_x, source_y, source_right - source_x, source_bottom - source_y,
        255, 255, 255, alpha, false);
}

static void flower(int x, int y, int colour)
{
    world_rect(x, y + 3, 1, 4, 0x397346);
    world_rect(x - 2, y, 2, 2, colour);
    world_rect(x + 1, y, 2, 2, colour);
    world_rect(x, y - 1, 1, 1, 0xffe6a1);
}

static void tree(int x, int y, int scale, bool stump)
{
    if (stump) {
        world_rect(x - 5, y - 4, 10, 7, 0x714421);
        world_rect(x - 3, y - 4, 6, 2, 0xc28a49);
        world_rect(x - 1, y - 3, 2, 1, 0x714421);
        world_rect(x - 3, y + 2, 2, 4, 0x4b7a3c);
        return;
    }
    world_rect(x - 3 * scale, y - 2 * scale, 6 * scale, 14 * scale, 0x75502e);
    world_rect(x - 12 * scale, y - 17 * scale, 24 * scale, 13 * scale, 0x28583a);
    world_rect(x - 16 * scale, y - 11 * scale, 32 * scale, 12 * scale, 0x347145);
    world_rect(x - 11 * scale, y - 21 * scale, 22 * scale, 8 * scale, 0x4b8b4d);
    world_rect(x - 7 * scale, y - 18 * scale, 9 * scale, 5 * scale, 0x6da557);
}

static void draw_ground(void)
{
    rect(0, 12, bramble_host->screen_width, bramble_host->screen_height - 24, shade(0x69a85a));
    int start_x = bramble.camera_x / 16 * 16, start_y = bramble.camera_y / 16 * 16;
    for (int y = start_y; y < bramble.camera_y + bramble_host->screen_height; y += 16) {
        for (int x = start_x; x < bramble.camera_x + bramble_host->screen_width; x += 16) {
            unsigned hash = (unsigned)x * 1103515245u ^ (unsigned)y * 2654435761u;
            if ((hash & 3u) == 0) world_rect(x + (hash >> 4 & 7), y + (hash >> 8 & 7), 2, 2, 0x84bd64);
            if ((hash & 15u) == 5) world_rect(x + 11, y + 4, 1, 3, 0x3f8046);
        }
    }
}

static void draw_paths(void)
{
    world_rect(112, 132, 28, 178, 0xb69a68);
    world_rect(126, 270, 382, 42, 0xb69a68);
    world_rect(276, 183, 42, 238, 0xb69a68);
    world_rect(520, 300, 165, 42, 0xb69a68);
    for (int x = 128; x < 680; x += 22) world_rect(x, 290 + ((x / 22) & 1) * 6, 8, 3, 0xd8bd80);
    for (int y = 145; y < 410; y += 24) world_rect(292 + ((y / 24) & 1) * 7, y, 4, 9, 0xd8bd80);
}

static void draw_stream(void)
{
    world_rect(492, 0, 56, BRAMBLE_WORLD_H, 0x3f8fa8);
    world_rect(498, 0, 4, BRAMBLE_WORLD_H, 0x7bc6c1);
    world_rect(540, 0, 3, BRAMBLE_WORLD_H, 0x276f91);
    for (int y = bramble.camera_y / 16 * 16; y < bramble.camera_y + bramble_host->screen_height; y += 16) {
        int drift = (int)((bramble.tick / 3 + (unsigned)y) % 18);
        world_rect(504 + drift, y + 5, 12, 2, 0xa4ded0);
        world_rect(527 - drift / 2, y + 11, 8, 1, 0xd5eee0);
    }
}

static void draw_bridge(void)
{
    world_rect(478, 296, 84, 48, 0x513722);
    world_rect(480, 302, 80, 36, 0xa8723c);
    for (int x = 484; x < 558; x += 9) world_rect(x, 303, 2, 34, 0x6b4629);
    world_rect(477, 299, 86, 3, 0xd1a15a);
    world_rect(477, 338, 86, 3, 0xd1a15a);
    for (int x = 481; x < 562; x += 20) {
        world_rect(x, 292, 3, 12, 0x68452c);
        world_rect(x, 337, 3, 12, 0x68452c);
    }
}

static void cottage(void)
{
    world_rect(74, 92, 92, 44, 0xe1cf92);
    world_rect(67, 82, 106, 20, 0x7d3f2d);
    world_rect(78, 73, 84, 23, 0x965137);
    world_rect(109, 112, 24, 24, 0x674327);
    world_rect(116, 117, 10, 19, 0x422a1d);
    world_rect(88, 108, 16, 14, 0x76b6b2);
    world_rect(91, 111, 4, 8, 0xffd77e);
    world_rect(97, 111, 4, 8, 0xffd77e);
    world_rect(146, 70, 9, 27, 0x72563e);
    flower(83, 132, 0xe06b79); flower(158, 130, 0xf0c35c);
}

static void village_building(int x, int y, int width, int height, unsigned wall,
                             unsigned roof, unsigned awning)
{
    world_rect(x, y + 14, width, height - 14, wall);
    world_rect(x - 5, y, width + 10, 17, roof);
    world_rect(x + width / 2 - 8, y + height - 20, 16, 20, 0x60412d);
    world_rect(x + 8, y + 26, 16, 12, 0x72b0aa);
    world_rect(x + 6, y + 22, 20, 5, awning);
    world_rect(x + width - 26, y + 22, 20, 5, awning);
    world_rect(x + width - 24, y + 26, 16, 12, 0x72b0aa);
}

static void church(void)
{
    world_rect(610, 105, 110, 71, 0xbab69f);
    world_rect(630, 82, 30, 94, 0xc9c5ae);
    world_rect(622, 91, 46, 16, 0x6b6665);
    world_rect(666, 96, 45, 16, 0x6b6665);
    world_rect(641, 63, 8, 24, 0x77706b);
    world_rect(643, 53, 4, 12, 0x77706b);
    world_rect(642, 50, 6, 3, 0xe7d092);
    world_rect(640, 118, 11, 27, 0x45382e);
    world_rect(678, 116, 14, 26, 0x533c62);
    world_rect(681, 119, 5, 8, 0xe6c84f);
    world_rect(687, 119, 3, 8, 0xd55b6d);
    world_rect(681, 129, 4, 9, 0x4a9eb2);
    world_rect(686, 129, 4, 9, 0x77a64e);
    flower(620, 174, 0xc75c81); flower(705, 174, 0xf5dc78);
}

static void draw_places(void)
{
    cottage();
    village_building(342, 142, 92, 58, 0xd6c58d, 0x527044, 0xe7dfb2);
    village_building(374, 245, 92, 58, 0xe2c99c, 0x8c4e3c, 0x83aaca);
    village_building(302, 326, 82, 55, 0xd0c7a8, 0x576f60, 0xd8b564);
    church();
    world_rect(126, 190, 14, 5, 0x75604b);
    world_rect(128, 195, 10, 4, 0x4b4039);
    if (bramble.fire_ticks) {
        world_rect(130, 187, 6, 9, 0xd64f2e);
        world_rect(132, 184, 3, 9, 0xffc34a);
    }
}

static void draw_forest(void)
{
    static const int decorative[][2] = {
        {28, 55}, {38, 142}, {28, 390}, {92, 470}, {165, 520}, {245, 505},
        {330, 535}, {420, 70}, {455, 112}, {565, 60}, {574, 245}, {735, 65},
        {742, 260}, {580, 475}, {675, 555}, {455, 555}, {215, 300}, {82, 540}
    };
    for (size_t i = 0; i < sizeof(decorative) / sizeof(decorative[0]); i++)
        tree(decorative[i][0], decorative[i][1], 1, false);
    for (int i = 0; i < BRAMBLE_TREES; i++) tree(bramble.trees[i].x, bramble.trees[i].y,
        1, bramble.trees[i].regrow > 0);
}

static void draw_garden(void)
{
    world_rect(164, 153, 82, 68, 0x8b6843);
    for (int i = 0; i < BRAMBLE_PLANTS; i++) {
        struct bramble_plant *plant = &bramble.plants[i];
        world_rect(plant->x - 7, plant->y - 5, 14, 11, 0x694b34);
        if (plant->stage == 1) {
            world_rect(plant->x - 1, plant->y - 2, 2, 5, 0x5f9b4d);
            world_rect(plant->x + 1, plant->y, 3, 2, 0x76b95b);
        } else if (plant->stage == 2) {
            world_rect(plant->x - 1, plant->y - 6, 2, 9, 0x4d8c43);
            world_rect(plant->x - 5, plant->y - 3, 5, 3, 0x75ac56);
            world_rect(plant->x + 1, plant->y - 1, 5, 3, 0x75ac56);
        } else if (plant->stage == 3) {
            flower(plant->x - 3, plant->y - 6, i & 1 ? 0xed9bba : 0xf5cf67);
            flower(plant->x + 3, plant->y - 4, i & 1 ? 0x8fc7d1 : 0xe77a8b);
        }
    }
}

static void draw_characters(void)
{
    if (!bramble.cycling) world_sprite(bramble.friend_art, 4, 2, 6, 172, 130, 50, 45, 255);
    for (int i = 0; i < BRAMBLE_NPCS; i++) {
        int bob = (int)((bramble.tick / 30 + (unsigned)i) & 1u);
        world_rect(bramble.npcs[i].x - 9, bramble.npcs[i].y + 12, 18, 4, 0x416d3f);
        world_sprite(bramble.friend_art, 4, 2, bramble.npcs[i].sprite,
                     bramble.npcs[i].x - 25, bramble.npcs[i].y - 29 - bob, 50, 45, 255);
    }
    int frame = bramble.moving ? (int)(bramble.tick / (bramble.cycling ? 4 : 8) % 4) : 0;
    int index = bramble.facing * 4 + frame;
    world_rect((int)bramble.x - 9, (int)bramble.y + 10, 18, 4, 0x3d6b3c);
    if (bramble.cycling) {
        world_sprite(bramble.friend_art, 4, 2, 6, (int)bramble.x - 25, (int)bramble.y - 19, 50, 45, 255);
        world_sprite(bramble.player_art, 4, 4, index, (int)bramble.x - 20, (int)bramble.y - 36, 40, 40, 255);
    } else {
        world_sprite(bramble.player_art, 4, 4, index, (int)bramble.x - 20,
                     (int)bramble.y - 31, 40, 40, 255);
    }
}

static const char *weather_label(void)
{
    static const char *labels[] = {"SUN", "RAIN", "MIST", "WIND"};
    return labels[clampi(bramble.weather, 0, BRAMBLE_WEATHER_COUNT - 1)];
}

static void draw_weather(void)
{
    int width = bramble_host->screen_width, height = bramble_host->screen_height;
    if (bramble.weather == BRAMBLE_RAIN) {
        for (int i = 0; i < 32; i++) {
            unsigned hash = (unsigned)i * 2654435761u;
            int x = (int)((hash + bramble.tick * 3) % (unsigned)(width + 18)) - 9;
            int y = 13 + (int)(((hash >> 9) + bramble.tick * 5) % (unsigned)(height - 30));
            rect(x, y, 1, 7, i & 1 ? 0x9acbd1 : 0xc1e4df);
        }
    } else if (bramble.weather == BRAMBLE_MIST) {
        for (int y = 32; y < height - 18; y += 24) {
            int offset = (int)((bramble.tick / 3 + (unsigned)y) % 30);
            for (int x = -offset; x < width; x += 30) rect(x, y, 18, 2, 0xb8c9b0);
        }
    } else if (bramble.weather == BRAMBLE_WIND) {
        for (int i = 0; i < 12; i++) {
            int x = (int)((i * 71u + bramble.tick * 2u) % (unsigned)(width + 12)) - 6;
            int y = 18 + (i * 37 % (height - 42));
            rect(x, y, 3, 2, i & 1 ? 0xd5b65a : 0x548947);
        }
    }
}

static void draw_hud(void)
{
    char line[96];
    rect(0, 0, bramble_host->screen_width, 12, 0x173c32);
    snprintf(line, sizeof(line), "D%u %02dH%02d  WOOD %d  SEED %d  COIN %d  %s",
             bramble.day, bramble.minute / 60, bramble.minute % 60,
             bramble.wood, bramble.seeds, bramble.coins, weather_label());
    text(3, 2, line, 1, 0xf4e6b4);
    rect(0, bramble_host->screen_height - 12, bramble_host->screen_width, 12, 0x173c32);
    const char *prompt = bramble.message_ticks ? bramble.message : bramble_context_prompt();
    int max = (bramble_host->screen_width - 6) / 6;
    char clipped[64]; snprintf(clipped, sizeof(clipped), "%.*s", max, prompt);
    center_text(bramble_host->screen_height - 10, clipped, 1, 0xf4e6b4);
}

static void wrapped_text(int x, int y, const char *value, int columns, int max_lines, unsigned colour)
{
    char copy[256]; snprintf(copy, sizeof(copy), "%s", value);
    char *cursor = copy;
    for (int line = 0; line < max_lines && *cursor; line++) {
        while (*cursor == ' ') cursor++;
        int length = (int)strlen(cursor);
        if (length > columns) {
            length = columns;
            while (length > 1 && cursor[length] != ' ') length--;
            if (length == 1) length = columns;
        }
        char saved = cursor[length]; cursor[length] = 0;
        text(x, y + line * 9, cursor, 1, colour);
        cursor[length] = saved;
        cursor += length;
    }
}

static void draw_dialog(void)
{
    int y = bramble_host->screen_height - 71;
    rect(7, y, bramble_host->screen_width - 14, 59, 0x2b241e);
    rect(9, y + 2, bramble_host->screen_width - 18, 55, 0xf1dfb4);
    text(15, y + 7, bramble.dialog_name, 1, 0x5f3529);
    wrapped_text(15, y + 19, bramble.dialog_text, 42, 3, 0x3d3a32);
    if (bramble.dialog_service != BRAMBLE_NO_SERVICE) {
        const char *service = bramble.dialog_service == BRAMBLE_SHOP ? "Y TRADE" :
                              bramble.dialog_service == BRAMBLE_BAKERY ? "Y BUY BUN" : "Y REST";
        text(15, y + 49, service, 1, 0x4d7446);
    }
    text(bramble_host->screen_width - 58, y + 49, "B CLOSE", 1, 0x765e45);
}

static void clip_line(char *output, size_t capacity, const char *input, int columns)
{
    int length = (int)strlen(input);
    if (length <= columns) snprintf(output, capacity, "%s", input);
    else snprintf(output, capacity, "%.*s...", columns - 3, input);
}

static void draw_director(void)
{
    rect(0, 0, bramble_host->screen_width, bramble_host->screen_height, 0x173c32);
    center_text(11, "WORLD DIRECTOR", 2, 0xf1d784);
    text(12, 35, "LOCAL SYSTEMS", 1, 0x91c783);
    char row[96];
    snprintf(row, sizeof(row), "  WEATHER         %s", weather_label());
    text(12, 49, row, 1, bramble.director_cursor == 0 ? 0xffdd75 : 0xe9e2bd);
    snprintf(row, sizeof(row), "  TIME            %02dH%02d",
             bramble.minute / 60, bramble.minute % 60);
    text(12, 62, row, 1, bramble.director_cursor == 1 ? 0xffdd75 : 0xe9e2bd);
    snprintf(row, sizeof(row), "  GROWTH          %dX", bramble.growth_boost);
    text(12, 75, row, 1, bramble.director_cursor == 2 ? 0xffdd75 : 0xe9e2bd);
    text(12, 95, "LLM STATE", 1, 0x91c783);
    char clipped[64];
    clip_line(clipped, sizeof(clipped), bramble.director.long_theme, 42);
    text(12, 109, "LONG", 1, 0xd8a85e); text(48, 109, clipped, 1, 0xe9e2bd);
    clip_line(clipped, sizeof(clipped), bramble.director.medium_event, 42);
    text(12, 124, "MID", 1, 0x7fc0c0); text(42, 124, clipped, 1, 0xe9e2bd);
    clip_line(clipped, sizeof(clipped), bramble.director.short_focus, 42);
    text(12, 139, "NOW", 1, 0xd88993); text(42, 139, clipped, 1, 0xe9e2bd);
    snprintf(row, sizeof(row), "REV %lu", bramble.director.revision);
    text(12, 159, row, 1, 0x779080);
    center_text(bramble_host->screen_height - 19, "D-PAD CHANGE    X/A CLOSE", 1, 0xf1d784);
}

static void draw_title(void)
{
    if (!splash_draw(&title_art, bramble_host)) {
        rect(0, 0, bramble_host->screen_width, bramble_host->screen_height, 0x244b38);
        center_text(68, "BRAMBLE", 3, 0xf3dfaa);
        center_text(94, "HOLLOW", 3, 0xf3dfaa);
    }
    rect(0, bramble_host->screen_height - 24, bramble_host->screen_width, 24, 0x1d2d25);
    center_text(bramble_host->screen_height - 20, "PRESS B TO BEGIN", 1, 0xffe6a1);
    center_text(bramble_host->screen_height - 10, "A LIVING WOODLAND STORY", 1, 0xa9d096);
}

void bramble_render(void)
{
    if (bramble_host->profile_scope) bramble_host->profile_scope(bramble_host->context, "scene.bramble", true);
    if (bramble.phase == BRAMBLE_TITLE) draw_title();
    else if (bramble.phase == BRAMBLE_DIRECTOR) draw_director();
    else {
        draw_ground();
        draw_paths();
        draw_stream();
        draw_bridge();
        draw_places();
        draw_garden();
        draw_forest();
        draw_characters();
        draw_weather();
        draw_hud();
        if (bramble.phase == BRAMBLE_DIALOG) draw_dialog();
    }
    if (bramble_host->profile_scope) bramble_host->profile_scope(bramble_host->context, "scene.bramble", false);
}
