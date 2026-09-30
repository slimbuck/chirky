#ifndef CHIRKY_CONSOLE_UI_H
#define CHIRKY_CONSOLE_UI_H
#include "chirky.h"
#include "input_setup.h"
#include "launcher_config.h"
#include "launcher_wordmark.h"
#include "splash_art.h"
#include "drawing.h"

static const char *const console_button_names[]={"LEFT","RIGHT","UP","DOWN","PRIMARY","SECONDARY","START","MENU"};
/* Every menu uses Primary to confirm and Secondary to go back. A movement
   consumes the frame so simultaneous directions/confirmation cannot spill. */
static inline int console_menu_update(int *selected,int count,const struct chirky_input *input,bool back)
{
    int direction=(int)input->button_pressed[CHIRKY_BUTTON_DOWN]-(int)input->button_pressed[CHIRKY_BUTTON_UP];
    if(count<=0)return -1;
    if(direction){*selected=(*selected+direction+count)%count;return -1;}
    if(back || input->button_pressed[CHIRKY_BUTTON_SECONDARY])return -2;
    return input->button_pressed[CHIRKY_BUTTON_PRIMARY]?*selected:-1;
}
static inline void console_glyphs(const struct chirky_host_api *api,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    chirky_draw_text(api,x,y,value,scale,r,g,b);
}
static inline void console_menu_text(const struct chirky_host_api *api, int x, int y, const char *value, int scale,
                      unsigned char r, unsigned char g, unsigned char b)
{
    char clipped[128];
    int columns=(api->screen_width-x-8)/(6*scale);
    if (columns<0) columns=0;
    if (columns>127) columns=127;
    snprintf(clipped,sizeof(clipped),"%.*s",columns,value);
    console_glyphs(api,x,y,clipped,scale,r,g,b);
}

static inline void console_menu_row(const struct chirky_host_api *api, int y, const char *label, bool selected)
{
    api->fill_rect(api->context,8,y-13,api->screen_width-16,20,selected?28:14,selected?74:30,selected?84:40);
    api->fill_rect(api->context,12,y-9,3,10,selected?244:70,selected?194:110,70);
    console_menu_text(api,22,y,label,1,selected?250:170,selected?248:185,selected?236:190);
}

static inline void console_menu_footer(const struct chirky_host_api *api, bool can_go_back)
{
    char confirm[32],back[32],line[80]; api->button_label(api->context,CHIRKY_BUTTON_PRIMARY,confirm,sizeof(confirm));
    api->button_label(api->context,CHIRKY_BUTTON_SECONDARY,back,sizeof(back));
    if (can_go_back) snprintf(line,sizeof(line),"%s SELECT - %s BACK",confirm,back);
    else snprintf(line,sizeof(line),"%s SELECT - UP DOWN MOVE",confirm);
    console_menu_text(api,10,14,line,1,112,160,170);
}

static inline void console_draw_launcher(const struct chirky_host_api *api, const struct splash_art *art, const struct launcher_config *launcher, int selected_game)
{
    splash_draw(art,api);
    int height=api->screen_height;
    launcher_wordmark(api);
    int count=launcher_count(launcher,false), first=selected_game<4?0:selected_game-3;
    for (int row=0;row<4 && first+row<count;row++) {
        int index=first+row;
        const char *label=launcher_at(launcher,false,index)->label;
        int y=height-83-row*24;
        bool selected=index==selected_game;
        int width=api->screen_width*2/3;
        api->fill_rect(api->context,8,y-13,width,20,selected?28:5,selected?74:17,selected?84:23);
        api->fill_rect(api->context,12,y-9,3,10,selected?244:40,selected?194:85,selected?70:91);
        char visible[64];
        snprintf(visible,sizeof(visible),"%.*s",(width-20)/6,label);
        console_menu_text(api,22,y,visible,1,selected?250:170,selected?248:185,selected?236:190);
    }
    if (first+4<count) console_menu_text(api,10,30,"MORE BELOW",1,112,160,170);
    else if (first>0) console_menu_text(api,10,30,"MORE ABOVE",1,112,160,170);
    api->fill_rect(api->context,8,5,api->screen_width-16,18,5,17,23);
    console_menu_footer(api,false);
}

static inline void console_draw_settings_menu(const struct chirky_host_api *api, const struct launcher_config *launcher, int settings_option)
{
    int height=api->screen_height;
    api->fill_rect(api->context,8,height-7,api->screen_width-16,3,40,175,212);
    console_menu_text(api,10,height-23,"SETTINGS",3,238,240,232);
    int count=launcher_count(launcher,true),first=settings_option<4?0:settings_option-3;
    for(int row=0;row<4 && first+row<=count;row++) {
        int index=first+row;
        console_menu_row(api,height-83-row*24,index==count?"BACK":launcher_at(launcher,true,index)->label,index==settings_option);
    }
    console_menu_footer(api,true);
}

