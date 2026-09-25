#include "game_state.h"
#include "asset_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return text;
}

static struct colour colour_from_hex(const char *text, struct colour fallback)
{
    unsigned int value;
    if (strlen(text) != 6 || sscanf(text, "%x", &value) != 1) return fallback;
    return (struct colour){(value >> 16) & 255u, (value >> 8) & 255u, value & 255u};
}

static void default_settings(void)
{
    settings = (struct settings){
        .move_accel=.34f,.max_run_speed=2.35f,.ground_friction=.72f,.air_drag=.95f,
        .gravity=.34f,.max_fall_speed=5.8f,.jump_speed=5.35f,.dash_speed=6.2f,
        .camera_lag=.12f,.coyote_frames=7,.jump_buffer_frames=7,.dash_frames=8,
        .background={5,10,20},.deep={9,26,43},.platform={22,60,85},
        .edge={53,215,211},.phosphor={156,255,87},.amber={255,180,59},
        .hazard={255,91,79},.paper={244,241,207}
    };
    copy_text(settings.content, sizeof(settings.content), "games/phosphor-run/content.conf");
    copy_text(settings.sound_device, sizeof(settings.sound_device), "plughw:0,0");
    copy_text(settings.jump_sound, sizeof(settings.jump_sound), "games/phosphor-run/assets/jump.wav");
    copy_text(settings.dash_sound, sizeof(settings.dash_sound), "games/phosphor-run/assets/dash.wav");
    copy_text(settings.shard_sound, sizeof(settings.shard_sound), "games/phosphor-run/assets/shard.wav");
    copy_text(settings.checkpoint_sound, sizeof(settings.checkpoint_sound), "games/phosphor-run/assets/checkpoint.wav");
    copy_text(settings.death_sound, sizeof(settings.death_sound), "games/phosphor-run/assets/death.wav");
    copy_text(settings.win_sound, sizeof(settings.win_sound), "games/phosphor-run/assets/win.wav");
}

bool load_settings(const char *path)
{
    default_settings();
    struct chirky_file source=chirky_file_open(host,path);
    FILE *file = source.stream;
    if (file == NULL) return false;
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *entry = trim(line);
        if (!*entry || *entry == '#' || *entry == ';') continue;
        char *separator = strchr(entry, '=');
        if (separator == NULL) continue;
        *separator = '\0';
        char *key = trim(entry), *value = trim(separator + 1);
        if (!strcmp(key,"move_accel")) settings.move_accel = strtof(value,NULL);
        else if (!strcmp(key,"max_run_speed")) settings.max_run_speed = strtof(value,NULL);
        else if (!strcmp(key,"ground_friction")) settings.ground_friction = strtof(value,NULL);
        else if (!strcmp(key,"air_drag")) settings.air_drag = strtof(value,NULL);
        else if (!strcmp(key,"gravity")) settings.gravity = strtof(value,NULL);
        else if (!strcmp(key,"max_fall_speed")) settings.max_fall_speed = strtof(value,NULL);
        else if (!strcmp(key,"jump_speed")) settings.jump_speed = strtof(value,NULL);
        else if (!strcmp(key,"dash_speed")) settings.dash_speed = strtof(value,NULL);
        else if (!strcmp(key,"camera_lag")) settings.camera_lag = strtof(value,NULL);
        else if (!strcmp(key,"coyote_frames")) settings.coyote_frames = atoi(value);
        else if (!strcmp(key,"jump_buffer_frames")) settings.jump_buffer_frames = atoi(value);
        else if (!strcmp(key,"dash_frames")) settings.dash_frames = atoi(value);
        else if (!strcmp(key,"content")) copy_text(settings.content,sizeof(settings.content),value);
        else if (!strcmp(key,"start_level")) settings.start_level=atoi(value);
        else if (!strcmp(key,"sound_device")) copy_text(settings.sound_device,sizeof(settings.sound_device),value);
        else if (!strcmp(key,"sound_jump")) copy_text(settings.jump_sound,sizeof(settings.jump_sound),value);
        else if (!strcmp(key,"sound_dash")) copy_text(settings.dash_sound,sizeof(settings.dash_sound),value);
        else if (!strcmp(key,"sound_shard")) copy_text(settings.shard_sound,sizeof(settings.shard_sound),value);
        else if (!strcmp(key,"sound_checkpoint")) copy_text(settings.checkpoint_sound,sizeof(settings.checkpoint_sound),value);
        else if (!strcmp(key,"sound_death")) copy_text(settings.death_sound,sizeof(settings.death_sound),value);
        else if (!strcmp(key,"sound_win")) copy_text(settings.win_sound,sizeof(settings.win_sound),value);
        else if (!strcmp(key,"background")) settings.background=colour_from_hex(value,settings.background);
        else if (!strcmp(key,"deep_machinery")) settings.deep=colour_from_hex(value,settings.deep);
        else if (!strcmp(key,"platform")) settings.platform=colour_from_hex(value,settings.platform);
        else if (!strcmp(key,"platform_edge")) settings.edge=colour_from_hex(value,settings.edge);
        else if (!strcmp(key,"phosphor")) settings.phosphor=colour_from_hex(value,settings.phosphor);
        else if (!strcmp(key,"amber")) settings.amber=colour_from_hex(value,settings.amber);
        else if (!strcmp(key,"hazard")) settings.hazard=colour_from_hex(value,settings.hazard);
        else if (!strcmp(key,"paper")) settings.paper=colour_from_hex(value,settings.paper);
    }
    chirky_file_close(&source);
    return true;
}

