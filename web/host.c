#include "chirky.h"
#include "pixel_font.h"
#include "splash_art.h"
#include "launcher_wordmark.h"
#include "rect_renderer.h"
#include "asset_store.h"
#include "image_cache.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>

static struct rect_renderer renderer;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
static struct chirky_host_api api;
static struct chirky_input input;
static unsigned previous;
static struct asset_store *assets;
static struct image_cache images;
EMSCRIPTEN_KEEPALIVE void web_destroy(void);
#ifdef CHIRKY_WEB_LAUNCHER
static struct splash_art art;
static int selected;
EM_JS(void, launch, (int index), { Module.onLaunch(index); });
EM_JS(int, launcher_count, (void), { return Module.onLauncherCount(); });
EM_JS(void, launcher_name, (int index,char *text,int capacity), {
    stringToUTF8(Module.onLauncherName(index) || "UNKNOWN",text,capacity);
});
#else
extern const struct chirky_game_api *chirky_game_entry(void);
static const struct chirky_game_api *game;
#endif
EM_JS(void, sound, (const char *path), { Module.onSound(UTF8ToString(path)); });
EM_JS(void, prepare_sound, (unsigned handle,const void *data,unsigned size,unsigned rate,unsigned channels), {
    Module.onAssetReady(handle,data,size,rate,channels);
});
EM_JS(void, play_asset_sound, (unsigned handle), { Module.onAssetSound(handle); });
EM_JS(int, connect_director, (const char *url,const char *game,const char *world), {
    return Module.onDirectorConnect(UTF8ToString(url),UTF8ToString(game),UTF8ToString(world)) ? 1 : 0;
});
EM_JS(void, disconnect_director, (void), { Module.onDirectorDisconnect(); });
EM_JS(int, emit_director_event, (const char *json,unsigned size), {
    return Module.onDirectorEvent(UTF8ToString(json,size)) ? 1 : 0;
});
EM_JS(unsigned, read_director_state,
    (unsigned after_revision,char *text,unsigned capacity,uint32_t *revision), {
    const update=Module.onDirectorState(after_revision);
    if(!update)return 0;
    const size=lengthBytesUTF8(update.text);
    if(size+1>capacity)return 0;
    stringToUTF8(update.text,text,capacity);
    HEAPU32[revision>>2]=update.revision>>>0;
    return size;
});
static chirky_asset request_asset(void *unused,const char *path,enum chirky_asset_type type)
{
    (void)unused;chirky_asset asset=asset_store_request(assets,path,type);
    if(type==CHIRKY_ASSET_SOUND && asset_store_state(assets,asset)==CHIRKY_ASSET_READY) {
        struct chirky_asset_view view=asset_store_view(assets,asset);
        prepare_sound(asset,view.data,(unsigned)view.size,view.rate,view.channels);
    }
    return asset;
}
static enum chirky_asset_state status_asset(void *unused,chirky_asset asset)
{ (void)unused;return asset_store_state(assets,asset); }
static struct chirky_asset_view data_asset(void *unused,chirky_asset asset)
{ (void)unused;return asset_store_view(assets,asset); }
static void release_asset(void *unused,chirky_asset asset)
{ (void)unused;asset_store_release(assets,asset); }
static void sprite(void *unused,chirky_asset image,int x,int y,int w,int h,
    int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)unused;image_cache_draw(&images,&renderer,&api,image,x,y,w,h,sx,sy,sw,sh,r,g,b,a,flip,16,12);
}
static void sound_play(void *unused,chirky_asset asset)
{ (void)unused;play_asset_sound(asset); }
static bool director_connect_api(void *unused,const char *url,const char *game,const char *world)
{ (void)unused;return connect_director(url,game,world)!=0; }
static bool director_event_api(void *unused,const char *json,size_t size)
{ (void)unused;return size<=UINT32_MAX && emit_director_event(json,(unsigned)size)!=0; }
static size_t director_state_api(void *unused,uint32_t after_revision,char *text,
                                 size_t capacity,uint32_t *revision)
{
    (void)unused;
    if(capacity>UINT32_MAX)return 0;
    return read_director_state(after_revision,text,(unsigned)capacity,revision);
}
static void fill(void *unused,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>api.screen_width)w=api.screen_width-x;
    if(y+h>api.screen_height)h=api.screen_height-y;
    if(w>0 && h>0)rect_renderer_rect(&renderer,x+16,y+12,w,h,r,g,b);
}
static void text(void *unused,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    for(;*value;value++,x+=6*scale) {
        const uint8_t *rows=glyph(*value);
        for(int row=0;row<7;row++)for(int col=0;col<5;col++)
            if(rows[row]&(1<<(4-col)))fill(unused,x+col*scale,y-row*scale,scale,scale,r,g,b);
    }
}
static void play(void *unused,const char *device,const char *path)
{ (void)unused;(void)device;sound(path); }
static void label(void *unused,enum chirky_button button,char *out,size_t size)
{
    (void)unused;
    const char *names[]={"LEFT","RIGHT","UP","DOWN","Z","X / ENTER","C","V","A","S","SPACE","ESC"};
    snprintf(out,size,"%s",button>=0 && button<CHIRKY_BUTTON_COUNT?names[button]:"?");
}
EMSCRIPTEN_KEEPALIVE int web_init(const char *config)
{
    EmscriptenWebGLContextAttributes attrs;emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha=0;attrs.depth=0;attrs.stencil=0;attrs.antialias=0;
    context=emscripten_webgl_create_context("#screen",&attrs);
    if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS)goto failed;
    if(!rect_renderer_init(&renderer,320,240))goto failed;
    assets=asset_store_create();if(!assets)goto failed;
    api=(struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,.screen_width=288,.screen_height=216,
        .fill_rect=fill,.play_sound=play,.draw_text=text,.button_label=label,
        .asset_request=request_asset,.asset_status=status_asset,.asset_data=data_asset,.asset_release=release_asset,
        .draw_sprite=sprite,.sound_play=sound_play,
        .director_connect=director_connect_api,.director_event=director_event_api,
        .director_state=director_state_api};
