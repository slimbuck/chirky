#ifndef CHIRKY_RUNTIME_H
#define CHIRKY_RUNTIME_H

#include "chirky.h"

struct chirky_runtime {
    const struct chirky_game_api *game;
    bool active;
};

bool chirky_runtime_start(struct chirky_runtime *runtime,
                          const struct chirky_game_api *game,
                          const struct chirky_host_api *host,
                          const char *config_path);
void chirky_runtime_stop(struct chirky_runtime *runtime);
void chirky_runtime_update(struct chirky_runtime *runtime,
                           const struct chirky_input *input);
void chirky_runtime_render(struct chirky_runtime *runtime);

static inline bool chirky_runtime_active(const struct chirky_runtime *runtime)
{
    return runtime && runtime->active;
}

#endif
