/* DOM, catalog and persistence transport. All console decisions live in src/console.c. */
#include "console.h"
static struct chirky_console console;
_Static_assert(sizeof(struct controller_binding)==12,"Browser binding transport layout");
EM_JS(int,catalog_count,(void),{return Module.onLauncherCount();});
EM_JS(int,catalog_diagnostic,(int index),{return Module.onDiagnostic(index)?1:0;});
EM_JS(void,catalog_name,(int index,char *out,int size),{stringToUTF8(Module.onLauncherName(index),out,size);});
EM_JS(void,catalog_id,(int index,char *out,int size),{stringToUTF8(Module.onLauncherId(index),out,size);});
EM_JS(void,launch,(int index),{Module.onLaunch(index);});
EM_JS(void,stopped,(void),{Module.onStopped();});
EM_JS(void,platform_option,(int option),{Module.onOption(option);});
EM_JS(int,save_keyboard,(unsigned profile,const struct controller_binding *bindings),{
    const values=[];
    for(let i=0;i<8;i++)values.push({kind:HEAP32[(bindings+i*12)>>2],code:HEAPU32[(bindings+i*12+4)>>2],direction:HEAP32[(bindings+i*12+8)>>2]});
    return Number(Module.onSaveKeyboard(values,profile));
});
EM_JS(void,raw_names,(int keyboard,char *out,int size),{stringToUTF8(Module.onRawNames(!!keyboard),out,size);});
static bool load_service(void *unused,int index){(void)unused;launch(index);return true;}
static void unload_service(void *unused){(void)unused;web_unload();stopped();}
static enum mapping_save_result mapping_service(void *unused,unsigned profile,const struct controller_binding *bindings)
{(void)unused;return (enum mapping_save_result)save_keyboard(profile,bindings);}
EM_JS(int,controller_info,(unsigned id,char *name,size_t size),{
    const info=Module.onControllerInfo(id);if(!info)return -1;
    stringToUTF8(info.name,name,size);return info.profile;
});
EM_JS(int,save_controller,(unsigned id,int profile,const struct controller_binding *bindings),{
    let values=null;
    if(bindings){values=[];for(let i=0;i<8;i++)values.push({kind:HEAP32[(bindings+i*12)>>2],code:HEAPU32[(bindings+i*12+4)>>2],direction:HEAP32[(bindings+i*12+8)>>2]});}
    return Module.onSaveController(id,profile,values)?1:0;
});
static int controller_info_service(void *unused,uint32_t id,char *name,size_t size)
{(void)unused;return controller_info(id,name,size);}
static enum mapping_save_result save_controller_service(void *unused,uint32_t id,enum controller_profile profile,const struct controller_binding *bindings)
{(void)unused;return (enum mapping_save_result)save_controller(id,profile,bindings);}
static void option_service(void *unused,enum console_action action){(void)unused;platform_option(action);}
static void console_init(void)
{
    struct console_game games[LAUNCHER_MAX];char ids[LAUNCHER_MAX][64],names[LAUNCHER_MAX][64];
    int count=catalog_count();if(count>LAUNCHER_MAX)count=LAUNCHER_MAX;
    for(int i=0;i<count;i++) {
        catalog_name(i,names[i],sizeof(names[i]));catalog_id(i,ids[i],sizeof(ids[i]));
        games[i]=(struct console_game){ids[i],names[i],catalog_diagnostic(i)!=0};
    }
    console.services=(struct console_services){.load=load_service,.unload=unload_service,.save_keyboard=mapping_service,.action=option_service,.controller_info=controller_info_service,.save_controller=save_controller_service};
    chirky_console_catalog(&console,games,count,CONSOLE_CAN_FULLSCREEN|CONSOLE_CAN_SOUND);
}
/* Stable browser diagnostics; internal screen ordinals are not an ABI. */
EMSCRIPTEN_KEEPALIVE int web_console_state(void)
{
    const int states[]={0,1,2,2,3,6,4,5};return states[chirky_console_screen(&console)];
}
EMSCRIPTEN_KEEPALIVE int web_capture_keyboard(void)
{return console.setup.active?(console.setup.keyboard?1:0):-1;}
EMSCRIPTEN_KEEPALIVE void web_capture(int kind,unsigned code,int direction)
{if(console.setup.keyboard)chirky_console_capture(&console,(struct controller_binding){kind,code,direction});}
EMSCRIPTEN_KEEPALIVE void web_controller_press(unsigned id){chirky_console_controller_press(&console,id);}
EMSCRIPTEN_KEEPALIVE unsigned web_mapping_controller(void){return console.setup.active && !console.setup.keyboard?console.setup.controller_id:0;}
EMSCRIPTEN_KEEPALIVE void web_capture_controller(unsigned id,int kind,unsigned code,int direction)
{chirky_console_capture_controller(&console,id,(struct controller_binding){kind,code,direction});}
EMSCRIPTEN_KEEPALIVE void web_console_launch(int index)
{
    if(index<0)chirky_console_home(&console,false);
    else if(index<catalog_count())chirky_console_launch(&console,index,catalog_diagnostic(index)!=0);
}
EMSCRIPTEN_KEEPALIVE void web_console_pause(void){chirky_console_pause(&console);}
EMSCRIPTEN_KEEPALIVE int web_console_tick(unsigned mask,unsigned keyboard,unsigned pad,int key_held,int pad_held,int cancel,int pad_buttons,unsigned options)
{
    (void)keyboard;(void)pad;
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        input.buttons[i]=(mask&(1u<<i))!=0;
        input.button_pressed[i]=input.buttons[i] && !(previous&(1u<<i));
    }
    previous=mask;
    struct console_input frame={.logical=&input,.options=options,.keyboard_held=key_held!=0,.controller_held=pad_held!=0,
        .cancel=cancel!=0,.controller_buttons=(unsigned)pad_buttons,.now_us=(uint64_t)(emscripten_get_now()*1000)};
    return chirky_console_update(&console,&frame);
}
static void console_render(void)
{
    char pad[96],key[96];raw_names(0,pad,sizeof(pad));raw_names(1,key,sizeof(key));
    chirky_console_render(&console,&api,&art,&input,pad,key);
}
