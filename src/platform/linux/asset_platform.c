#include "asset_platform.h"

#include <pthread.h>
#include <stdlib.h>

struct asset_platform {
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    bool started;
};

struct asset_platform *asset_platform_create(void)
{
    struct asset_platform *platform=calloc(1,sizeof(*platform));
    if(!platform)return NULL;
    if(pthread_mutex_init(&platform->mutex,NULL))goto fail;
    if(pthread_cond_init(&platform->wake,NULL))goto fail_mutex;
    return platform;
fail_mutex:
    pthread_mutex_destroy(&platform->mutex);
fail:
    free(platform);return NULL;
}

bool asset_platform_start(struct asset_platform *platform,
                          void *(*worker)(void *),void *context)
{
    if(!platform || platform->started || pthread_create(&platform->worker,NULL,worker,context))return false;
    platform->started=true;return true;
}

bool asset_platform_async(const struct asset_platform *platform)
{ return platform!=NULL; }
void asset_platform_lock(struct asset_platform *platform)
{ pthread_mutex_lock(&platform->mutex); }
void asset_platform_unlock(struct asset_platform *platform)
{ pthread_mutex_unlock(&platform->mutex); }
void asset_platform_signal(struct asset_platform *platform)
{ pthread_cond_signal(&platform->wake); }
void asset_platform_wait(struct asset_platform *platform)
{ pthread_cond_wait(&platform->wake,&platform->mutex); }
void asset_platform_join(struct asset_platform *platform)
{ if(platform && platform->started){pthread_join(platform->worker,NULL);platform->started=false;} }
void asset_platform_destroy(struct asset_platform *platform)
{
    if(!platform)return;
    pthread_cond_destroy(&platform->wake);pthread_mutex_destroy(&platform->mutex);free(platform);
}
