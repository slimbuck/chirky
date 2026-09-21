#ifndef CHIRKY_LAUNCHER_WORDMARK_H
#define CHIRKY_LAUNCHER_WORDMARK_H
#include "chirky.h"
#include <stdint.h>

/* Custom 7-by-9-pixel lettering, drawn at integer scale. The order spells
   CHIRKY BOX; two-pixel stems keep the maker's badge readable on a CRT. */
static inline void launcher_wordmark(const struct chirky_host_api *api)
{
    static const uint8_t letters[9][9]={
        {0x3f,0x7f,0x60,0x60,0x60,0x60,0x60,0x7f,0x3f}, /* C */
        {0x63,0x63,0x63,0x7f,0x7f,0x63,0x63,0x63,0x63}, /* H */
        {0x7f,0x7f,0x1c,0x1c,0x1c,0x1c,0x1c,0x7f,0x7f}, /* I */
        {0x7e,0x7f,0x63,0x63,0x7e,0x7c,0x6c,0x66,0x63}, /* R */
        {0x63,0x67,0x6e,0x7c,0x78,0x7c,0x6e,0x67,0x63}, /* K */
        {0x63,0x63,0x63,0x36,0x3e,0x1c,0x1c,0x1c,0x1c}, /* Y */
        {0x7e,0x7f,0x63,0x63,0x7e,0x7f,0x63,0x63,0x7e}, /* B */
        {0x3e,0x7f,0x63,0x63,0x63,0x63,0x63,0x7f,0x3e}, /* O */
        {0x63,0x63,0x36,0x3e,0x1c,0x3e,0x36,0x63,0x63}, /* X */
    };
    int scale=api->screen_width>=248?3:2;
    int left=10, top=api->screen_height-24;
    int badge_x=left+50*scale, badge_y=top-11*scale;
    int badge_w=27*scale, badge_h=13*scale;
    api->fill_rect(api->context,badge_x+1,badge_y-1,badge_w,badge_h,8,42,44);
    api->fill_rect(api->context,badge_x,badge_y,badge_w,badge_h,244,184,69);
    api->fill_rect(api->context,badge_x+2,badge_y+2,badge_w-4,badge_h-4,5,17,23);
    for(int pass=0;pass<2;pass++)for(int letter=0;letter<9;letter++) {
        int x=letter<6?left+letter*8*scale:badge_x+2*scale+(letter-6)*8*scale;
        int offset=pass?0:1;
        unsigned char r=pass?244:8;
        unsigned char g=pass?(letter<6?233:184):42;
        unsigned char b=pass?(letter<6?202:69):44;
        for(int row=0;row<9;row++)for(int col=0;col<7;) {
            if(!(letters[letter][row]&(1u<<(6-col)))) {col++;continue;}
            int end=col+1;
            while(end<7 && (letters[letter][row]&(1u<<(6-end))))end++;
            api->fill_rect(api->context,x+col*scale+offset,top-(row+1)*scale-offset,
                           (end-col)*scale,scale,r,g,b);
            col=end;
        }
    }
}
#endif
