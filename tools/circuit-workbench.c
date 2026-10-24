/* Dashboard-only simulation adapter. No duplicate movement or animation logic. */
#include "../games/circuit-clash/game.c"
#include "viewport.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WB_EXPORT
#endif

static int wb_previous,wb_facing=1;
static int wb_sample[34];
static void wb_rect(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;}
static void wb_text(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)s;(void)scale;(void)r;(void)g;(void)b;}
static void wb_sprite(void *ctx,chirky_asset a,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                      unsigned char r,unsigned char g,unsigned char b,unsigned char alpha,bool flip)
{
    (void)ctx;(void)a;(void)r;(void)g;(void)b;(void)alpha;
    int draw[]={x,y,w,h,sx,sy,sw,sh,flip};
    memcpy(wb_sample+14,draw,sizeof(draw));
}
static struct chirky_host_api wb_host={.abi_version=CHIRKY_ABI_VERSION,
    .screen_width=CHIRKY_VIEWPORT_WIDTH,.screen_height=CHIRKY_VIEWPORT_HEIGHT,
    .fill_rect=wb_rect,.draw_text=wb_text,.draw_sprite=wb_sprite};

WB_EXPORT void wb_reset(int facing)
{
    wb_facing=facing<0?-1:1;wb_previous=0;reset_round();
    memset(controls,0,sizeof(controls));countdown=0;release_gate=false;waiting=false;started=true;
    fighters[0].x=(wb_facing>0?72:host->screen_width-72)*8;fighters[0].facing=wb_facing;
    /* Isolate the subject: retain normal walls and facing, without an opponent collision. */
    fighters[1].x=(wb_facing>0?host->screen_width:0)*8;fighters[1].y=1000000;
}
WB_EXPORT int wb_load(void)
{
    struct character_tuning next;struct stage_tuning arena;
    if(!read_character(&wb_host,"/character.conf",&next) || !read_stage(&wb_host,"/stage.conf",&arena))return 0;
    host=&wb_host;characters[0]=characters[1]=next;stage=arena;artwork[0]=1;artwork[1]=0;
    wb_reset(wb_facing);return 1;
}
WB_EXPORT void wb_step(int mask)
{
    for(int b=0;b<CHIRKY_BUTTON_COUNT;b++) {
        controls[0].buttons[b]=(mask&(1<<b))!=0;
        controls[0].button_pressed[b]=(mask&(1<<b)) && !(wb_previous&(1<<b));
    }
    wb_previous=mask;fighters[1].y=1000000;simulate();
}
WB_EXPORT const int *wb_state(void)
{
    struct fighter *f=&fighters[0];struct attack_tuning a=move_for(0);
    int phase=!f->attack?0:f->action_tick<a.startup?1:active_attack(0)?2:
        f->action_tick<a.startup+a.active+a.followthrough?3:4;
    int state[]={f->x,f->y,f->vx,f->vy,fighter_frame(0),f->facing,f->attack,f->action_tick,
        active_attack(0),phase,f->walk_direction,body_height(0),characters[0].body_half_width_px,stage.floor_y_px};
    memcpy(wb_sample,state,sizeof(state));draw_fighter(0);
    wb_sample[23]=a.reach;wb_sample[24]=a.bottom;wb_sample[25]=a.top;
    wb_sample[26]=CHIRKY_SAFE_X;wb_sample[27]=CHIRKY_SAFE_Y;
    wb_sample[28]=CHIRKY_FRAMEBUFFER_WIDTH;wb_sample[29]=CHIRKY_FRAMEBUFFER_HEIGHT;
    wb_sample[30]=host->screen_width;wb_sample[31]=host->screen_height;
    wb_sample[32]=f->motion;wb_sample[33]=f->motion_tick;
    return wb_sample;
}