static inline void console_draw_live_inputs(const struct chirky_host_api *api, unsigned pad_mask, unsigned key_mask, const char *pad_line, const char *key_line)
{
    int cell=(api->screen_width-20)/4;
    console_menu_text(api,10,87,"PAD GREEN / KEY GOLD",1,155,175,180);
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        bool pad=(pad_mask&(1u<<i))!=0;
        bool key=(key_mask&(1u<<i))!=0;
        int x=10+(i%4)*cell, y=61-(i/4)*21;
        api->fill_rect(api->context,x,y,cell-2,18,pad||key?45:22,pad||key?65:32,pad||key?58:38);
        console_menu_text(api,x+2,y+14,console_button_names[i],1,pad||key?250:130,pad||key?245:150,pad||key?220:157);
        api->fill_rect(api->context,x+2,y+2,(cell-6)/2,3,pad?93:40,pad?220:60,pad?153:60);
        api->fill_rect(api->context,x+cell/2,y+2,(cell-6)/2,3,key?250:60,key?196:55,key?75:40);
    }
    console_menu_text(api,10,38,pad_line,1,93,220,153);
    console_menu_text(api,10,26,key_line,1,250,196,75);
}

static inline void console_draw_controller_settings(const struct chirky_host_api *api, const struct binding_setup *setup, bool input_test, int selected_option, const char *message)
{
    int height=api->screen_height;
    if (setup->active) {
        console_menu_text(api,10,height-18,setup->keyboard?"MAP KEYBOARD":"MAP CONTROLLER",2,238,240,232);
        int step=setup->step<CHIRKY_BUTTON_COUNT?setup->step:CHIRKY_BUTTON_COUNT-1;
        console_menu_text(api,10,height-47,console_button_names[step],3,244,194,70);
        char line[80];
        snprintf(line,sizeof(line),"%d OF %d - %s",step+1,CHIRKY_BUTTON_COUNT,
            setup->wait_release?"RELEASE ALL INPUTS":"PRESS NOW");
        console_menu_text(api,10,height-74,line,1,112,180,190);
        console_menu_text(api,10,99,setup->message,1,244,160,70);
        console_menu_text(api,10,12,setup->keyboard?"F1 CANCEL - SAVES AFTER ALL 8":"HOLD TWO BUTTONS TO CANCEL",1,112,160,170);
    } else if (input_test) {
        console_menu_text(api,10,height-22,"TEST BUTTONS",2,238,240,232);
        console_menu_text(api,10,height-49,"PRESS ANY KEYS OR BUTTONS",1,112,180,190);
        console_menu_text(api,10,height-64,"BOTH SOURCES LIGHT UP BELOW",1,112,180,190);
        console_menu_text(api,10,12,"HOLD SECONDARY 1 SECOND TO RETURN",1,112,160,170);
    } else {
        console_menu_text(api,10,height-19,"INPUT SETTINGS",2,238,240,232);
        console_menu_text(api,10,height-35,message?message:"",1,244,194,70);
        const char *labels[]={"MAP CONTROLLER","MAP KEYBOARD","TEST BUTTONS","BACK"};
        for (int i=0;i<4;i++) console_menu_row(api,height-48-i*16,labels[i],selected_option==i);
        console_menu_text(api,10,12,"PRIMARY SELECT - SECONDARY BACK",1,112,160,170);
    }
}

static inline void console_draw_pause_menu(const struct chirky_host_api *api, int pause_option)
{
    const int width=216,height=112;
    int x=(api->screen_width-width)/2,y=(api->screen_height-height)/2;
    api->fill_rect(api->context,x+3,y-3,width,height,4,8,11);
    api->fill_rect(api->context,x,y,width,height,40,175,212);
    api->fill_rect(api->context,x+1,y+1,width-2,height-2,12,22,28);
    console_menu_text(api,x+12,y+89,"PAUSED",2,238,240,232);
    api->fill_rect(api->context,x+12,y+77,width-24,1,40,75,85);
    const char *labels[]={"CONTINUE GAME","RETURN TO LAUNCHER"};
    for(int i=0;i<2;i++) {
        bool selected=pause_option==i;
        int row=y+61-i*24;
        api->fill_rect(api->context,x+8,row-13,width-16,20,selected?28:14,selected?74:30,selected?84:40);
        api->fill_rect(api->context,x+12,row-9,3,10,selected?244:70,selected?194:110,70);
        console_menu_text(api,x+22,row,labels[i],1,selected?250:170,selected?248:185,selected?236:190);
    }
    console_menu_text(api,x+12,y+10,"PRIMARY OK - SECONDARY BACK",1,112,160,170);
}

#endif
