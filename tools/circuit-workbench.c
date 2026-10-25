/* Dashboard-only simulation adapter. No duplicate movement or animation logic. */
#include "../games/circuit-clash/game.c"
#include "viewport.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WB_EXPORT
#endif

enum { WB_FIGHTER_SIZE=40, WB_SAMPLE_SIZE=89 };
static int wb_previous,wb_facing=1,wb_mode,wb_gap=78,wb_drawing;
static int wb_sample[WB_SAMPLE_SIZE];
static void wb_rect(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;}
static void wb_text(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)s;(void)scale;(void)r;(void)g;(void)b;}
static void wb_sprite(void *ctx,chirky_asset a,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                      unsigned char r,unsigned char g,unsigned char b,unsigned char alpha,bool flip)
{
    (void)ctx;(void)a;(void)r;(void)g;(void)b;(void)alpha;
    int draw[]={x,y,w,h,sx,sy,sw,sh,flip};
    memcpy(wb_sample+wb_drawing*WB_FIGHTER_SIZE+14,draw,sizeof(draw));
}
static struct chirky_host_api wb_host={.abi_version=CHIRKY_ABI_VERSION,
    .screen_width=CHIRKY_VIEWPORT_WIDTH,.screen_height=CHIRKY_VIEWPORT_HEIGHT,
    .fill_rect=wb_rect,.draw_text=wb_text,.draw_sprite=wb_sprite};

WB_EXPORT void wb_reset(int facing)
{
    wb_facing=facing<0?-1:1;wb_previous=0;reset_round();
    memset(controls,0,sizeof(controls));countdown=0;release_gate=false;waiting=false;started=true;
    fighters[0].x=(wb_facing>0?72:host->screen_width-72)*8;fighters[0].facing=wb_facing;
    if(wb_mode) {
        fighters[1].x=fighters[0].x+wb_facing*wb_gap*8;clamp_fighter(&fighters[1]);
        fighters[1].facing=-wb_facing;
    } else {
        /* Solo retains normal walls and facing without opponent collision. */
        fighters[1].x=(wb_facing>0?host->screen_width:0)*8;fighters[1].y=1000000;
    }
}
WB_EXPORT void wb_configure(int mode,int gap)
{wb_mode=mode>=0 && mode<=2?mode:1;wb_gap=gap<40?40:gap>160?160:gap;}
WB_EXPORT int wb_load(void)
{
    struct character_tuning next;struct stage_tuning arena;
    if(!read_character(&wb_host,"/character.conf",&next) || !read_stage(&wb_host,"/stage.conf",&arena))return 0;
    host=&wb_host;characters[0]=characters[1]=next;stage=arena;artwork[0]=artwork[1]=1;
    wb_reset(wb_facing);return 1;
}
WB_EXPORT void wb_step(int mask)
{
    for(int b=0;b<CHIRKY_BUTTON_COUNT;b++) {
        controls[0].buttons[b]=(mask&(1<<b))!=0;
        controls[0].button_pressed[b]=(mask&(1<<b)) && !(wb_previous&(1<<b));
    }
    wb_previous=mask;memset(&controls[1],0,sizeof(controls[1]));
    if(wb_mode==2)controls[1].buttons[CHIRKY_BUTTON_PRIMARY]=controls[1].buttons[CHIRKY_BUTTON_SECONDARY]=true;
    if(!wb_mode)fighters[1].y=1000000;
    if(winner==-1 || hitstop)simulate();
    else {frozen_frame=false;contact_mask=0;memset(sound_events,0,sizeof(sound_events));}
}
WB_EXPORT const int *wb_state(void)
{
    for(int p=0;p<2;p++) {
    int *sample=wb_sample+p*WB_FIGHTER_SIZE;
    struct fighter *f=&fighters[p];struct attack_tuning a=move_for(p);
    int phase=!f->attack?0:f->action_tick<a.startup?1:active_attack(p)?2:
        f->action_tick<a.startup+a.active+a.followthrough?3:4;
    int state[]={f->x,f->y,f->vx,f->vy,fighter_frame(p),f->facing,f->attack,f->action_tick,
        active_attack(p),phase,f->walk_direction,body_height(p),characters[p].body_half_width_px,stage.floor_y_px};
    memcpy(sample,state,sizeof(state));wb_drawing=p;draw_fighter(p);
    sample[23]=a.reach;sample[24]=a.bottom;sample[25]=a.top;
    sample[26]=CHIRKY_SAFE_X;sample[27]=CHIRKY_SAFE_Y;
    sample[28]=CHIRKY_FRAMEBUFFER_WIDTH;sample[29]=CHIRKY_FRAMEBUFFER_HEIGHT;
    sample[30]=host->screen_width;sample[31]=host->screen_height;
    sample[32]=f->motion;sample[33]=f->motion_tick;
    sample[34]=f->health;sample[35]=f->stun;sample[36]=f->blocking || (f->stun && f->guard_stun);
    sample[37]=f->recoil_tick;sample[38]=f->recoil_ticks;sample[39]=f->reaction_ticks;
    }
    wb_sample[80]=hitstop;wb_sample[81]=frozen_frame;wb_sample[82]=contact_mask;
    wb_sample[83]=wb_mode;wb_sample[84]=round_ticks;
    for(int p=0;p<2;p++){wb_sample[85+p*2]=sound_events[p].cue;wb_sample[86+p*2]=sound_events[p].volume;}
    return wb_sample;
}
