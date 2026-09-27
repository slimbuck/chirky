#include "asset_platform.h"

#include <stdlib.h>

struct asset_platform { bool active; };

struct asset_platform *asset_platform_create(void)
{ return calloc(1,sizeof(struct asset_platform)); }
bool asset_platform_start(struct asset_platform *platform,
                          void *(*worker)(void *),void *context)
{ (void)worker;(void)context;if(platform)platform->active=true;return platform!=NULL; }
bool asset_platform_async(const struct asset_platform *platform)
{ (void)platform;return false; }
void asset_platform_lock(struct asset_platform *platform) { (void)platform; }
void asset_platform_unlock(struct asset_platform *platform) { (void)platform; }
void asset_platform_signal(struct asset_platform *platform) { (void)platform; }
void asset_platform_wait(struct asset_platform *platform) { (void)platform; }
void asset_platform_join(struct asset_platform *platform) { (void)platform; }
void asset_platform_destroy(struct asset_platform *platform) { free(platform); }
