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
    c->menu_offset=c->menu_velocity=0;
}
static void apply_display(struct chirky_console *c)
{ if(c->services.display)c->services.display(c->services.context,&c->display); }
static void leave_display(struct chirky_console *c)
{ if(c->display_settings){c->display=c->saved_display;apply_display(c);c->display_settings=false;} }
void chirky_console_home(struct chirky_console *c,bool settings)
{
    leave_display(c);
    c->loading=c->game_active=c->paused=c->setup.active=c->controller_settings=c->input_test=false;
    c->controller_selecting=c->controller_options=false;c->controller_id=0;
    c->settings_menu=settings;c->pause_option=0;c->controller_menu_chord_frames=0;
    c->menu_offset=c->menu_velocity=0;
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
void chirky_console_controller_press(struct chirky_console *c,uint32_t id)
{
    if(!c->controller_selecting || c->ui_wait_release || !id || !c->services.controller_info)return;
    if(c->services.controller_info(c->services.context,id,c->controller_name,sizeof(c->controller_name))<0)return;
    c->settings_message="";c->controller_id=id;c->controller_selecting=false;c->controller_options=true;c->controller_option=0;
    c->menu_offset=c->menu_velocity=0;chirky_console_block(c);
}
void chirky_console_capture_controller(struct chirky_console *c,uint32_t id,struct controller_binding binding)
{
    if(!c->setup.keyboard && c->setup.controller_id==id && id)chirky_console_capture(c,binding);
}
static const char *mapping_message(enum mapping_save_result result)
{
    return result==MAPPING_SAVED?"BUTTONS SAVED":result==MAPPING_CONFLICT?
        "KEY USED BY OTHER PLAYER":"SAVE FAILED - NOTHING CHANGED";
}
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
    if(before==SCREEN_LAUNCHER || before==SCREEN_SETTINGS || before==SCREEN_INPUT || before==SCREEN_DISPLAY) {
        /* Two fixed substeps per console tick: continuous velocity on reversals,
           gentle settling, and no render-rate-dependent navigation. */
        for(int step=0;step<2;step++) {
            c->menu_velocity+=(-230*c->menu_offset-23*c->menu_velocity)/120;
            c->menu_offset+=c->menu_velocity/120;
        }
        if(c->menu_offset>-.0008f && c->menu_offset<.0008f &&
           c->menu_velocity>-.009f && c->menu_velocity<.009f)
            c->menu_offset=c->menu_velocity=0;
    }
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
    if((c->controller_options || (c->setup.active && !c->setup.keyboard)) && c->services.controller_info &&
       c->services.controller_info(c->services.context,c->controller_id,c->controller_name,sizeof(c->controller_name))<0) {
        c->setup.active=c->controller_options=false;c->controller_selecting=true;c->controller_id=0;
        c->settings_message="CONTROLLER DISCONNECTED";chirky_console_block(c);
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
                enum mapping_save_result result=MAPPING_FAILED;
                if(c->setup.keyboard && c->services.save_keyboard)
                    result=c->services.save_keyboard(c->services.context,c->setup.keyboard_profile,c->setup.pending);
                else if(!c->setup.keyboard && c->services.save_controller)
                    result=c->services.save_controller(c->services.context,c->setup.controller_id,c->setup.controller_profile,c->setup.pending);
                c->settings_message=mapping_message(result);c->setup.active=false;
            }
        }
    } else if(c->controller_selecting) {
        if(frame->cancel || input->button_pressed[CHIRKY_BUTTON_SECONDARY])c->controller_selecting=false;
    } else if(c->controller_options) {
        int choice=console_menu_update(&c->controller_option,4,input,frame->cancel);
        if(choice==-2 || choice==3)c->controller_options=false;
        else if(choice==0) {
            enum mapping_save_result result=c->services.save_controller?
                c->services.save_controller(c->services.context,c->controller_id,CONTROLLER_SNES,NULL):MAPPING_FAILED;
            c->settings_message=mapping_message(result);
            if(result==MAPPING_SAVED)c->controller_options=false;
        } else if(choice==1 || choice==2) {
            int profile=c->services.controller_info(c->services.context,c->controller_id,c->controller_name,sizeof(c->controller_name));
            setup_begin(&c->setup,false);c->setup.controller_id=c->controller_id;
            c->setup.controller_profile=choice==1?CONTROLLER_GENERIC:(enum controller_profile)profile;
            c->settings_message="";
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
        if(direction)c->menu_offset+=console_menu_move(&c->display_option,6,direction);
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
        int previous=c->selected_option;
        int choice=console_menu_update(&c->selected_option,5,input,frame->cancel);
        c->menu_offset+=c->selected_option-previous;
        if(choice==-2 || choice==4)c->controller_settings=false;
        else if(choice==2){c->input_test=true;c->controller_menu_chord_frames=0;}
        else if(choice==0){c->controller_selecting=true;c->settings_message="";}
        else if(choice==1 || choice==3){setup_begin(&c->setup,true);c->setup.keyboard_profile=choice==3?2:1;
            c->settings_message="";c->controller_menu_chord_frames=0;}
    } else if(c->settings_menu) {
        int previous=c->settings_option;
        int count=launcher_count(&c->launcher,true),choice=console_menu_update(&c->settings_option,count+1,input,frame->cancel);
        c->menu_offset+=c->settings_option-previous;
        if(choice==-2 || choice==count)c->settings_menu=false;
        else if(choice>=0)choose(c,launcher_at(&c->launcher,true,choice)->action,true);
    } else {
        int previous=c->selected_game;
        int choice=console_menu_update(&c->selected_game,launcher_count(&c->launcher,false),input,false);
        c->menu_offset+=c->selected_game-previous;
        if(choice>=0)choose(c,launcher_at(&c->launcher,false,choice)->action,false);
    }
    if(before!=chirky_console_screen(c)){c->menu_offset=c->menu_velocity=0;chirky_console_block(c);}
    return false;
}
void chirky_console_draw_display(const struct chirky_console *c,const struct chirky_host_api *api)
{
    int w=api->screen_width,h=api->screen_height;
    launcher_rect(api,0,0,w,1,launcher_gold,0,h);launcher_rect(api,0,h-1,w,1,launcher_gold,0,h);
    launcher_rect(api,0,0,1,h,launcher_gold,0,h);launcher_rect(api,w-1,0,1,h,launcher_gold,0,h);
    const char *names[]={"Side margin","Top / bottom margin","Horizontal offset","Vertical offset"};
    int values[]={c->display.x,c->display.y,c->display.offset_x,c->display.offset_y};
    char rows[4][64];const char *labels[6];
    for(int i=0;i<4;i++){snprintf(rows[i],sizeof(rows[i]),"%s: %d",names[i],values[i]);labels[i]=rows[i];}labels[4]="Save";labels[5]="Back";
    console_scroll_list(api,labels,NULL,6,c->display_option,c->menu_offset,45,h-78);
    char direction[52],hint[96];
    chirky_direction_label(api,CHIRKY_BUTTON_LEFT,2,direction,sizeof(direction));
    snprintf(hint,sizeof(hint),"%s adjusts - keep edges visible",direction);
    console_menu_text(api,12,34,c->settings_message && *c->settings_message?c->settings_message:hint,1,201,191,173);
    console_menu_footer(api,true);
}
void chirky_console_render(struct chirky_console *c,const struct chirky_host_api *api,struct splash_art *art,unsigned pad,unsigned key,const char *pad_names,const char *key_names)
{
    switch(chirky_console_screen(c)) {
    case SCREEN_PAUSE:console_draw_pause_menu(api,c->pause_option);break;
    case SCREEN_GAME:break;
    case SCREEN_DISPLAY:console_page(api,art,"Display area");chirky_console_draw_display(c,api);break;
    case SCREEN_INPUT:case SCREEN_SETUP:case SCREEN_TEST:
        console_page(api,art,c->controller_selecting?"Choose controller":c->controller_options && !c->setup.active?"Controller setup":c->setup.active?(c->setup.keyboard?
            (c->setup.keyboard_profile==2?"Map keyboard P2":"Map keyboard P1"):
            "Map controller"):c->input_test?"Test buttons":"Input settings");
        if(c->controller_selecting)console_draw_controller_selection(api,c->settings_message);
        else if(c->controller_options && !c->setup.active)
            console_draw_controller_profile(api,c->controller_name,c->controller_option,c->settings_message);
        else console_draw_controller_settings(api,&c->setup,c->input_test,c->selected_option,c->settings_message,c->menu_offset);
        if(c->input_test)console_draw_live_inputs(api,pad,key,pad_names,key_names);
        break;
    case SCREEN_SETTINGS:console_page(api,art,"Settings");console_draw_settings_menu(api,&c->launcher,c->settings_option,c->menu_offset);break;
    default:console_draw_launcher(api,art,&c->launcher,c->selected_game,c->menu_offset);break;
    }
    if(c->loading)console_draw_loading(api);
}
