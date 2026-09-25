#include "assets.h"
#include "asset_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const struct chirky_host_api *asset_api;
void assets_bind(const struct chirky_host_api *api) { asset_api=api; }

void animation_free(struct animation *animation)
{
    for (int i = 0; i < animation->count; ++i) free(animation->frames[i].pixels);
    memset(animation, 0, sizeof(*animation));
}

static bool read_grids(const char *path, struct animation *out, bool sprite)
{
    struct chirky_file source=chirky_file_open(asset_api,path);
    FILE *file = source.stream;
    if (!file) return false;
    struct animation result = {.count=1, .ticks=6};
    char line[1024];
    bool ok = true;
    int maximum = sprite ? 128 : 512;
    while (ok && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (!strncmp(line, "# ", 2)) {
            if (!strncmp(line, "# ticks=", 8)) {
                char extra;
                if (!sprite || sscanf(line+8, "%d%c", &result.ticks, &extra) != 1 ||
                    result.ticks < 1 || result.ticks > 600) ok = false;
            }
            continue;
        }
        struct grid *frame = &result.frames[result.count-1];
        if (sprite && !strcmp(line, "---")) {
            if (!frame->height || result.count == FRAME_LIMIT) ok = false;
            else result.count++;
            continue;
        }
        int width = (int)strlen(line);
        if (width > maximum || frame->height == maximum ||
            (frame->width && frame->width != width) ||
            strspn(line, sprite ? ".nscawrg" : ".#^oCSE") != (size_t)width) {
            ok = false; break;
        }
        char *pixels = realloc(frame->pixels, (size_t)width * (frame->height+1));
        if (!pixels) { ok = false; break; }
        frame->pixels = pixels;
        memcpy(pixels + (size_t)width * frame->height, line, (size_t)width);
        frame->width = width; frame->height++;
    }
    if (ferror(file)) ok = false;
    chirky_file_close(&source);
    for (int i=0; i<result.count; i++)
        if (!result.frames[i].height || result.frames[i].width != result.frames[0].width ||
            result.frames[i].height != result.frames[0].height) ok = false;
    if (!ok) { animation_free(&result); fprintf(stderr, "phosphor-run: invalid grid %s\n", path); return false; }
    *out = result;
    return true;
}

bool grid_load(const char *path, struct grid *out)
{
    struct animation data = {0};
    if (!read_grids(path, &data, false)) return false;
    int spawn=0, exit=0, solid=0;
    struct grid *grid = &data.frames[0];
    for (int i=0; i<grid->width*grid->height; i++) {
        spawn += grid->pixels[i]=='S'; exit += grid->pixels[i]=='E'; solid += grid->pixels[i]=='#';
    }
    if (spawn != 1 || exit != 1 || !solid) { animation_free(&data); return false; }
    *out = *grid;
    return true;
}

bool animation_load(const char *path, struct animation *out) { return read_grids(path, out, true); }

static bool valid_id(const char *id)
{
    size_t length = strlen(id);
    if (!length || length > 63 || !isalnum((unsigned char)id[0])) return false;
    for (; *id; id++) if (!(*id >= 'a' && *id <= 'z') && !isdigit((unsigned char)*id) && *id != '-') return false;
    return true;
}

static bool valid_path(const char *path)
{
    if (!*path || path[0]=='/' || strlen(path)>400) return false;
    const char *part=path;
    for (const char *p=path;;p++) {
        if (*p=='/' || !*p) {
            size_t n=(size_t)(p-part);
            if (!n || (n==1 && *part=='.') || (n==2 && !strncmp(part,"..",2))) return false;
            part=p+1;
            if (!*p) break;
        } else if (!isalnum((unsigned char)*p) && *p!='.' && *p!='_' && *p!='-') return false;
    }
    return true;
}

void content_free(struct content *content)
{
    for (int i=0; i<content->sprite_count; i++) animation_free(&content->sprites[i].animation);
    for (int i=0; i<content->level_count; i++) free(content->levels[i].level.pixels);
    memset(content, 0, sizeof(*content));
}

bool content_load(const char *path, struct content *out)
{
    struct chirky_file source=chirky_file_open(asset_api,path);
    FILE *file=source.stream;
    if (!file) return false;
    char directory[512], line[1024];
    snprintf(directory,sizeof(directory),"%s",path);
    char *slash=strrchr(directory,'/');
    if (slash) slash[1]=0; else directory[0]=0;
    bool ok=true;
    while (ok && fgets(line,sizeof(line),file)) {
        line[strcspn(line,"\r\n")]=0;
        if (!*line || *line=='#') continue;
        char *value=strchr(line,'=');
        if (!value) { ok=false; break; }
        *value++=0;
        bool is_level=!strncmp(line,"level.",6);
        bool is_sprite=!strncmp(line,"sprite.",7);
        char *id=line+(is_level?6:7);
        if ((!is_level && !is_sprite) || !valid_id(id) || !valid_path(value)) { ok=false; break; }
        int *count=is_level?&out->level_count:&out->sprite_count;
        struct content_entry *entries=is_level?out->levels:out->sprites;
        if (*count==CONTENT_LIMIT) { ok=false; break; }
        for (int i=0;i<*count;i++) if (!strcmp(entries[i].id,id)) ok=false;
        if (!ok) break;
        struct content_entry *entry=&entries[(*count)++];
        snprintf(entry->id,sizeof(entry->id),"%s",id);
        if (snprintf(entry->path,sizeof(entry->path),"%s%s",directory,value)>=(int)sizeof(entry->path)) { ok=false; break; }
        if (is_sprite) ok=animation_load(entry->path,&entry->animation);
        else ok=grid_load(entry->path,&entry->level);
    }
    if (ferror(file)) ok=false;
    chirky_file_close(&source);
    if (!out->level_count || !out->sprite_count) ok=false;
    if (!ok) content_free(out);
    return ok;
}

const struct animation *content_animation(const struct content *content, const char *id)
{
    for (int i=0;i<content->sprite_count;i++)
        if (!strcmp(content->sprites[i].id,id)) return &content->sprites[i].animation;
    return NULL;
}

const struct grid *animation_frame(const struct animation *animation, int tick)
{
    if (!animation || !animation->count) return NULL;
    return &animation->frames[(tick/animation->ticks)%animation->count];
}
