#ifndef CHIRKY_DRAWING_H
#define CHIRKY_DRAWING_H
#include "chirky.h"
#include "pixel_font.h"

static inline bool chirky_clip_rect(const struct chirky_host_api *api,int *x,int *y,int *w,int *h)
{
    if(*w<=0 || *h<=0)return false;
    if(*x<0){*w+=*x;*x=0;}if(*y<0){*h+=*y;*y=0;}
    if((int64_t)*x+*w>api->screen_width)*w=api->screen_width-*x;
    if((int64_t)*y+*h>api->screen_height)*h=api->screen_height-*y;
    return *w>0 && *h>0;
}

static inline void chirky_draw_text_pixels(const struct chirky_host_api *api,int x,int y,
    const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    for(;*value;x+=6*scale) {
        const uint8_t *rows=glyph(chirky_text_next(&value));
        for(int row=0;row<7;row++)for(int col=0;col<5;col++)
            if(rows[row]&(1<<(4-col)))api->fill_rect(api->context,x+col*scale,y-row*scale,scale,scale,r,g,b);
    }
}
static inline void chirky_draw_text(const struct chirky_host_api *api,int x,int y,
    const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    if(api->draw_text)api->draw_text(api->context,x,y,value,scale,r,g,b);
    else chirky_draw_text_pixels(api,x,y,value,scale,r,g,b);
}
#endif
