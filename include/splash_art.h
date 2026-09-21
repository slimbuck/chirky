#ifndef CHIRKY_SPLASH_ART_H
#define CHIRKY_SPLASH_ART_H
#include "chirky.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounded, native-resolution P6 artwork. Missing art leaves the text title usable.
   Colour runs use the host's batched rectangle renderer; no new graphics ABI. */
struct splash_art { unsigned char *pixels; int width, height; };
static inline void splash_free(struct splash_art *art)
{ free(art->pixels); memset(art,0,sizeof(*art)); }
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
static inline bool splash_load(struct splash_art *art, const char *config)
{
    if (!config) { splash_free(art); return false; }
    char path[1024];
    const char *slash=strrchr(config,'/');
    int length=slash?(int)(slash-config):1;
    if (snprintf(path,sizeof(path),"%.*s/assets/artwork/splash.ppm",length,slash?config:".") >= (int)sizeof(path)) { splash_free(art); return false; }
    return splash_load_file(art,path);
}
static inline bool splash_draw(const struct splash_art *art, const struct chirky_host_api *api)
{
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
