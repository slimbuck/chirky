#ifndef CHIRKY_IMAGE_CACHE_H
#define CHIRKY_IMAGE_CACHE_H
#include "chirky.h"
#include "rect_renderer.h"

#define IMAGE_CACHE_CAPACITY 128
struct image_cache_entry { chirky_asset asset; struct rect_renderer_texture texture; };
struct image_cache { struct image_cache_entry entries[IMAGE_CACHE_CAPACITY]; };
static inline void image_cache_clear(struct image_cache *cache,struct rect_renderer *renderer)
{
    for(unsigned i=0;i<IMAGE_CACHE_CAPACITY;i++) {
        rect_renderer_texture_delete(renderer,&cache->entries[i].texture);
        cache->entries[i].asset=0;
    }
}
static inline const struct rect_renderer_texture *image_cache_get(struct image_cache *cache,
    struct rect_renderer *renderer,const struct chirky_host_api *api,chirky_asset image)
{
    if(!image || api->asset_status(api->context,image)!=CHIRKY_ASSET_READY)return NULL;
    struct image_cache_entry *entry=NULL,*empty=NULL;
    for(unsigned i=0;i<IMAGE_CACHE_CAPACITY;i++) {
        if(cache->entries[i].asset==image){entry=&cache->entries[i];break;}
        if(!cache->entries[i].asset && !empty)empty=&cache->entries[i];
    }
    if(!entry) {
        if(!empty)for(unsigned i=0;i<IMAGE_CACHE_CAPACITY;i++)
            if(api->asset_status(api->context,cache->entries[i].asset)==CHIRKY_ASSET_FAILED) {
                empty=&cache->entries[i];rect_renderer_texture_delete(renderer,&empty->texture);
                empty->asset=0;break;
            }
        if(!empty)return NULL;
        struct chirky_asset_view view=api->asset_data(api->context,image);
        chirky_scope(api,"assets.upload",true);
        bool ok=rect_renderer_texture_create(renderer,&empty->texture,(int)view.width,(int)view.height,view.data,view.size);
        chirky_scope(api,"assets.upload",false);
        if(!ok)return NULL;
        entry=empty;entry->asset=image;
    }
    return &entry->texture;
}
static inline void image_cache_draw(struct image_cache *cache,struct rect_renderer *renderer,
    const struct chirky_host_api *api,chirky_asset image,int x,int y,int w,int h,
    int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,unsigned char b,unsigned char a,
    bool flip,int offset_x,int offset_y)
{
    const struct rect_renderer_texture *texture=image_cache_get(cache,renderer,api,image);
    if(!texture)return;
    rect_renderer_sprite_clipped(renderer,texture,x+offset_x,y+offset_y,w,h,sx,sy,sw,sh,
        r,g,b,a,flip,offset_x,offset_y,api->screen_width,api->screen_height);
}
#endif
