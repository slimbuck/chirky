#include "splash_art.h"
#include <assert.h>
#include <stdio.h>

struct fixture {
    unsigned requests,releases,draws;
    enum chirky_asset_state state;
    bool reject;
    char path[1024];
    struct chirky_asset_view view;
};
static chirky_asset request(void *ctx,const char *path,enum chirky_asset_type type)
{
    struct fixture *f=ctx;assert(type==CHIRKY_ASSET_IMAGE);
    f->requests++;snprintf(f->path,sizeof(f->path),"%s",path);return f->reject?0:f->requests;
}
static enum chirky_asset_state status(void *ctx,chirky_asset h)
{ struct fixture *f=ctx;assert(h==f->requests);return f->state; }
static struct chirky_asset_view data(void *ctx,chirky_asset h)
{ struct fixture *f=ctx;assert(h==f->requests && f->state==CHIRKY_ASSET_READY);return f->view; }
static void release(void *ctx,chirky_asset h)
{ struct fixture *f=ctx;assert(h==f->requests);f->releases++; }
static void draw(void *ctx,chirky_asset h,int x,int y,int w,int height,int sx,int sy,int sw,int sh,
                 unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    struct fixture *f=ctx;assert(h==f->requests && f->state==CHIRKY_ASSET_READY);
    assert(!x && !y && w==256 && height==192 && !sx && !sy && sw==2 && sh==1);
    assert(r==255 && g==255 && b==255 && a==255 && !flip);f->draws++;
}
static void no_rect(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{ (void)ctx;(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;assert(0); }

int main(void)
{
    const unsigned char pixels[]={255,0,0,255,0,255,0,255};
    struct fixture f={.state=CHIRKY_ASSET_LOADING,.view={.data=pixels,.size=sizeof(pixels),.width=2,.height=1}};
    struct chirky_host_api api={.abi_version=CHIRKY_ABI_VERSION,.context=&f,.screen_width=256,.screen_height=192,
        .fill_rect=no_rect,.asset_request=request,.asset_status=status,.asset_data=data,.asset_release=release,.draw_sprite=draw};
    struct splash_art art={0};
    /* A real PPM exists here: pending/failed modern requests must not read it. */
    const char *config="games/phosphor-run/game.conf";
    assert(splash_load_api(&art,&api,config));
    assert(!strcmp(f.path,"games/phosphor-run/assets/artwork/splash.ppm"));
    assert(art.image==1 && !art.pixels && !splash_draw(&art,&api));
    assert(!f.draws && !f.releases);
    f.state=CHIRKY_ASSET_FAILED;assert(!splash_draw(&art,&api) && !art.pixels);
    f.state=CHIRKY_ASSET_READY;assert(splash_draw(&art,&api) && f.draws==1 && !f.releases);
    assert(splash_draw(&art,&api) && f.draws==2 && art.image==1);
    f.view.size=1;assert(!splash_draw(&art,&api));f.view.size=sizeof(pixels);
    f.view.width=UINT_MAX;assert(!splash_draw(&art,&api));f.view.width=2;
    struct chirky_host_api other=api;other.context=NULL;assert(!splash_draw(&art,&other));
    assert(splash_load_file_api(&art,&api,"not-on-disk.png"));assert(f.releases==1 && art.image==2);
    splash_free(&art);splash_free(&art);assert(f.releases==2 && !art.image);
    f.reject=true;assert(!splash_load_api(&art,&api,config));
    assert(art.asset_requested && !art.image && !art.pixels && !splash_draw(&art,&api));
    splash_free(&art);assert(f.releases==2);f.reject=false;
    /* Every missing optional callback permits the legacy path. */
    for (int missing=0;missing<5;missing++) {
        other=api;
        switch (missing) {
        case 0: other.asset_request=NULL;break;
        case 1: other.asset_status=NULL;break;
        case 2: other.asset_data=NULL;break;
        case 3: other.asset_release=NULL;break;
        case 4: other.draw_sprite=NULL;break;
        }
        assert(splash_load_api(&art,&other,config) && art.pixels && !art.image);
        splash_free(&art);
    }
    assert(splash_load_api(&art,NULL,config) && art.pixels);splash_free(&art);
    assert(splash_load_file_api(&art,&api,"last.png"));
    assert(!splash_load_api(&art,&api,NULL) && f.releases==3 && !art.image);
    puts("Texture splash: async status, single sprite, retained/replaced/released handles and legacy fallback passed.");
    return 0;
}
