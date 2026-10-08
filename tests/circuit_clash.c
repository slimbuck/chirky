#include <assert.h>
#include "../games/circuit-clash/game.c"
#include "local_input.h"

static unsigned rectangles,texts;
static void fill(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)r;(void)g;(void)b;assert(w>=0 && h>=0);rectangles++;}
static void draw(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)y;(void)r;(void)g;(void)b;assert(x>=0 && x+(int)chirky_text_length(s)*6*scale<=host->screen_width);texts++;}
static struct chirky_input frame;
static struct local_input_bank bank;
static void tick(unsigned a,unsigned b,unsigned c)
{
    frame.device_count=0;
    const unsigned masks[]={a,b,c};
    for(unsigned i=0;i<3;i++)if(masks[i]!=256) {
        struct chirky_device_input *d=local_input_add(&bank,&frame,i+1,i?CHIRKY_DEVICE_CONTROLLER:CHIRKY_DEVICE_KEYBOARD,masks[i],0);
        for(int k=0;k<CHIRKY_BUTTON_COUNT;k++)snprintf(d->labels[k],sizeof(d->labels[k]),"%s",i?"B":"X");
    }
    local_input_finish(&bank,&frame);update(&frame);render();
}
int main(void)
{
    struct chirky_host_api api={.abi_version=CHIRKY_ABI_VERSION,.screen_width=288,.screen_height=216,.fill_rect=fill,.draw_text=draw};
    assert(init(&api,"unused"));
    tick(16,0,0);assert(!players[0]); /* Held launch press cannot join. */
    tick(0,0,0);tick(64,64,0);assert(!players[0] && !players[1]);
    tick(0,0,0);tick(16,0,0);assert(players[0]==1 && !players[1]);
    tick(0,0,0);tick(16,0,0);assert(!players[1]); /* One device cannot claim both. */
    tick(0,16,0);assert(players[1]==2 && !waiting && release_gate);
    tick(0,0,0);for(int i=0;i<90;i++)tick(0,0,0);
    assert(!countdown && !release_gate);
    int ax=fighters[0].x,bx=fighters[1].x;
    tick(2,0,1);assert(fighters[0].x>ax && fighters[1].x==bx); /* Unassigned pad ignored. */
    tick(0,1,0);assert(fighters[1].x<bx);
    tick(32,0,0);assert(fighters[0].y>0 && fighters[1].y==0);
    for(int i=0;i<60;i++)tick(0,0,0);
    /* Move into reach and prove one held punch causes only one hit. */
    while(fighters[1].x-fighters[0].x>24*8)tick(2,1,0);
    tick(16,0,0);for(int i=0;i<60;i++)tick(16,0,0);
    assert(fighters[1].health==80 && fighters[0].health==100);
    tick(0,256,0);assert(waiting);ax=fighters[0].x;
    tick(2,256,0);assert(fighters[0].x==ax && fighters[1].health==80);
    tick(0,256,16);assert(players[1]==3 && !waiting && release_gate);
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    for(int hit=0;hit<4;hit++) {
        while(fighters[1].x-fighters[0].x>24*8)tick(2,256,0);
        tick(16,256,0);for(int i=0;i<30;i++)tick(0,256,0);
    }
    assert(winner==0 && fighters[1].health==0);
    tick(16,256,0);assert(fighters[0].ready && winner==0);
    tick(0,256,16);assert(winner==-1 && fighters[0].health==100 && fighters[1].health==100);
    /* Equal active-frame attacks resolve together, including the final hit. */
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    fighters[0].x=100*8;fighters[1].x=124*8;
    fighters[0].health=fighters[1].health=20;
    tick(16,256,16);for(int i=0;i<6;i++)tick(0,256,0);
    assert(winner==2 && fighters[0].health==0 && fighters[1].health==0);
    shutdown();assert(init(&api,"unused") && !players[0] && !players[1]);
    api.screen_width=256;api.screen_height=192;tick(0,0,0);tick(16,16,0);tick(0,0,0);
    assert(rectangles && texts);
    puts("Circuit Clash: join gates, exclusive devices, movement, jump, combat, disconnect/rejoin, rematch, reload and layout passed.");
}
