#include "console.h"
#include "console_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {struct chirky_console console;int loads,unloads,saves,actions;bool save_ok,load_ok;};
static bool load(void *context,int index){struct fixture *f=context;assert(index==0 || index==1);f->loads++;return f->load_ok;}
static void unload(void *context){((struct fixture *)context)->unloads++;}
static bool save(void *context,bool keyboard,const struct controller_binding *bindings)
{struct fixture *f=context;assert(keyboard);assert(bindings[7].code==107);f->saves++;return f->save_ok;}
static void action(void *context,enum console_action action){(void)action;((struct fixture *)context)->actions++;}
static void init(struct fixture *f,unsigned caps)
{
    *f=(struct fixture){.save_ok=true,.load_ok=true};
    f->console.services=(struct console_services){.context=f,.load=load,.unload=unload,.save_mapping=save,.action=action};
    const struct console_game games[]={{"game","Game",false},{"diagnostic","Diagnostic",true}};
    chirky_console_catalog(&f->console,games,2,caps);
}
static bool tick(struct fixture *f,int button,bool pressed,bool cancel)
{
    struct chirky_input input={0};
    if(button>=0){input.buttons[button]=true;input.button_pressed[button]=pressed;}
    return chirky_console_update(&f->console,&(struct console_input){.logical=&input,.cancel=cancel});
}
static void settle(struct fixture *f){for(int i=0;i<3;i++)tick(f,-1,false,false);}
static void tap(struct fixture *f,int button){settle(f);tick(f,button,true,false);settle(f);}
static void shared_trace(unsigned caps)
{
    struct fixture f;init(&f,caps);struct chirky_console *c=&f.console;
    tap(&f,CHIRKY_BUTTON_MENU);assert(chirky_console_screen(c)==SCREEN_SETTINGS);
    tap(&f,CHIRKY_BUTTON_PRIMARY);assert(chirky_console_screen(c)==SCREEN_INPUT);
    tap(&f,CHIRKY_BUTTON_DOWN);tap(&f,CHIRKY_BUTTON_PRIMARY);assert(c->setup.active && c->setup.keyboard);
    for(unsigned i=0;i<8;i++) {
        chirky_console_capture(c,(struct controller_binding){BINDING_KEY,100+i,0});
        tick(&f,-1,false,false);
    }
    assert(!c->setup.active && f.saves==1 && !strcmp(c->settings_message,"BUTTONS SAVED"));
    tap(&f,CHIRKY_BUTTON_PRIMARY);assert(c->setup.active);
    chirky_console_capture(c,(struct controller_binding){BINDING_KEY,100,0});tick(&f,-1,false,false);
    chirky_console_capture(c,(struct controller_binding){BINDING_KEY,100,0});tick(&f,-1,false,false);
    assert(c->setup.step==1 && f.saves==1);
    tick(&f,-1,false,true);assert(!c->setup.active && f.saves==1);
    f.save_ok=false;settle(&f);tap(&f,CHIRKY_BUTTON_PRIMARY);
    for(unsigned i=0;i<8;i++){chirky_console_capture(c,(struct controller_binding){BINDING_KEY,100+i,0});tick(&f,-1,false,false);}
    assert(f.saves==2 && !strcmp(c->settings_message,"SAVE FAILED - NOTHING CHANGED"));
    chirky_console_home(c,false);settle(&f);tap(&f,CHIRKY_BUTTON_PRIMARY);
    assert(c->loading && !c->game_active && f.loads==1);
    assert(!tick(&f,CHIRKY_BUTTON_PRIMARY,true,false));
    chirky_console_loaded(c,true);
    for(int i=0;i<10;i++)assert(!tick(&f,CHIRKY_BUTTON_PRIMARY,true,false));
    settle(&f);assert(tick(&f,-1,false,false));
    tap(&f,CHIRKY_BUTTON_MENU);assert(c->paused);
    for(int i=0;i<100;i++)assert(!tick(&f,-1,false,false));
    tap(&f,CHIRKY_BUTTON_SECONDARY);assert(!c->paused && c->game_active);
    chirky_console_launch(c,1,true);chirky_console_loaded(c,true);settle(&f);
    tap(&f,CHIRKY_BUTTON_MENU);assert(chirky_console_screen(c)==SCREEN_SETTINGS);
    chirky_console_launch(c,0,false);tick(&f,-1,false,true);
    assert(!c->loading && !c->game_active && chirky_console_screen(c)==SCREEN_LAUNCHER);
    f.load_ok=false;assert(!chirky_console_launch(c,0,false));assert(!c->loading && !c->game_active);
    f.load_ok=true;chirky_console_launch(c,0,false);chirky_console_loaded(c,false);assert(!c->game_active && !c->loading);
    chirky_console_launch(c,0,false);chirky_console_loaded(c,true);settle(&f);
    struct chirky_input chord={0};chord.buttons[CHIRKY_BUTTON_START]=chord.buttons[CHIRKY_BUTTON_MENU]=true;
    chord.button_pressed[CHIRKY_BUTTON_MENU]=true;
    for(int i=0;i<60;i++) {
        assert(!chirky_console_update(c,&(struct console_input){.logical=&chord,.controller_held=true}));
        chord.button_pressed[CHIRKY_BUTTON_MENU]=false;
    }
    assert(!c->game_active && !c->paused && chirky_console_screen(c)==SCREEN_LAUNCHER);
}
static int pause_text_bottom,pause_divider;
static void pause_rect(void *unused,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;(void)x;(void)w;(void)h;
    if(r==238 && g==240 && b==232 && y<pause_text_bottom)pause_text_bottom=y;
    if(r==40 && g==75 && b==85)pause_divider=y;
}
int main(void)
{
    pause_text_bottom=999;pause_divider=-1;
    const struct chirky_host_api api={.screen_width=288,.screen_height=216,.fill_rect=pause_rect};
    console_draw_pause_menu(&api,0);
    assert(pause_divider>=0 && pause_text_bottom-(pause_divider+1)>=3);
    shared_trace(CONSOLE_CAN_POWER|CONSOLE_CAN_DISPLAY|CONSOLE_CAN_TIMING);
    shared_trace(CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
    struct fixture web;init(&web,CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
    chirky_console_open(&web.console,CONSOLE_DISPLAY);assert(chirky_console_screen(&web.console)==SCREEN_LAUNCHER);
    chirky_console_timing(&web.console,true,0);chirky_console_timing(&web.console,true,3000000);assert(!web.actions);
    puts("Console: same navigation, mapping, release gates, pause, diagnostics and load failures across platform capabilities.");
}
