#include "chirky.h"
#include "pixel_font.h"
#include "splash_art.h"
#include "launcher_wordmark.h"
#include "rect_renderer.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>

static struct rect_renderer renderer;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
static struct chirky_host_api api;
static struct chirky_input input;
static unsigned previous;
#ifdef CHIRKY_WEB_LAUNCHER
static struct splash_art art;
static int selected;
EM_JS(void, launch, (int index), { Module.onLaunch(index); });
#else
extern const struct chirky_game_api *chirky_game_entry(void);
static const struct chirky_game_api *game;
#endif
EM_JS(void, sound, (const char *path), { Module.onSound(UTF8ToString(path)); });
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
    if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS)return 0;
    if(!rect_renderer_init(&renderer,320,240))return 0;
    api=(struct chirky_host_api){CHIRKY_ABI_VERSION,288,216,NULL,fill,play,text,label};
#ifdef CHIRKY_WEB_LAUNCHER
    (void)config;splash_load_file(&art,"assets/launcher/splash.ppm");return 1;
#else
    game=chirky_game_entry();
    return game && game->abi_version==CHIRKY_ABI_VERSION && game->init(&api,config);
#endif
}
EMSCRIPTEN_KEEPALIVE void web_tick(unsigned mask)
{
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        input.buttons[i]=(mask&(1u<<i))!=0;
        input.button_pressed[i]=input.buttons[i] && !(previous&(1u<<i));
    }
    input.controller_pressed=(mask&~previous)!=0;previous=mask;
#ifdef CHIRKY_WEB_LAUNCHER
    if(input.button_pressed[CHIRKY_BUTTON_UP])selected=(selected+2)%3;
    if(input.button_pressed[CHIRKY_BUTTON_DOWN])selected=(selected+1)%3;
    if(input.button_pressed[CHIRKY_BUTTON_B])launch(selected);
#else
    game->update(&input);
#endif
}
EMSCRIPTEN_KEEPALIVE void web_render(void)
{
    glViewport(0,0,320,240);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);rect_renderer_begin(&renderer);
#ifdef CHIRKY_WEB_LAUNCHER
    splash_draw(&art,&api);launcher_wordmark(&api);
    const char *names[]={"PHOSPHOR RUN","ROSEY CHOP","HARDWARE TEST"};
    for(int i=0;i<3;i++) {
        int y=api.screen_height-83-i*24;bool active=i==selected;
        fill(NULL,8,y-13,192,20,active?28:5,active?74:17,active?84:23);
        fill(NULL,12,y-9,3,10,244,194,70);text(NULL,22,y,names[i],1,250,248,236);
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
    if(game)game->shutdown();
#endif
    rect_renderer_destroy(&renderer);emscripten_webgl_destroy_context(context);
}
