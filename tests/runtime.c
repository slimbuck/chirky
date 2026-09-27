#include "runtime.h"

#include <assert.h>
#include <string.h>

static int initialized,shutdowns,updates,renders;
static bool should_start;

static bool init(const struct chirky_host_api *host,const char *config)
{
    initialized++;
    return should_start && host->abi_version==CHIRKY_ABI_VERSION &&
        !strcmp(config,"games/test/game.conf");
}
static void shutdown(void) { shutdowns++; }
static void update(const struct chirky_input *input)
{ assert(input->buttons[CHIRKY_BUTTON_B]);updates++; }
static void render(void) { renders++; }

int main(void)
{
    struct chirky_runtime runtime={0};
    struct chirky_host_api host={.abi_version=CHIRKY_ABI_VERSION};
    const struct chirky_game_api game={.abi_version=CHIRKY_ABI_VERSION,
        .init=init,.shutdown=shutdown,.update=update,.render=render};
    struct chirky_input input={0};input.buttons[CHIRKY_BUTTON_B]=true;

    should_start=false;
    assert(!chirky_runtime_start(&runtime,&game,&host,"games/test/game.conf"));
    assert(initialized==1 && shutdowns==1 && !chirky_runtime_active(&runtime));
    should_start=true;
    assert(chirky_runtime_start(&runtime,&game,&host,"games/test/game.conf"));
    assert(chirky_runtime_active(&runtime));
    assert(!chirky_runtime_start(&runtime,&game,&host,"games/test/game.conf"));
    chirky_runtime_update(&runtime,&input);chirky_runtime_render(&runtime);
    assert(updates==1 && renders==1);
    chirky_runtime_stop(&runtime);chirky_runtime_stop(&runtime);
    assert(shutdowns==2 && !chirky_runtime_active(&runtime));

    struct chirky_game_api incompatible=game;incompatible.abi_version--;
    assert(!chirky_runtime_start(&runtime,&incompatible,&host,"games/test/game.conf"));
    assert(initialized==2 && shutdowns==2);
    return 0;
}
