/* Browser platform adapter for the portable console menus and input wizard.
   The launcher module stays alive for the lifetime of the display. */
#include "console_ui.h"
#include "input_gate.h"
enum console_screen { CONSOLE_LAUNCHER,CONSOLE_SETTINGS,CONSOLE_INPUT,CONSOLE_TEST,CONSOLE_GAME,CONSOLE_PAUSE };
static struct launcher_config menu;
static struct binding_setup setup;
static struct chirky_input_gate gate;
static enum console_screen screen;
static int selected,settings_option,input_option,pause_option;
static unsigned pad_mask,key_mask,cancel_frames;
static bool diagnostic,from_settings;
static const char *settings_message="";
_Static_assert(sizeof(struct controller_binding)==12,"Browser binding transport layout");
EM_JS(int, catalog_count, (void), { return Module.onLauncherCount(); });
EM_JS(int, catalog_diagnostic, (int index), { return Module.onDiagnostic(index)?1:0; });
EM_JS(void, catalog_name, (int index,char *out,int size), { stringToUTF8(Module.onLauncherName(index),out,size); });
EM_JS(void, launch, (int index), { Module.onLaunch(index); });
EM_JS(void, return_to_launcher, (void), { Module.onReturn(); });
EM_JS(void, platform_option, (int option), { Module.onOption(option); });
EM_JS(int, save_mapping, (int keyboard,const struct controller_binding *bindings), {
    const values=[];
    for(let i=0;i<8;i++)values.push({kind:HEAP32[(bindings+i*12)>>2],code:HEAPU32[(bindings+i*12+4)>>2],direction:HEAP32[(bindings+i*12+8)>>2]});
    return Module.onSaveMapping(!!keyboard,values)?1:0;
});
EM_JS(void, raw_names, (int keyboard,char *out,int size), { stringToUTF8(Module.onRawNames(!!keyboard),out,size); });
static void console_init(void)
{
    launcher_add(&menu,"input","Input Settings",-3,true);
    for(int i=0;i<catalog_count();i++) {
        char name[64],id[32];catalog_name(i,name,sizeof(name));snprintf(id,sizeof(id),"game-%d",i);
        launcher_add(&menu,id,name,i,catalog_diagnostic(i)!=0);
    }
    launcher_add(&menu,"settings","Settings",-1,false);
    launcher_add(&menu,"fullscreen","Full Screen",-4,true);
    launcher_add(&menu,"sound","Mute / Unmute",-5,true);
}
EMSCRIPTEN_KEEPALIVE int web_console_state(void) { return screen; }
EMSCRIPTEN_KEEPALIVE int web_capture_keyboard(void) { return setup.active?(setup.keyboard?1:0):-1; }
EMSCRIPTEN_KEEPALIVE void web_capture(int kind,unsigned code,int direction)
{ chirky_setup_offer(&setup,(struct controller_binding){kind,code,direction}); }
EMSCRIPTEN_KEEPALIVE void web_console_game(int active,int is_diagnostic,int game_index)
{
    diagnostic=is_diagnostic!=0;
    if(active) {
        from_settings=diagnostic;
        for(int i=0;i<launcher_count(&menu,diagnostic);i++) {
            if(launcher_at(&menu,diagnostic,i)->action==game_index) {
                if(diagnostic)settings_option=i;else selected=i;
            }
        }
    }
    screen=active?CONSOLE_GAME:(from_settings?CONSOLE_SETTINGS:CONSOLE_LAUNCHER);
    chirky_gate_begin(&gate);
}
EMSCRIPTEN_KEEPALIVE void web_console_pause(void)
{
    if(screen==CONSOLE_GAME && !diagnostic){screen=CONSOLE_PAUSE;pause_option=0;chirky_gate_begin(&gate);}
}
EMSCRIPTEN_KEEPALIVE int web_console_tick(unsigned mask,unsigned keyboard,unsigned pad,int key_held,int pad_held,int cancel,int pad_buttons)
{
    key_mask=keyboard;pad_mask=pad;
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        input.buttons[i]=(mask&(1u<<i))!=0;
        input.button_pressed[i]=input.buttons[i] && !(previous&(1u<<i));
    }
    previous=mask;
    enum console_screen before=screen;bool was_setup=setup.active;
    if(screen==CONSOLE_GAME || screen==CONSOLE_PAUSE) {
        cancel_frames=input.buttons[CHIRKY_BUTTON_START] && input.buttons[CHIRKY_BUTTON_MENU]?cancel_frames+1:0;
        if(cancel || cancel_frames>=60){cancel_frames=0;return_to_launcher();return 0;}
    }
    if(gate.blocked && !cancel){chirky_gate_accept(&gate,!mask && !key_held && !pad_held);return 0;}
    if(setup.active) {
        cancel_frames=pad_buttons>=2?cancel_frames+1:0;
        if(cancel || cancel_frames>=60) {
            setup.active=false;settings_message="CANCELLED - NOTHING CHANGED";cancel_frames=0;
        } else {
            setup_release(&setup,setup.keyboard?!key_held:!pad_held);
            if(setup.complete) {
                settings_message=save_mapping(setup.keyboard,setup.pending)?"BUTTONS SAVED":"SAVE FAILED - NOTHING CHANGED";
                setup.active=false;
            }
        }
    } else if(screen==CONSOLE_GAME) {
        if(cancel || (diagnostic && input.button_pressed[CHIRKY_BUTTON_MENU]))return_to_launcher();
        else if(input.button_pressed[CHIRKY_BUTTON_MENU]){screen=CONSOLE_PAUSE;pause_option=0;}
        else return 1;
    } else if(screen==CONSOLE_PAUSE) {
        int choice=console_menu_update(&pause_option,2,&input,cancel || input.button_pressed[CHIRKY_BUTTON_MENU]);
        if(choice==-2 || choice==0)screen=CONSOLE_GAME;
        else if(choice==1){from_settings=false;return_to_launcher();}
    } else if(screen==CONSOLE_TEST) {
        cancel_frames=input.buttons[CHIRKY_BUTTON_SECONDARY]?cancel_frames+1:0;
        if(cancel || cancel_frames>=60){screen=CONSOLE_INPUT;cancel_frames=0;}
    } else if(screen==CONSOLE_INPUT) {
        int choice=console_menu_update(&input_option,4,&input,cancel);
        if(choice==-2 || choice==3)screen=CONSOLE_SETTINGS;
        else if(choice==2){screen=CONSOLE_TEST;cancel_frames=0;}
        else if(choice>=0){setup_begin(&setup,choice==1);cancel_frames=0;settings_message="";}
    } else if(screen==CONSOLE_SETTINGS) {
        int count=launcher_count(&menu,true),choice=console_menu_update(&settings_option,count+1,&input,cancel);
        if(choice==-2 || choice==count)screen=CONSOLE_LAUNCHER;
        else if(choice>=0) {
            int action=launcher_at(&menu,true,choice)->action;
            if(action==-3){screen=CONSOLE_INPUT;input_option=0;settings_message="";}
            else if(action>=0){from_settings=true;launch(action);}
            else platform_option(action);
        }
    } else {
        int choice=console_menu_update(&selected,launcher_count(&menu,false),&input,false);
        if(choice>=0) {
            int action=launcher_at(&menu,false,choice)->action;
            if(action==-1){screen=CONSOLE_SETTINGS;settings_option=0;}
            else {from_settings=false;launch(action);}
        } else if(input.button_pressed[CHIRKY_BUTTON_MENU]){screen=CONSOLE_SETTINGS;settings_option=0;}
    }
    if(before!=screen || was_setup!=setup.active)chirky_gate_begin(&gate);
    return 0;
}
static void console_render(void)
{
    if(screen==CONSOLE_PAUSE)console_draw_pause_menu(&api,pause_option);
    else if(screen==CONSOLE_SETTINGS)console_draw_settings_menu(&api,&menu,settings_option);
    else if(screen==CONSOLE_INPUT || screen==CONSOLE_TEST) {
        char pad[96],key[96];raw_names(0,pad,sizeof(pad));raw_names(1,key,sizeof(key));
        console_draw_controller_settings(&api,&setup,screen==CONSOLE_TEST,input_option,settings_message);
        console_draw_live_inputs(&api,pad_mask,key_mask,pad,key);
    } else if(screen==CONSOLE_LAUNCHER)console_draw_launcher(&api,&art,&menu,selected);
}
