#include "chirky.h"
#include "drawing.h"
#include "splash_art.h"
#include "launcher_wordmark.h"
#include "rect_renderer.h"
#include "asset_store.h"
#include "image_cache.h"
#include "runtime.h"
#include "viewport.h"
#include "save_data.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdlib.h>

static struct rect_renderer renderer;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
static struct chirky_host_api api;
static struct chirky_input input;
static unsigned previous;
static struct asset_store *assets;
static struct image_cache images;
EMSCRIPTEN_KEEPALIVE void web_destroy(void);
static struct splash_art art;
static struct chirky_runtime runtime;
static void *game_library;
static unsigned load_generation;
EMSCRIPTEN_KEEPALIVE void web_unload(void);
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
static chirky_asset create_image(void *unused,unsigned w,unsigned h,const void *rgba,size_t size)
{ (void)unused;return asset_store_image_create(assets,w,h,rgba,size); }
static void sprite(void *unused,chirky_asset image,int x,int y,int w,int h,
    int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)unused;image_cache_draw(&images,&renderer,&api,image,x,y,w,h,sx,sy,sw,sh,r,g,b,a,flip,
        CHIRKY_SAFE_X,CHIRKY_SAFE_Y);
}
static void sound_play(void *unused,chirky_asset asset)
{ (void)unused;play_asset_sound(asset); }
static void projected_sprite(void *unused,chirky_asset image,float x,float y,float scale,int sx,int sy,int sw,int sh,
    unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)unused;const struct rect_renderer_texture *t=image_cache_get(&images,&renderer,&api,image);
    if(t)rect_renderer_sprite_projected(&renderer,t,x,y,scale,sx,sy,sw,sh,r,g,b,a,flip,
        CHIRKY_SAFE_X,CHIRKY_SAFE_Y,api.screen_width,api.screen_height);
}
EM_JS(unsigned,read_save,(const char *game,const char *key,void *data,unsigned capacity),{
    const bytes=Module.onSaveRead(UTF8ToString(game),UTF8ToString(key));
    if(!bytes || bytes.length>capacity)return 0;
    HEAPU8.set(bytes,data);return bytes.length;
});
EM_JS(int,write_save,(const char *game,const char *key,const void *data,unsigned size),{
    return Module.onSaveWrite(UTF8ToString(game),UTF8ToString(key),HEAPU8.subarray(data,data+size))?1:0;
});
static size_t save_read_api(void *unused,const char *game,const char *key,void *data,size_t capacity)
{
    (void)unused;
    if(!chirky_save_name(game) || !chirky_save_name(key) || !data || !capacity)return 0;
    return read_save(game,key,data,(unsigned)(capacity<CHIRKY_SAVE_LIMIT?capacity:CHIRKY_SAVE_LIMIT));
}
static bool save_write_api(void *unused,const char *game,const char *key,const void *data,size_t size)
{
    (void)unused;
    return chirky_save_name(game) && chirky_save_name(key) && data && size && size<=CHIRKY_SAVE_LIMIT && write_save(game,key,data,(unsigned)size);
}
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
static bool mesh(void *unused,const struct chirky_mesh_vertex *v,size_t count,float ambient)
{
    (void)unused;return rect_renderer_mesh(&renderer,v,count,ambient,CHIRKY_SAFE_X,CHIRKY_SAFE_Y,
                                         api.screen_width,api.screen_height);
}
static void fill(void *unused,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;
    if(chirky_clip_rect(&api,&x,&y,&w,&h))rect_renderer_rect(&renderer,x+CHIRKY_SAFE_X,y+CHIRKY_SAFE_Y,w,h,r,g,b);
}
static void text(void *unused,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;
    if(!rect_renderer_text(&renderer,x+CHIRKY_SAFE_X,y+CHIRKY_SAFE_Y,value,scale,r,g,b,false,
                          CHIRKY_SAFE_X,CHIRKY_SAFE_Y,api.screen_width,api.screen_height))
        chirky_draw_text_pixels(&api,x,y,value,scale,r,g,b);
}
static void outlined(void *unused,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;
    if(rect_renderer_text(&renderer,x+CHIRKY_SAFE_X,y+CHIRKY_SAFE_Y,value,scale,r,g,b,true,
                         CHIRKY_SAFE_X,CHIRKY_SAFE_Y,api.screen_width,api.screen_height))return;
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(dx || dy)text(NULL,x+dx,y+dy,value,scale,0,0,0);
    text(NULL,x,y,value,scale,r,g,b);
}
static void play(void *unused,const char *device,const char *path)
{ (void)unused;(void)device;sound(path); }
static void label(void *unused,enum chirky_button button,char *out,size_t size)
{
    (void)unused;
    const char *names[]={"LEFT","RIGHT","UP","DOWN","PRIMARY","SECONDARY","START","MENU"};
    snprintf(out,size,"%s",button>=0 && button<CHIRKY_BUTTON_COUNT?names[button]:"?");
}
#include "console_bridge.h"
EMSCRIPTEN_KEEPALIVE int web_init(const char *config)
{
    EmscriptenWebGLContextAttributes attrs;emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha=0;attrs.depth=1;attrs.stencil=0;attrs.antialias=0;
    context=emscripten_webgl_create_context("#screen",&attrs);
    if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS)goto failed;
    if(!rect_renderer_init(&renderer,CHIRKY_FRAMEBUFFER_WIDTH,CHIRKY_FRAMEBUFFER_HEIGHT))goto failed;
    assets=asset_store_create();if(!assets)goto failed;
    api=(struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,
        .screen_width=CHIRKY_VIEWPORT_WIDTH,.screen_height=CHIRKY_VIEWPORT_HEIGHT,
        .fill_rect=fill,.play_sound=play,.draw_text=text,.button_label=label,
        .asset_request=request_asset,.asset_status=status_asset,.asset_data=data_asset,.asset_release=release_asset,
        .draw_sprite=sprite,.sound_play=sound_play,.draw_mesh=mesh,.image_create=create_image,.draw_text_outlined=outlined,
        .draw_sprite_projected=projected_sprite,
        .director_connect=director_connect_api,.director_event=director_event_api,
        .director_state=director_state_api,.save_read=save_read_api,.save_write=save_write_api};
    (void)config;splash_load_file_api(&art,&api,"assets/launcher/mascot.ppm");console_init();return 1;
