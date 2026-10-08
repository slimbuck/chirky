#include "console.h"
#include "console_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {struct chirky_console console;int loads,unloads,saves,actions;unsigned profile;bool conflict;bool save_ok,load_ok;};
static bool load(void *context,int index){struct fixture *f=context;assert(index==0 || index==1);f->loads++;return f->load_ok;}
static void unload(void *context){((struct fixture *)context)->unloads++;}
static enum mapping_save_result save(void *context,unsigned profile,const struct controller_binding *bindings)
{struct fixture *f=context;assert(profile<=2);assert(bindings[7].code==107);f->saves++;f->profile=profile;return f->conflict?MAPPING_CONFLICT:f->save_ok;}
static void action(void *context,enum console_action action){(void)action;((struct fixture *)context)->actions++;}
static void init(struct fixture *f,unsigned caps)
{
    *f=(struct fixture){.save_ok=true,.load_ok=true};
    f->console.services=(struct console_services){.context=f,.load=load,.unload=unload,.save_keyboard=save,.action=action};
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
static int controller_info(void *context,uint32_t id,char *name,size_t size)
{struct fixture *f=context;snprintf(name,size,"Test pad");return id==9?(int)f->profile:-1;}
static enum mapping_save_result controller_save(void *context,uint32_t id,enum controller_profile profile,const struct controller_binding *map)
{struct fixture *f=context;assert(id==9);assert(map || profile==CONTROLLER_SNES);f->saves++;if(f->save_ok)f->profile=profile;return f->save_ok?MAPPING_SAVED:MAPPING_FAILED;}
static void controller_profiles(unsigned capabilities)
{
    struct fixture f;init(&f,capabilities);struct chirky_console *c=&f.console;
    c->services.controller_info=controller_info;c->services.save_controller=controller_save;
    c->controller_settings=true;
    tap(&f,CHIRKY_BUTTON_PRIMARY);assert(c->controller_selecting);
    chirky_console_controller_press(c,8);assert(c->controller_selecting);
    chirky_console_controller_press(c,9);assert(c->controller_options && !c->controller_selecting);
    tap(&f,CHIRKY_BUTTON_PRIMARY);assert(f.saves==1 && f.profile==CONTROLLER_SNES && !c->controller_options);
    tap(&f,CHIRKY_BUTTON_PRIMARY);chirky_console_controller_press(c,9);settle(&f);
    c->controller_option=2;tap(&f,CHIRKY_BUTTON_PRIMARY);
    assert(c->setup.active && c->setup.controller_profile==CONTROLLER_SNES);
    chirky_console_capture_controller(c,8,(struct controller_binding){BINDING_KEY,100,0});
    tick(&f,-1,false,false);assert(c->setup.step==0 && !c->setup.ready);
    for(unsigned i=0;i<8;i++){chirky_console_capture_controller(c,9,(struct controller_binding){BINDING_KEY,100+i,0});tick(&f,-1,false,false);}
    assert(f.saves==2 && f.profile==CONTROLLER_SNES && !c->setup.active);
    c->controller_option=1;tap(&f,CHIRKY_BUTTON_PRIMARY);
    assert(c->setup.controller_profile==CONTROLLER_GENERIC);
    f.save_ok=false;
    for(unsigned i=0;i<8;i++){chirky_console_capture_controller(c,9,(struct controller_binding){BINDING_KEY,100+i,0});tick(&f,-1,false,false);}
    assert(f.saves==3 && f.profile==CONTROLLER_SNES && !strcmp(c->settings_message,"SAVE FAILED - NOTHING CHANGED"));
    tap(&f,CHIRKY_BUTTON_PRIMARY);assert(c->setup.active);
    c->controller_id=8;tick(&f,-1,false,false);
    assert(!c->setup.active && !c->controller_options && c->controller_selecting);
    assert(!strcmp(c->settings_message,"CONTROLLER DISCONNECTED"));
}
static void shared_trace(unsigned caps)
{
    struct fixture f;init(&f,caps);struct chirky_console *c=&f.console;
    tap(&f,CHIRKY_BUTTON_MENU);assert(chirky_console_screen(c)==SCREEN_LAUNCHER);
    tap(&f,CHIRKY_BUTTON_DOWN);tap(&f,CHIRKY_BUTTON_PRIMARY);
    assert(chirky_console_screen(c)==SCREEN_SETTINGS);
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
    for(unsigned profile=1;profile<=2;profile++) {
        c->selected_option=profile==1?1:3;f.conflict=profile==2;f.save_ok=true;
        tap(&f,CHIRKY_BUTTON_PRIMARY);assert(c->setup.keyboard_profile==profile);
        for(unsigned i=0;i<8;i++){chirky_console_capture(c,(struct controller_binding){BINDING_KEY,100+i,0});tick(&f,-1,false,false);}
        assert(f.profile==profile);
        assert(!strcmp(c->settings_message,f.conflict?"KEY USED BY OTHER PLAYER":"BUTTONS SAVED"));
    }
    chirky_console_home(c,false);settle(&f);tap(&f,CHIRKY_BUTTON_UP);tap(&f,CHIRKY_BUTTON_PRIMARY);
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
static int pause_text_bottom,pause_selection_top;
static void pause_rect(void *unused,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)unused;(void)x;(void)w;(void)h;
    if(r==244 && g==233 && b==202 && y>132 && y<pause_text_bottom)pause_text_bottom=y;
    if(r==244 && g==184 && b==69 && y+h>pause_selection_top)pause_selection_top=y+h;
}
static unsigned char launcher_pixels[240][320][3];
static int mascot_draws;
static enum chirky_asset_state mascot_status(void *unused,chirky_asset handle)
{(void)unused;assert(handle==1);return CHIRKY_ASSET_READY;}
static struct chirky_asset_view mascot_data(void *unused,chirky_asset handle)
{(void)unused;assert(handle==1);return (struct chirky_asset_view){.width=64,.height=64};}
static void mascot_draw(void *context,chirky_asset handle,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
    unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    const struct chirky_host_api *api=context;
    assert(handle==1 && w==64 && h==64 && sw==w && sh==h && sx==0 && sy==0);
    assert(x>=0 && y>=0 && x+w<=api->screen_width && y+h<=api->screen_height);
    assert(r==255 && g==255 && b==255 && a==255 && !flip);mascot_draws++;
}
static void launcher_paint(void *context,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    const struct chirky_host_api *api=context;
    assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=api->screen_width && y+h<=api->screen_height);
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++) {
        launcher_pixels[yy][xx][0]=r;launcher_pixels[yy][xx][1]=g;launcher_pixels[yy][xx][2]=b;
    }
}
static void scrolling_launcher(void)
{
    struct fixture f;init(&f,0);
    const struct console_game games[]={{"phosphor-run","Phosphor Run",false},{"rosey-chop","Rosey Chop",false},{"bramble-hollow","Bramble Hollow",false}};
    for(int i=0;i<3;i++)assert(console_icon(games[i].id));
    assert(!console_icon("settings") && !console_icon("unknown-game"));
    chirky_console_catalog(&f.console,games,3,0);
    tick(&f,CHIRKY_BUTTON_UP,true,false);
    assert(f.console.selected_game==0 && f.console.menu_offset==0);
    assert(f.console.menu_offset==0 && f.console.menu_velocity==0);
    tick(&f,CHIRKY_BUTTON_DOWN,true,false);
    assert(f.console.selected_game==1 && f.console.menu_offset==1);
    for(int i=0;i<4;i++)tick(&f,-1,false,false);
    assert(f.console.menu_offset>0 && f.console.menu_offset<1);
    tick(&f,CHIRKY_BUTTON_UP,true,false);assert(f.console.selected_game==0);
    for(int i=0;i<90;i++)tick(&f,-1,false,false);
    assert(f.console.menu_offset==0 && f.console.menu_velocity==0);
    const int sizes[][2]={{256,192},{288,216},{320,240}};
    const float offsets[]={0,.35f,-.6f,2.2f,-3.7f};struct splash_art art={0};
    art.asset_requested=true;art.image=1;art.asset_api.asset_status=mascot_status;art.asset_api.asset_data=mascot_data;
    for(int size=0;size<3;size++) {
        struct chirky_host_api api={.screen_width=sizes[size][0],.screen_height=sizes[size][1],.fill_rect=launcher_paint,.draw_sprite=mascot_draw};api.context=&api;
        int centre=(28+api.screen_height-78)/2;
        for(unsigned frame=0;frame<sizeof(offsets)/sizeof(*offsets);frame++) {
            memset(launcher_pixels,0,sizeof(launcher_pixels));
            console_draw_launcher(&api,&art,&f.console.launcher,frame%4,offsets[frame]);
            for(int y=centre-9;y<centre+9;y++)assert(!memcmp(launcher_pixels[y][13],(unsigned char[]){244,184,69},3));
            assert(memcmp(launcher_pixels[centre-18][13],(unsigned char[]){244,184,69},3));
        }
    }
    assert(mascot_draws==15);
    for(int i=0;i<10;i++)tap(&f,CHIRKY_BUTTON_DOWN);
    assert(f.console.selected_game==3);
    for(int i=0;i<90;i++)tick(&f,-1,false,false);
    tick(&f,CHIRKY_BUTTON_DOWN,true,false);
    assert(f.console.menu_offset==0 && f.console.menu_velocity==0);
    /* No repeated cards or labels beyond either end of a settled list. */
    struct chirky_host_api api={.screen_width=288,.screen_height=216,.fill_rect=launcher_paint};api.context=&api;
    const char *labels[]={"First","Middle","Last"};
    for(int selected=0;selected<=2;selected+=2) {
        memset(launcher_pixels,0,sizeof(launcher_pixels));
        console_scroll_list(&api,labels,NULL,3,selected,0,28,138);
        int y=selected==0?113:53;
        for(int x=12;x<276;x++)assert(!memcmp(launcher_pixels[y][x],(unsigned char[]){0,0,0},3));
    }
}
static void shared_menu_layouts(void)
{
    struct fixture f;init(&f,CONSOLE_CAN_DISPLAY);
    const enum console_action pages[]={CONSOLE_SETTINGS,CONSOLE_INPUT,CONSOLE_DISPLAY};
    const int counts[]={4,5,6};
    struct chirky_console *c=&f.console;
    for(int page=0;page<3;page++) {
        chirky_console_open(c,pages[page]);settle(&f);
        tick(&f,CHIRKY_BUTTON_UP,true,false);
        int *selected=page==0?&c->settings_option:page==1?&c->selected_option:&c->display_option;
        assert(*selected==0 && c->menu_offset==0 && c->menu_velocity==0);
        for(int i=0;i<counts[page]+3;i++)tap(&f,CHIRKY_BUTTON_DOWN);
        assert(*selected==counts[page]-1);
        for(int i=0;i<90;i++)tick(&f,-1,false,false);
        assert(c->menu_offset==0 && c->menu_velocity==0);
        tick(&f,CHIRKY_BUTTON_DOWN,true,false);
        assert(c->menu_offset==0 && c->menu_velocity==0);
        tick(&f,CHIRKY_BUTTON_UP,true,false);
        assert(c->menu_offset==-1 && *selected==counts[page]-2);
        tick(&f,CHIRKY_BUTTON_SECONDARY,true,false);
        assert(c->menu_offset==0 && c->menu_velocity==0);
    }
    struct splash_art art={.asset_requested=true,.image=1};
    art.asset_api.asset_status=mascot_status;art.asset_api.asset_data=mascot_data;
    const int sizes[][2]={{256,192},{288,216},{320,240}};
    for(int size=0;size<3;size++) {
        struct chirky_host_api api={.screen_width=sizes[size][0],.screen_height=sizes[size][1],.fill_rect=launcher_paint,.draw_sprite=mascot_draw};api.context=&api;
        for(int page=0;page<3;page++) {
            chirky_console_open(c,pages[page]);c->menu_offset=-.6f;
            chirky_console_render(c,&api,&art,0,0,"PAD NONE","KEY NONE");
        }
        chirky_console_open(c,CONSOLE_INPUT);c->input_test=true;
        chirky_console_render(c,&api,&art,255,255,"PAD BTN SOUTH","KEY LEFT");
        c->input_test=false;setup_begin(&c->setup,true);
        for(int step=0;step<8;step++) {
            c->setup.step=step;
            chirky_console_render(c,&api,&art,0,0,"PAD NONE","KEY NONE");
        }
    }
}
static void physical_label(void *context,enum chirky_button action,char *out,size_t size)
{snprintf(out,size,"%s",((const char *const *)context)[action]);}
static void check_direction_labels(void)
{
    const char *labels[]={CHIRKY_ARROW_LEFT,CHIRKY_ARROW_RIGHT,CHIRKY_ARROW_UP,CHIRKY_ARROW_DOWN,"B","Y","START","SELECT"};
    struct chirky_host_api api={.context=labels,.button_label=physical_label};
    char text[100];
    chirky_direction_label(&api,0,4,text,sizeof(text));assert(!strcmp(text,CHIRKY_ARROW_LEFT CHIRKY_ARROW_RIGHT CHIRKY_ARROW_UP CHIRKY_ARROW_DOWN));
    assert(chirky_text_length(text)==4 && launcher_text_width(text,0)==28);
    chirky_direction_label(&api,2,2,text,sizeof(text));assert(!strcmp(text,CHIRKY_ARROW_UP CHIRKY_ARROW_DOWN));
    labels[2]="W";labels[3]="S";
    chirky_direction_label(&api,2,2,text,sizeof(text));assert(!strcmp(text,"W/S"));
    labels[2]=CHIRKY_ARROW_UP;labels[3]=CHIRKY_ARROW_DOWN;
    chirky_direction_label(&api,0,4,text,2);assert(text[0]==0);
    chirky_direction_label(&api,0,4,text,5);assert(!strcmp(text,CHIRKY_ARROW_LEFT));
    chirky_text_copy(text,sizeof(text),CHIRKY_ARROW_LEFT " MOVE",3);assert(!strcmp(text,CHIRKY_ARROW_LEFT " M"));
    assert(chirky_text_length("\xe2")==1 && chirky_text_length("\xe2\x86")==2);
    static const uint8_t up[7]={4,14,21,4,4,4,0};assert(!memcmp(glyph(0x2191),up,sizeof(up)));
    chirky_direction_label(&api,0,4,NULL,0);
    api.button_label=NULL;chirky_input_label(&api,CHIRKY_BUTTON_PRIMARY,text,sizeof(text));assert(!strcmp(text,"UNBOUND"));
}
int main(void)
{
    check_direction_labels();
    scrolling_launcher();
    shared_menu_layouts();
    pause_text_bottom=999;pause_selection_top=-1;
    const struct chirky_host_api api={.screen_width=288,.screen_height=216,.fill_rect=pause_rect};
    console_draw_pause_menu(&api,0);
    assert(pause_selection_top>=0 && pause_text_bottom-(pause_selection_top+1)>=3);
    shared_trace(CONSOLE_CAN_POWER|CONSOLE_CAN_DISPLAY|CONSOLE_CAN_TIMING);
    shared_trace(CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
    controller_profiles(CONSOLE_CAN_POWER|CONSOLE_CAN_DISPLAY|CONSOLE_CAN_TIMING);
    controller_profiles(CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
    struct fixture web;init(&web,CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
    chirky_console_open(&web.console,CONSOLE_DISPLAY);assert(chirky_console_screen(&web.console)==SCREEN_LAUNCHER);
    chirky_console_timing(&web.console,true,0);chirky_console_timing(&web.console,true,3000000);assert(!web.actions);
    puts("Console: same navigation, mapping, release gates, pause, diagnostics and load failures across platform capabilities.");
}
