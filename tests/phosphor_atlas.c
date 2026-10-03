/* Same GLES/WebGL pixel oracle as the renderer suite, with the live game. */
static void atlas_tests(void);
#define CHIRKY_TEST_EXTRA atlas_tests
#include "texture_renderer.c"
#include "../games/phosphor-run/game_state.h"
#include <time.h>
extern const struct chirky_game_api *chirky_game_entry(void);

static struct chirky_host_api atlas_api;
static struct rect_renderer_texture atlas_images[16];
static unsigned char *atlas_pixels[16];
static unsigned uploads;
static void game_rect(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)ctx;if(chirky_clip_rect(&atlas_api,&x,&y,&w,&h))rect_renderer_rect(&renderer,x+13,y+11,w,h,r,g,b);
}
static void game_text(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{ (void)ctx;chirky_draw_text_pixels(&atlas_api,x,y,s,scale,r,g,b); }
static void atlas_text(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{ (void)ctx;assert(rect_renderer_text(&renderer,x+13,y+11,s,scale,r,g,b,false,13,11,288,216)); }
static void atlas_outline(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{ (void)ctx;assert(rect_renderer_text(&renderer,x+13,y+11,s,scale,r,g,b,true,13,11,288,216)); }
static bool game_mesh(void *ctx,const struct chirky_mesh_vertex *v,size_t n,float ambient)
{ (void)ctx;return rect_renderer_mesh(&renderer,v,n,ambient,13,11,288,216); }
static chirky_asset atlas_create(void *ctx,unsigned w,unsigned h,const void *rgba,size_t size)
{
    (void)ctx;assert(w==512 && h==512 && size==512*512*4);
    for(unsigned i=0;i<16;i++)if(!atlas_images[i].id) {
        assert(rect_renderer_texture_create(&renderer,&atlas_images[i],w,h,rgba,size));
        atlas_pixels[i]=malloc(size);assert(atlas_pixels[i]);memcpy(atlas_pixels[i],rgba,size);uploads++;return i+1;
    }
    assert(0);return 0;
}
static void atlas_release(void *ctx,chirky_asset image)
{ (void)ctx;if(!image)return;assert(image<=16);rect_renderer_texture_delete(&renderer,&atlas_images[image-1]);free(atlas_pixels[image-1]);atlas_pixels[image-1]=NULL; }
static void atlas_sprite(void *ctx,chirky_asset image,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                         unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)ctx;assert(image && image<=16);
    rect_renderer_sprite_clipped(&renderer,&atlas_images[image-1],x+13,y+11,w,h,sx,sy,sw,sh,r,g,b,a,flip,13,11,288,216);
}
static void quiet(void *ctx,const char *device,const char *path){(void)ctx;(void)device;(void)path;}
static void atlas_projected(void *ctx,chirky_asset image,float x,float y,float scale,int sx,int sy,int sw,int sh,
    unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)ctx;assert(image && image<=16);
    rect_renderer_sprite_projected(&renderer,&atlas_images[image-1],x,y,scale,sx,sy,sw,sh,r,g,b,a,flip,13,11,288,216);
}
static void atlas_structure(void)
{
    const char *keys="nscawrg";
    const struct colour colours[]={settings.deep,settings.platform,settings.edge,settings.amber,settings.paper,settings.hazard,settings.phosphor};
    for(int i=0;i<content.sprite_count;i++)for(int f=0;f<content.sprites[i].animation.count;f++) {
        const struct grid *g=&content.sprites[i].animation.frames[f];
        assert(g->atlas_page<16 && atlas_pixels[g->atlas_page]);
        for(int mask=0;mask<2;mask++)for(int y=-1;y<=g->height;y++)for(int x=-1;x<=g->width;x++) {
            const unsigned char *p=atlas_pixels[g->atlas_page]+((g->atlas_y+y)*512+g->atlas_x+x+mask*(g->width+2))*4;
            if(x<0 || y<0 || x==g->width || y==g->height || g->pixels[y*g->width+x]=='.')assert(!p[3]);
            else {
                assert(p[3]==255);
                if(mask)assert(p[0]==255 && p[1]==255 && p[2]==255);
                else {const char *key=strchr(keys,g->pixels[y*g->width+x]);assert(key);
                    struct colour c=colours[key-keys];assert(p[0]==c.r && p[1]==c.g && p[2]==c.b);}
            }
        }
    }
}
#ifndef __EMSCRIPTEN__
static void submission_benchmark(const struct chirky_game_api *game,const char *name)
{
    clock_t start=clock();
    for(int i=0;i<120;i++){rect_renderer_begin(&renderer);game->render();rect_renderer_flush(&renderer);}
    printf("%s render submission: %.3f ms process CPU/frame\n",name,1000.*(clock()-start)/CLOCKS_PER_SEC/120);
}
#endif
static void atlas_tests(void)
{
    assert(rect_renderer_init(&renderer,W,H));
    atlas_api=(struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,.screen_width=288,.screen_height=216,
        .fill_rect=game_rect,.draw_text=game_text,.draw_mesh=game_mesh,.play_sound=quiet,
        .draw_sprite=atlas_sprite,.draw_sprite_projected=atlas_projected,.asset_release=atlas_release};
    const struct chirky_game_api *game=chirky_game_entry();
    assert(game->init(&atlas_api,"games/phosphor-run/game.conf"));
    for(int scene=0;scene<48;scene++) {
        scenery_atlas_free();atlas_api.image_create=NULL;atlas_api.draw_text=game_text;atlas_api.draw_text_outlined=NULL;
        phase=scene<44?PHASE_LEVEL_INTRO:PHASE_PLAY;
        level_intro_timer=scene<4?130:44-scene;
        frame_number=scene*43;facing=scene%2?-1:1;
        if(scene>=44){camera_x=(scene-44)*15.f;camera_y=(scene-44)*9.f;}
#ifndef __EMSCRIPTEN__
        if(scene==44)submission_benchmark(game,"Original rectangles");
#endif
        begin();game->render();rect_renderer_flush(&renderer);
        unsigned before=renderer.rectangles;
        glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,expected);
        atlas_api.image_create=atlas_create;atlas_api.draw_text=atlas_text;atlas_api.draw_text_outlined=atlas_outline;
        assert(scenery_atlas_load());if(scene==0)atlas_structure();
#ifndef __EMSCRIPTEN__
        if(scene==44)submission_benchmark(game,"Texture atlases");
#endif
        glClear(GL_COLOR_BUFFER_BIT);rect_renderer_begin(&renderer);game->render();
        printf("Scene %d: %u rectangles -> %u rectangles + %u sprite quads\n",scene,before,renderer.rectangles,renderer.sprites);
        compare("Phosphor scenery/font atlases vs original rectangles",0);
    }
    unsigned count=uploads;
    for(int i=0;i<120;i++){begin();game->render();rect_renderer_flush(&renderer);}
    assert(uploads==count); /* Static frames never upload another image. */
    game->shutdown();for(unsigned i=0;i<16;i++)assert(!atlas_images[i].id);
    rect_renderer_destroy(&renderer);puts("PHOSPHOR_ATLAS_TESTS_PASSED");
}