failed:
    web_destroy();return 0;
}
EMSCRIPTEN_KEEPALIVE void web_tick(unsigned mask)
{
    (void)mask;chirky_runtime_update(&runtime,&input);
}
EMSCRIPTEN_KEEPALIVE void web_render(void)
{
    emscripten_webgl_make_context_current(context);
    glViewport(0,0,CHIRKY_FRAMEBUFFER_WIDTH,CHIRKY_FRAMEBUFFER_HEIGHT);
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    rect_renderer_begin(&renderer);
    chirky_runtime_render(&runtime);
    console_render();
    rect_renderer_flush(&renderer);
}
EMSCRIPTEN_KEEPALIVE void web_unload(void)
{
    ++load_generation;
    chirky_runtime_stop(&runtime);
    disconnect_director();
    if(game_library){dlclose(game_library);game_library=NULL;}
    emscripten_webgl_make_context_current(context);
    splash_free(&art);image_cache_clear(&images,&renderer);
    if(assets){asset_store_clear(assets);splash_load_file_api(&art,&api,"assets/launcher/mascot.ppm");}
}
struct module_load {unsigned generation;char config[1024];};
EM_JS(void,module_loaded,(int success),{Module.onLoaded(!!success);});
static void module_failed(void *data)
{ free(data);module_loaded(0); }
static void module_ready(void *data,void *handle)
{
    struct module_load *load=data;
    if(load->generation!=load_generation){dlclose(handle);free(load);module_loaded(0);return;}
    game_library=handle;
    chirky_game_entry_fn entry=(chirky_game_entry_fn)dlsym(handle,"chirky_game_entry");
    bool ok=entry && chirky_runtime_start(&runtime,entry(),&api,load->config);
    if(ok)chirky_console_loaded(&console,true);
    free(load);module_loaded(ok);
}
/* The public asynchronous loader reads the downloaded bytes from the host's
   filesystem, without fetching them again or blocking the browser on compile. */
EMSCRIPTEN_KEEPALIVE void web_load(const char *module,const char *config)
{
    char directory[1024];snprintf(directory,sizeof(directory),"%s",config);
    char *slash=strrchr(directory,'/');if(!slash){module_loaded(0);return;}*slash=0;
    if(!asset_store_prefetch(assets,directory) || asset_store_prefetch_state(assets)!=CHIRKY_ASSET_READY){module_loaded(0);return;}
    struct module_load *load=calloc(1,sizeof(*load));
    if(!load){module_loaded(0);return;}
    load->generation=load_generation;snprintf(load->config,sizeof(load->config),"%s",config);
    emscripten_dlopen(module,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE,load,module_ready,module_failed);
}
EMSCRIPTEN_KEEPALIVE void web_load_failed(void){chirky_console_loaded(&console,false);}
EMSCRIPTEN_KEEPALIVE void web_destroy(void)
{
    web_unload();splash_free(&art);
    asset_store_destroy(assets);assets=NULL;
    if(context>0){rect_renderer_destroy(&renderer);emscripten_webgl_destroy_context(context);}
    context=0;previous=0;memset(&input,0,sizeof(input));
}