#ifdef CHIRKY_WEB_LAUNCHER
    (void)config;splash_load_file_api(&art,&api,"assets/launcher/splash.ppm");return 1;
#else
    char directory[1024];snprintf(directory,sizeof(directory),"%s",config);
    char *slash=strrchr(directory,'/');if(!slash)goto failed;*slash=0;
    if(!asset_store_prefetch(assets,directory) || asset_store_prefetch_state(assets)!=CHIRKY_ASSET_READY)goto failed;
    game=chirky_game_entry();
    if(game && game->abi_version==CHIRKY_ABI_VERSION && game->init(&api,config))return 1;
#endif
failed:
    web_destroy();return 0;
}
EMSCRIPTEN_KEEPALIVE void web_tick(unsigned mask)
{
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        input.buttons[i]=(mask&(1u<<i))!=0;
        input.button_pressed[i]=input.buttons[i] && !(previous&(1u<<i));
    }
    input.controller_pressed=(mask&~previous)!=0;previous=mask;
#ifdef CHIRKY_WEB_LAUNCHER
    int count=launcher_count();
    if(count>0) {
        if(selected>=count)selected=0;
        if(input.button_pressed[CHIRKY_BUTTON_UP])selected=(selected+count-1)%count;
        if(input.button_pressed[CHIRKY_BUTTON_DOWN])selected=(selected+1)%count;
        if(input.button_pressed[CHIRKY_BUTTON_B])launch(selected);
    }
#else
    game->update(&input);
#endif
}
EMSCRIPTEN_KEEPALIVE void web_render(void)
{
    glViewport(0,0,320,240);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);rect_renderer_begin(&renderer);
#ifdef CHIRKY_WEB_LAUNCHER
    splash_draw(&art,&api);launcher_wordmark(&api);
    int count=launcher_count();
    for(int i=0;i<count;i++) {
        char name[48];launcher_name(i,name,sizeof(name));
        int y=api.screen_height-83-i*24;bool active=i==selected;
        fill(NULL,8,y-13,192,20,active?28:5,active?74:17,active?84:23);
        fill(NULL,12,y-9,3,10,244,194,70);text(NULL,22,y,name,1,250,248,236);
    }
    fill(NULL,8,5,272,18,5,17,23);text(NULL,10,14,"ENTER SELECT - UP DOWN MOVE",1,112,160,170);
#else
    game->render();
#endif
    rect_renderer_flush(&renderer);
}
EMSCRIPTEN_KEEPALIVE void web_destroy(void)
{
#ifdef CHIRKY_WEB_LAUNCHER
    splash_free(&art);
#else
    if(game)game->shutdown();game=NULL;
#endif
    disconnect_director();
    image_cache_clear(&images,&renderer);
    asset_store_destroy(assets);assets=NULL;
    if(context>0){rect_renderer_destroy(&renderer);emscripten_webgl_destroy_context(context);}
    context=0;previous=0;memset(&input,0,sizeof(input));
}
