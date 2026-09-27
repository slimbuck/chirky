#ifndef CHIRKY_ASSET_PLATFORM_H
#define CHIRKY_ASSET_PLATFORM_H

#include <stdbool.h>

struct asset_platform;

struct asset_platform *asset_platform_create(void);
bool asset_platform_start(struct asset_platform *platform,
                          void *(*worker)(void *),void *context);
bool asset_platform_async(const struct asset_platform *platform);
void asset_platform_lock(struct asset_platform *platform);
void asset_platform_unlock(struct asset_platform *platform);
void asset_platform_signal(struct asset_platform *platform);
void asset_platform_wait(struct asset_platform *platform);
void asset_platform_join(struct asset_platform *platform);
void asset_platform_destroy(struct asset_platform *platform);

#endif
