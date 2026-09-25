#ifndef CHIRKY_SPLASH_ART_H
#define CHIRKY_SPLASH_ART_H
#include "chirky.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ABI9 optionally uses decoded IMAGE assets; zero handles are invalid.
   Legacy callers retain the bounded P6/rectangle path, byte for byte. */
struct splash_art {
    unsigned char *pixels;
    int width, height;
    chirky_asset image;
    struct chirky_host_api asset_api;
    bool asset_requested;
};
static inline void splash_free(struct splash_art *art)
{
    if (art->image) art->asset_api.asset_release(art->asset_api.context,art->image);
    free(art->pixels); memset(art,0,sizeof(*art));
}
static inline bool splash_load_file(struct splash_art *art, const char *path)
{
    splash_free(art);
    if (!path) return false;
    FILE *file=fopen(path,"rb");
    if (!file) return false;
    int width=0,height=0,max=0;
    char magic[3]={0};
    bool valid=fscanf(file,"%2s %d %d %d",magic,&width,&height,&max)==4 &&
        !strcmp(magic,"P6") && width>0 && height>0 && width<=320 && height<=240 && max==255;
    int separator=fgetc(file);
    if (separator=='\r') separator=fgetc(file);
    valid=valid && (separator=='\n' || separator==' ' || separator=='\t');
    if (valid) {
        size_t bytes=(size_t)width*height*3;
        art->pixels=malloc(bytes);
        valid=art->pixels && fread(art->pixels,1,bytes,file)==bytes;
    }
    fclose(file);
    if (!valid) { splash_free(art); return false; }
    art->width=width; art->height=height; return true;
}
/* True means the request was accepted; splash_draw returns false until ready.
   A modern request never falls back to synchronous file IO, even on failure.
   Keep the requesting context alive through splash_free. */
static inline bool splash_load_file_api(struct splash_art *art,
                                       const struct chirky_host_api *api, const char *path)
{
    if (api && api->asset_request && api->asset_status && api->asset_data &&
        api->asset_release && api->draw_sprite) {
        splash_free(art);
        if (!path) return false;
        art->asset_requested=true;art->asset_api=*api;
        art->image=api->asset_request(api->context,path,CHIRKY_ASSET_IMAGE);
        return art->image!=0;
    }
    return splash_load_file(art,path);
}
static inline bool splash_load_api(struct splash_art *art,
                                  const struct chirky_host_api *api, const char *config)
{
    if (!config) { splash_free(art); return false; }
    char path[1024];
    const char *slash=strrchr(config,'/');
    int length=slash?(int)(slash-config):1;
    if (snprintf(path,sizeof(path),"%.*s/assets/artwork/splash.ppm",length,slash?config:".") >= (int)sizeof(path)) { splash_free(art); return false; }
    return splash_load_file_api(art,api,path);
}
static inline bool splash_load(struct splash_art *art, const char *config)
{
    return splash_load_api(art,NULL,config);
}
static inline bool splash_draw(const struct splash_art *art, const struct chirky_host_api *api)
{
    if (!api || api->screen_width<=0 || api->screen_height<=0) return false;
    if (art->asset_requested) {
        const struct chirky_host_api *owner=&art->asset_api;
        if (!art->image || !api->draw_sprite || api->context!=owner->context ||
            owner->asset_status(owner->context,art->image)!=CHIRKY_ASSET_READY) return false;
        struct chirky_asset_view view=owner->asset_data(owner->context,art->image);
        if (!view.data || !view.width || !view.height || view.width>INT_MAX || view.height>INT_MAX ||
            (size_t)view.width>(size_t)-1/4/(size_t)view.height ||
            view.size<(size_t)view.width*(size_t)view.height*4) return false;
        api->draw_sprite(api->context,art->image,0,0,api->screen_width,api->screen_height,
                         0,0,view.width,view.height,255,255,255,255,false);
        return true;
    }
    if (!art->pixels) return false;
    for (int row=0;row<art->height;row++) {
        int top=row*api->screen_height/art->height;
        int bottom=(row+1)*api->screen_height/art->height;
        if (top==bottom) continue;
        for (int col=0;col<art->width;) {
            const unsigned char *pixel=art->pixels+((size_t)row*art->width+col)*3;
            int end=col+1;
            while (end<art->width && !memcmp(pixel,art->pixels+((size_t)row*art->width+end)*3,3)) end++;
            int x=col*api->screen_width/art->width, right=end*api->screen_width/art->width;
            if (right>x) api->fill_rect(api->context,x,api->screen_height-bottom,right-x,bottom-top,pixel[0],pixel[1],pixel[2]);
            col=end;
        }
    }
    return true;
}
#endif
