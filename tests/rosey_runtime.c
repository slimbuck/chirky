/* Reuse the real host font for software screenshots; unused DRM code is stripped. */
#define main unused_host_main
#include "../src/host.c"
#undef main
#include "../games/rosey-chop/game_state.h"
#include <assert.h>

const struct two_forty_game_api *two_forty_game_entry(void);
static unsigned char pixels[240][320][3];
static int draws, sounds;
static struct two_forty_host_api test_host;
static const struct two_forty_game_api *game;
static struct two_forty_input controls;

static void rectangle(void *ctx, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    (void)ctx; draws++;
    assert(w >= 0 && h >= 0);
    for (int yy=y; yy<y+h; yy++) for (int xx=x; xx<x+w; xx++)
        if (xx>=0 && xx<test_host.screen_width && yy>=0 && yy<test_host.screen_height) {
            pixels[yy][xx][0]=r; pixels[yy][xx][1]=g; pixels[yy][xx][2]=b;
        }
}
static void lettering(void *ctx, int x, int y, const char *s, int scale, unsigned char r, unsigned char g, unsigned char b)
{
    assert(x >= 0 && x+(int)strlen(s)*6*scale-scale <= test_host.screen_width);
    assert(y-6*scale >= 0 && y+scale <= test_host.screen_height);
    for (; *s; s++, x+=6*scale) {
        const uint8_t *rows=glyph(*s);
        for (int row=0; row<7; row++) for (int col=0; col<5; col++)
            if (rows[row] & (1u<<(4-col))) rectangle(ctx,x+col*scale,y-row*scale,scale,scale,r,g,b);
    }
}
static void effect(void *ctx, const char *device, const char *path)
{
    (void)ctx; (void)device;
    FILE *f=fopen(path,"rb"); assert(f);
    char magic[4]; assert(fread(magic,1,4,f)==4 && !memcmp(magic,"RIFF",4)); fclose(f); sounds++;
}
static void label(void *ctx, enum two_forty_button action, char *s, size_t n)
{ (void)ctx; snprintf(s,n,"%s",action==TWO_FORTY_BUTTON_Y ? "Y" : "B"); }
static void preview(const char *name)
{
    draws=0; game->render(); assert(draws>0 && draws<5000);
    char path[256]; snprintf(path,sizeof(path),"build/rosey-%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n%d %d\n255\n",test_host.screen_width,test_host.screen_height);
    for (int y=test_host.screen_height-1; y>=0; y--) fwrite(pixels[y],3,(size_t)test_host.screen_width,f);
    fclose(f);
}
static void begin(void)
{
    memset(&controls,0,sizeof(controls));
    game->update(&controls);game->update(&controls);
    garden.result_age=41; controls.button_pressed[TWO_FORTY_BUTTON_B]=true;
    game->update(&controls); controls.button_pressed[TWO_FORTY_BUTTON_B]=false;
    assert(garden.phase==PLAY && garden.elapsed==0 && garden.remaining==18);
    game->update(&controls);game->update(&controls);
}
int main(void)
{
    test_host=(struct two_forty_host_api){.abi_version=TWO_FORTY_ABI_VERSION,
        .screen_width=288,.screen_height=216,.fill_rect=rectangle,
        .play_sound=effect,.draw_text=lettering,.button_label=label};
    game=two_forty_game_entry(); assert(game->abi_version==TWO_FORTY_ABI_VERSION);
    assert(game->init(&test_host,"games/rosey-chop/game.conf"));
    assert(garden.total==18 && garden.rose_count==96 && garden.phase==TITLE);
    for (int i=0;i<3600;i++) game->update(&controls);
    assert(garden.phase==TITLE && !garden.elapsed); preview("title");
    controls.button_pressed[TWO_FORTY_BUTTON_START]=true;game->update(&controls);
    controls.button_pressed[TWO_FORTY_BUTTON_START]=false;assert(garden.phase==PLAY);
    game->update(&controls);game->update(&controls);preview("garden");
    controls.buttons[TWO_FORTY_BUTTON_LEFT]=true;
    for (int i=0;i<150;i++) game->update(&controls);
    assert(garden.x==10); controls.buttons[TWO_FORTY_BUTTON_LEFT]=false;
    garden.phase=STORM; begin();
    /* A full route uses real directional input, held chop and timed jump, with wasps enabled. */
    int route_ticks=0;
    while (garden.phase==PLAY && route_ticks++<garden.storm_ticks) {
        const struct rose *nearest=NULL; float best=1e9f;
        for (int i=0;i<garden.rose_count;i++) {
            struct rose *r=&garden.roses[i];
            float dx=r->x-garden.x,dy=r->y-garden.y,d=dx*dx+dy*dy;
            if (r->kind=='d' && !r->cut && d<best) { nearest=r; best=d; }
        }
        assert(nearest);
        controls.buttons[TWO_FORTY_BUTTON_LEFT]=nearest->x<garden.x-2;
        controls.buttons[TWO_FORTY_BUTTON_RIGHT]=nearest->x>garden.x+2;
        controls.buttons[TWO_FORTY_BUTTON_UP]=nearest->y<garden.y-2;
        controls.buttons[TWO_FORTY_BUTTON_DOWN]=nearest->y>garden.y+2;
        controls.buttons[TWO_FORTY_BUTTON_B]=true;
        float dx=garden.wasp_x-garden.x,dy=garden.wasp_y-garden.y;
        controls.button_pressed[TWO_FORTY_BUTTON_Y]=garden.wasp_life && dx*dx+dy*dy<40*40;
        game->update(&controls);
    }
    assert(garden.phase==WON && garden.remaining==0);
    for (int i=0;i<garden.rose_count;i++) assert(garden.roses[i].cut==(garden.roses[i].kind=='d'));
    printf("Rosey Chop: complete input-driven route in %.2f seconds.\n",garden.elapsed/60.f);
    preview("win"); int elapsed=garden.elapsed;
    for (int i=0;i<40;i++) game->update(&controls);
    assert(garden.elapsed==elapsed); begin();
    for (int i=0;i<garden.rose_count;i++) assert(!garden.roses[i].cut);
    assert(!garden.wasp_life && !garden.warning && !garden.z && !garden.chop);
    garden.elapsed=garden.next_wasp-1; game->update(&controls); assert(garden.warning==89 && !garden.wasp_life);
    for (int i=0;i<89;i++) game->update(&controls);
    assert(!garden.warning && garden.wasp_life==garden.wasp_duration);
    garden.wasp_life=1; garden.wasp_x=10; garden.wasp_y=10;
    game->update(&controls); assert(!garden.wasp_life && garden.next_wasp==garden.elapsed+garden.wasp_interval);
    garden.wasp_x=garden.x; garden.wasp_y=garden.y; garden.wasp_life=60;
    garden.z=12; garden.vz=0; game->update(&controls); assert(garden.phase==PLAY);
    preview("wasp");
    garden.z=garden.vz=0; garden.wasp_x=garden.x; garden.wasp_y=garden.y;
    game->update(&controls); assert(garden.phase==STUNG); preview("sting"); begin();
    controls.button_pressed[TWO_FORTY_BUTTON_Y]=true; game->update(&controls);
    controls.button_pressed[TWO_FORTY_BUTTON_Y]=false; assert(garden.z>0);
    for (int i=0;i<35;i++) game->update(&controls);
    assert(garden.z==0 && garden.vz==0);
    garden.elapsed=garden.storm_ticks-1; game->update(&controls);
    assert(garden.phase==STORM); preview("storm"); begin();
    /* All phases fit the supported CRT-safe areas, including custom labels. */
    const int sizes[][2]={{320,240},{288,216},{256,192}};
    for (int i=0;i<3;i++) {
        test_host.screen_width=sizes[i][0]; test_host.screen_height=sizes[i][1];
        for (int phase=TITLE;phase<=STORM;phase++) { garden.phase=phase; game->render(); }
    }
    game->shutdown(); game->shutdown();
    assert(!game->init(&test_host,"/tmp/rosey-missing-config"));
    assert(game->init(&test_host,"games/rosey-chop/game.conf"));
    memset(&controls,0,sizeof(controls));
    controls.buttons[TWO_FORTY_BUTTON_B]=controls.button_pressed[TWO_FORTY_BUTTON_B]=true;
    game->update(&controls);assert(garden.phase==PLAY);
    controls.button_pressed[TWO_FORTY_BUTTON_B]=false;
    for(int i=0;i<10;i++)game->update(&controls);
    assert(garden.chop==0); /* Beginning a run must not also chop. */
    controls.buttons[TWO_FORTY_BUTTON_Y]=controls.button_pressed[TWO_FORTY_BUTTON_Y]=true;
    game->update(&controls);assert(garden.z==0 && garden.chop==0);
    memset(&controls,0,sizeof(controls));game->update(&controls);game->update(&controls);
    controls.buttons[TWO_FORTY_BUTTON_B]=controls.button_pressed[TWO_FORTY_BUTTON_B]=true;
    game->update(&controls);assert(garden.chop>0);
    game->shutdown();
    assert(sounds>0);
    puts("Rosey Chop: controls, healthy roses, win/replay, warning, sting, jump, storm and CRT layouts passed.");
}
