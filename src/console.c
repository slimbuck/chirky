#include "console.h"
#include "console_ui.h"
#include "viewport.h"

enum console_screen chirky_console_screen(const struct chirky_console *c)
{
    if(c->setup.active)return SCREEN_SETUP;
    if(c->game_active)return c->paused?SCREEN_PAUSE:SCREEN_GAME;
    if(c->display_settings)return SCREEN_DISPLAY;
    if(c->controller_settings)return c->input_test?SCREEN_TEST:SCREEN_INPUT;
    return c->settings_menu?SCREEN_SETTINGS:SCREEN_LAUNCHER;
}
void chirky_console_block(struct chirky_console *c)
{ c->ui_wait_release=true;chirky_gate_begin(&c->transition_gate); }
void chirky_console_catalog(struct chirky_console *c,const struct console_game *games,int count,unsigned caps)
{
    c->capabilities=caps;c->launcher=(struct launcher_config){0};
    for(int i=0;i<count;i++)if(!games[i].diagnostic)
        launcher_add(&c->launcher,games[i].id,games[i].name,i,false);
    launcher_add(&c->launcher,"settings","Settings",CONSOLE_SETTINGS,false);
    if(caps&CONSOLE_CAN_POWER)launcher_add(&c->launcher,"power","Power Down",CONSOLE_POWER,false);
    launcher_add(&c->launcher,"input","Input Settings",CONSOLE_INPUT,true);
    if(caps&CONSOLE_CAN_DISPLAY)launcher_add(&c->launcher,"display","Display Area",CONSOLE_DISPLAY,true);
    bool first=true;
    for(int i=0;i<count;i++)if(games[i].diagnostic) {
        /* Preserve the original launcher configuration's diagnostic key. */
        launcher_add(&c->launcher,first?"hardware":games[i].id,games[i].name,i,true);first=false;
    }
    if(caps&CONSOLE_CAN_FULLSCREEN)launcher_add(&c->launcher,"fullscreen","Full Screen",CONSOLE_FULLSCREEN,true);
    if(caps&CONSOLE_CAN_SOUND)launcher_add(&c->launcher,"sound","Mute / Unmute",CONSOLE_SOUND,true);
    c->selected_game=c->settings_option=0;
}
static void apply_display(struct chirky_console *c)
{ if(c->services.display)c->services.display(c->services.context,&c->display); }
static void leave_display(struct chirky_console *c)
{ if(c->display_settings){c->display=c->saved_display;apply_display(c);c->display_settings=false;} }
void chirky_console_home(struct chirky_console *c,bool settings)
{
    leave_display(c);
    c->loading=c->game_active=c->paused=c->setup.active=c->controller_settings=c->input_test=false;
    c->settings_menu=settings;c->pause_option=0;c->controller_menu_chord_frames=0;
    chirky_console_block(c);
    if(c->services.unload)c->services.unload(c->services.context);
}
bool chirky_console_launch(struct chirky_console *c,int index,bool diagnostic)
{
    chirky_console_home(c,diagnostic);
    c->diagnostic=diagnostic;c->loading=true;
    for(int i=0;i<launcher_count(&c->launcher,diagnostic);i++)
        if(launcher_at(&c->launcher,diagnostic,i)->action==index) {
            if(diagnostic)c->settings_option=i;else c->selected_game=i;
        }
    if(!c->services.load || !c->services.load(c->services.context,index)) {
        chirky_console_loaded(c,false);return false;
    }
    return true;
}
void chirky_console_loaded(struct chirky_console *c,bool success)
{
    c->loading=false;c->game_active=success;c->paused=false;
    if(!success && c->services.unload)c->services.unload(c->services.context);
    chirky_console_block(c);
}
void chirky_console_open(struct chirky_console *c,enum console_action action)
{
    if(action==CONSOLE_DISPLAY && !(c->capabilities&CONSOLE_CAN_DISPLAY))return;
    chirky_console_home(c,true);
    c->settings_message="";
    if(action==CONSOLE_SETTINGS)c->settings_option=0;
    for(int i=0;i<launcher_count(&c->launcher,true);i++)
        if(launcher_at(&c->launcher,true,i)->action==(int)action)c->settings_option=i;
    if(action==CONSOLE_INPUT){c->controller_settings=true;c->selected_option=0;}
    if(action==CONSOLE_DISPLAY){c->display_settings=true;c->display_option=0;c->saved_display=c->display;}
}
void chirky_console_pause(struct chirky_console *c)
{
    if(c->game_active && !c->diagnostic && !c->paused) {
        c->paused=true;c->pause_option=0;chirky_console_block(c);
    }
}
void chirky_console_capture(struct chirky_console *c,struct controller_binding binding)
{ if(!c->ui_wait_release)chirky_setup_offer(&c->setup,binding); }
void chirky_console_timing(struct chirky_console *c,bool start,uint64_t now)
{
    if(!(c->capabilities&CONSOLE_CAN_TIMING))return;
    if(!start){c->timing_start_held=c->timing_start_toggled=false;return;}
    if(!c->timing_start_held){c->timing_start_held=true;c->timing_start_us=now;}
    if(!c->timing_start_toggled && now-c->timing_start_us>=2000000u) {
        c->timing_start_toggled=true;
        if(c->services.action)c->services.action(c->services.context,CONSOLE_TIMING);
    }
}
static void choose(struct chirky_console *c,int action,bool settings)
{
    if(action>=0)chirky_console_launch(c,action,settings);
    else if(action==CONSOLE_SETTINGS || action==CONSOLE_INPUT || action==CONSOLE_DISPLAY)chirky_console_open(c,action);
    else if(c->services.action)c->services.action(c->services.context,(enum console_action)action);
}
bool chirky_console_update(struct chirky_console *c,const struct console_input *frame)
{
    const struct chirky_input *input=frame->logical;
    enum console_screen before=chirky_console_screen(c);
    chirky_console_timing(c,input->buttons[CHIRKY_BUTTON_START],frame->now_us);
    if(c->loading){if(frame->cancel)chirky_console_home(c,c->settings_menu);return false;}
    /* Recovery must remain available while Menu's pause transition waits for
       release; otherwise holding Start+Menu can block its own recovery chord. */
    if(c->game_active) {
        bool recovery=frame->recovery || (input->buttons[CHIRKY_BUTTON_START] && input->buttons[CHIRKY_BUTTON_MENU]);
        c->controller_menu_chord_frames=recovery?c->controller_menu_chord_frames+1:0;
        if(frame->cancel || c->controller_menu_chord_frames>=60) {
            chirky_console_home(c,c->settings_menu);return false;
        }
    }
    if(c->ui_wait_release && !frame->cancel) {
        bool neutral=!frame->keyboard_held && !frame->controller_held;
        for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)neutral &= !input->buttons[i] && !input->button_pressed[i];
        chirky_gate_accept(&c->transition_gate,neutral);c->ui_wait_release=c->transition_gate.blocked;
        return false;
    }
    if(c->setup.active) {
        c->controller_menu_chord_frames=frame->controller_buttons>=2?c->controller_menu_chord_frames+1:0;
        if(frame->cancel || c->controller_menu_chord_frames>=60) {
            c->setup.active=false;c->settings_message="CANCELLED - NOTHING CHANGED";c->controller_menu_chord_frames=0;
        } else {
            setup_release(&c->setup,c->setup.keyboard?!frame->keyboard_held:!frame->controller_held);
            if(c->setup.complete) {
                bool ok=c->services.save_mapping && c->services.save_mapping(c->services.context,c->setup.keyboard,c->setup.pending);
                c->settings_message=ok?"BUTTONS SAVED":"SAVE FAILED - NOTHING CHANGED";c->setup.active=false;
            }
        }
    } else if(c->game_active) {
        if(c->diagnostic && input->button_pressed[CHIRKY_BUTTON_MENU])
            chirky_console_home(c,c->settings_menu);
        else if(c->paused) {
            int choice=console_menu_update(&c->pause_option,2,input,input->button_pressed[CHIRKY_BUTTON_MENU]);
            if(choice==-2 || choice==0)c->paused=false;
            else if(choice==1)chirky_console_home(c,false);
        } else if(input->button_pressed[CHIRKY_BUTTON_MENU])chirky_console_pause(c);
        else return true;
    } else if(c->display_settings) {
        int direction=(int)input->button_pressed[CHIRKY_BUTTON_DOWN]-(int)input->button_pressed[CHIRKY_BUTTON_UP];
        if(direction)c->display_option=(c->display_option+direction+6)%6;
        int delta=(int)input->button_pressed[CHIRKY_BUTTON_RIGHT]-(int)input->button_pressed[CHIRKY_BUTTON_LEFT];
        int *values[]={&c->display.x,&c->display.y,&c->display.offset_x,&c->display.offset_y};
        int max[]={CHIRKY_FRAMEBUFFER_WIDTH/10,CHIRKY_FRAMEBUFFER_HEIGHT/10,c->display.x,c->display.y};
        if(c->display_option<4 && delta) {
            int i=c->display_option,min=i<2?0:-max[i];*values[i]+=delta;
            if(*values[i]<min)*values[i]=min;
            if(*values[i]>max[i])*values[i]=max[i];
            if(c->display.offset_x>c->display.x)c->display.offset_x=c->display.x;
            if(c->display.offset_x<-c->display.x)c->display.offset_x=-c->display.x;
            if(c->display.offset_y>c->display.y)c->display.offset_y=c->display.y;
            if(c->display.offset_y<-c->display.y)c->display.offset_y=-c->display.y;
            apply_display(c);
        }
        if(frame->cancel || input->button_pressed[CHIRKY_BUTTON_SECONDARY] || (!direction && input->button_pressed[CHIRKY_BUTTON_PRIMARY] && c->display_option==5))leave_display(c);
        else if(!direction && input->button_pressed[CHIRKY_BUTTON_PRIMARY] && c->display_option==4) {
            if(c->services.save_display && c->services.save_display(c->services.context))c->display_settings=false;
            else c->settings_message="SAVE FAILED - TRY AGAIN";
        }
    } else if(c->controller_settings && c->input_test) {
        c->controller_menu_chord_frames=input->buttons[CHIRKY_BUTTON_SECONDARY]?c->controller_menu_chord_frames+1:0;
        if(frame->cancel || c->controller_menu_chord_frames>=60){c->input_test=false;c->controller_menu_chord_frames=0;}
    } else if(c->controller_settings) {
        int choice=console_menu_update(&c->selected_option,4,input,frame->cancel);
        if(choice==-2 || choice==3)c->controller_settings=false;
        else if(choice==2){c->input_test=true;c->controller_menu_chord_frames=0;}
        else if(choice>=0){setup_begin(&c->setup,choice==1);c->settings_message="";c->controller_menu_chord_frames=0;}
    } else if(c->settings_menu) {
        int count=launcher_count(&c->launcher,true),choice=console_menu_update(&c->settings_option,count+1,input,frame->cancel);
        if(choice==-2 || choice==count)c->settings_menu=false;
        else if(choice>=0)choose(c,launcher_at(&c->launcher,true,choice)->action,true);
    } else {
        int choice=console_menu_update(&c->selected_game,launcher_count(&c->launcher,false),input,false);
        if(choice>=0)choose(c,launcher_at(&c->launcher,false,choice)->action,false);
        else if(input->button_pressed[CHIRKY_BUTTON_MENU])chirky_console_open(c,CONSOLE_SETTINGS);
    }
    if(before!=chirky_console_screen(c))chirky_console_block(c);
    return false;
}
void chirky_console_draw_display(const struct chirky_console *c,const struct chirky_host_api *api)
{
    int w=api->screen_width,h=api->screen_height;
    api->fill_rect(api->context,0,0,w,1,40,175,212);api->fill_rect(api->context,0,h-1,w,1,40,175,212);
    api->fill_rect(api->context,0,0,1,h,40,175,212);api->fill_rect(api->context,w-1,0,1,h,40,175,212);
    console_menu_text(api,10,h-20,"DISPLAY AREA",2,238,240,232);
    console_menu_text(api,10,h-47,"KEEP ALL FOUR EDGES VISIBLE",1,112,160,170);
    const char *names[]={"SIDE MARGIN","TOP BOTTOM MARGIN","HORIZONTAL","VERTICAL"};
    int values[]={c->display.x,c->display.y,c->display.offset_x,c->display.offset_y};
    for(int i=0;i<4;i++){char line[64];snprintf(line,sizeof(line),"%s - %d",names[i],values[i]);console_menu_row(api,h-70-i*16,line,c->display_option==i);}
    console_menu_row(api,h-134,"SAVE",c->display_option==4);console_menu_row(api,h-150,"BACK",c->display_option==5);
    console_menu_text(api,10,25,c->settings_message && *c->settings_message?c->settings_message:"LEFT RIGHT ADJUST - UP DOWN MOVE",1,112,160,170);
    console_menu_footer(api,true);
}
void chirky_console_render(struct chirky_console *c,const struct chirky_host_api *api,struct splash_art *art,unsigned pad,unsigned key,const char *pad_names,const char *key_names)
{
    switch(chirky_console_screen(c)) {
    case SCREEN_PAUSE:console_draw_pause_menu(api,c->pause_option);break;
    case SCREEN_GAME:break;
    case SCREEN_DISPLAY:chirky_console_draw_display(c,api);break;
    case SCREEN_INPUT:case SCREEN_SETUP:case SCREEN_TEST:
        console_draw_controller_settings(api,&c->setup,c->input_test,c->selected_option,c->settings_message);
        console_draw_live_inputs(api,pad,key,pad_names,key_names);break;
    case SCREEN_SETTINGS:console_draw_settings_menu(api,&c->launcher,c->settings_option);break;
    default:console_draw_launcher(api,art,&c->launcher,c->selected_game);break;
    }
    if(c->loading)console_menu_text(api,12,api->screen_height/2,"LOADING",2,250,248,236);
}
