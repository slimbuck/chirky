#include "runtime.h"

bool chirky_runtime_start(struct chirky_runtime *runtime,
                          const struct chirky_game_api *game,
                          const struct chirky_host_api *host,
                          const char *config_path)
{
    if (!runtime || !game || !host || !config_path || runtime->active ||
        game->abi_version != CHIRKY_ABI_VERSION ||
        host->abi_version != CHIRKY_ABI_VERSION || !game->init ||
        !game->shutdown || !game->update || !game->render) return false;
    runtime->game = game;
    if (!game->init(host, config_path)) {
        runtime->game = NULL;
        game->shutdown();
        return false;
    }
    runtime->active = true;
    return true;
}

void chirky_runtime_stop(struct chirky_runtime *runtime)
{
    if (!runtime || !runtime->active) return;
    const struct chirky_game_api *game = runtime->game;
    runtime->game = NULL;
    runtime->active = false;
    game->shutdown();
}

void chirky_runtime_update(struct chirky_runtime *runtime,
                           const struct chirky_input *input)
{
    if (runtime && runtime->active) runtime->game->update(input);
}

void chirky_runtime_render(struct chirky_runtime *runtime)
{
    if (runtime && runtime->active) runtime->game->render();
}
