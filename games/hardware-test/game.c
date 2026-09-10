#include "two_forty.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct settings {
    int width;
    int height;
    int speed_x;
    int speed_y;
    char pattern[512];
    char sound[512];
    char sound_device[128];
};

struct image {
    int width;
    int height;
    unsigned char *pixels;
};

static const struct two_forty_host_api *host;
static struct settings settings;
static struct image image;
static int x;
static int y;
static int velocity_x;
static int velocity_y;
static bool automatic;

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity > 0) snprintf(destination, capacity, "%s", source);
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return text;
}

static bool read_settings(const char *path)
{
    settings.width = 168;
    settings.height = 120;
    settings.speed_x = 2;
    settings.speed_y = 1;
    copy_text(settings.pattern, sizeof(settings.pattern),
              "games/hardware-test/assets/colour-bars.ppm");
    copy_text(settings.sound, sizeof(settings.sound),
              "games/hardware-test/assets/edge-beep.wav");
    copy_text(settings.sound_device, sizeof(settings.sound_device), "plughw:0,0");

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "hardware-test: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *entry = trim(line);
        if (*entry == '\0' || *entry == '#' || *entry == ';') continue;
        char *separator = strchr(entry, '=');
        if (separator == NULL) continue;
        *separator = '\0';
        char *key = trim(entry);
        char *value = trim(separator + 1);
        if (strcmp(key, "box_width") == 0) settings.width = atoi(value);
        else if (strcmp(key, "box_height") == 0) settings.height = atoi(value);
        else if (strcmp(key, "speed_x") == 0) settings.speed_x = atoi(value);
        else if (strcmp(key, "speed_y") == 0) settings.speed_y = atoi(value);
        else if (strcmp(key, "pattern") == 0)
            copy_text(settings.pattern, sizeof(settings.pattern), value);
        else if (strcmp(key, "bounce_sound") == 0)
            copy_text(settings.sound, sizeof(settings.sound), value);
        else if (strcmp(key, "sound_device") == 0)
            copy_text(settings.sound_device, sizeof(settings.sound_device), value);
    }
    fclose(file);
    return settings.width >= 8 && settings.height >= 8 &&
           settings.speed_x != 0 && settings.speed_y != 0;
}

static bool next_ppm_token(FILE *file, char *token, size_t capacity)
{
    int character;
    do {
        character = fgetc(file);
        if (character == '#') {
            while (character != '\n' && character != EOF) character = fgetc(file);
        }
    } while (character != EOF && (character == ' ' || character == '\t' ||
                                   character == '\r' || character == '\n'));
    if (character == EOF) return false;

    size_t length = 0;
    while (character != EOF && character != ' ' && character != '\t' &&
           character != '\r' && character != '\n' && character != '#') {
        if (length + 1 < capacity) token[length++] = (char)character;
        character = fgetc(file);
    }
    token[length] = '\0';
    return length > 0;
}

static bool read_image(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) return false;
    char token[64];
    if (!next_ppm_token(file, token, sizeof(token)) || strcmp(token, "P3") != 0 ||
        !next_ppm_token(file, token, sizeof(token))) {
        fclose(file);
        return false;
    }
    image.width = atoi(token);
    if (!next_ppm_token(file, token, sizeof(token))) { fclose(file); return false; }
    image.height = atoi(token);
    if (!next_ppm_token(file, token, sizeof(token))) { fclose(file); return false; }
    int maximum = atoi(token);
    if (image.width <= 0 || image.height <= 0 || maximum <= 0) {
        fclose(file);
        return false;
    }

    size_t size = (size_t)image.width * image.height * 3u;
    image.pixels = malloc(size);
    if (image.pixels == NULL) { fclose(file); return false; }
    for (size_t index = 0; index < size; ++index) {
        if (!next_ppm_token(file, token, sizeof(token))) {
            free(image.pixels);
            image.pixels = NULL;
            fclose(file);
            return false;
        }
        int value = atoi(token);
        if (value < 0) value = 0;
        if (value > maximum) value = maximum;
        image.pixels[index] = (unsigned char)((value * 255) / maximum);
    }
    fclose(file);
    return true;
}

static bool game_init(const struct two_forty_host_api *host_api, const char *config_path)
{
    host = host_api;
    if (!read_settings(config_path) || !read_image(settings.pattern)) return false;
    if (settings.width > host->screen_width || settings.height > host->screen_height)
        return false;
    x = (host->screen_width - settings.width) / 2;
    y = (host->screen_height - settings.height) / 2;
    velocity_x = settings.speed_x;
    velocity_y = settings.speed_y;
    automatic = true;
    return true;
}

static void game_shutdown(void)
{
    free(image.pixels);
    image.pixels = NULL;
    host = NULL;
}

static void game_update(const struct two_forty_input *input)
{
    const int manual_speed = 3;
    int dx = 0;
    int dy = 0;
    if (input->buttons[TWO_FORTY_BUTTON_LEFT]) dx -= manual_speed;
    if (input->buttons[TWO_FORTY_BUTTON_RIGHT]) dx += manual_speed;
    if (input->buttons[TWO_FORTY_BUTTON_UP]) dy += manual_speed;
    if (input->buttons[TWO_FORTY_BUTTON_DOWN]) dy -= manual_speed;
    if (input->button_pressed[TWO_FORTY_BUTTON_B]) automatic = !automatic;
    if (input->button_pressed[TWO_FORTY_BUTTON_Y])
        host->play_sound(host->context, settings.sound_device, settings.sound);

    if (dx != 0 || dy != 0) {
        automatic = false;
        x += dx;
        y += dy;
    } else if (automatic) {
        x += velocity_x;
        y += velocity_y;
    }

    bool bounced = false;
    int max_x = host->screen_width - settings.width;
    int max_y = host->screen_height - settings.height;
    if (x < 0) { x = 0; velocity_x = abs(velocity_x); bounced = true; }
    else if (x > max_x) { x = max_x; velocity_x = -abs(velocity_x); bounced = true; }
    if (y < 0) { y = 0; velocity_y = abs(velocity_y); bounced = true; }
    else if (y > max_y) { y = max_y; velocity_y = -abs(velocity_y); bounced = true; }
    if (bounced) host->play_sound(host->context, settings.sound_device, settings.sound);
}

static void game_render(void)
{
    host->fill_rect(host->context, 0, 0, host->screen_width, host->screen_height,
                    1, 1, 2);
    host->fill_rect(host->context, x - 2, y - 2,
                    settings.width + 4, settings.height + 4, 218, 218, 218);
    for (int row = 0; row < image.height; ++row) {
        int y0 = y + (row * settings.height) / image.height;
        int y1 = y + ((row + 1) * settings.height) / image.height;
        for (int column = 0; column < image.width; ++column) {
            int x0 = x + (column * settings.width) / image.width;
            int x1 = x + ((column + 1) * settings.width) / image.width;
            int source_row = image.height - 1 - row;
            const unsigned char *rgb = image.pixels +
                ((size_t)source_row * image.width + column) * 3u;
            host->fill_rect(host->context, x0, y0, x1 - x0, y1 - y0,
                            rgb[0], rgb[1], rgb[2]);
        }
    }
}

static const struct two_forty_game_api api = {
    .abi_version = TWO_FORTY_ABI_VERSION,
    .init = game_init,
    .shutdown = game_shutdown,
    .update = game_update,
    .render = game_render,
};

const struct two_forty_game_api *two_forty_game_entry(void)
{
    return &api;
}
