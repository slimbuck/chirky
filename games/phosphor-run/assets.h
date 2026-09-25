#ifndef PHOSPHOR_ASSETS_H
#define PHOSPHOR_ASSETS_H

#include <stdbool.h>
#include "chirky.h"

#define CONTENT_LIMIT 256
#define FRAME_LIMIT 64

/* Text grids are stored top-to-bottom; world coordinates increase upwards. */
struct grid { int width, height; char *pixels; };
struct animation { int count, ticks; struct grid frames[FRAME_LIMIT]; };
struct content_entry { char id[64], path[512]; struct animation animation; struct grid level; };
struct content {
    int level_count, sprite_count;
    struct content_entry levels[CONTENT_LIMIT], sprites[CONTENT_LIMIT];
};

/* Load into zero-initialized outputs. Caller owns grids' pixels; animations and
   catalogs have matching free functions, which also reset them to empty. */
bool grid_load(const char *path, struct grid *out);
void assets_bind(const struct chirky_host_api *api);
bool animation_load(const char *path, struct animation *out);
void animation_free(struct animation *animation);
bool content_load(const char *path, struct content *out);
void content_free(struct content *content);
const struct animation *content_animation(const struct content *content, const char *id);
const struct grid *animation_frame(const struct animation *animation, int tick);

#endif
