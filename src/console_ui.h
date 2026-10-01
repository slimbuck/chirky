#ifndef CHIRKY_CONSOLE_UI_H
#define CHIRKY_CONSOLE_UI_H
#include "chirky.h"
#include "input_setup.h"
#include "launcher_config.h"
#include "launcher_wordmark.h"
#include "splash_art.h"
#include "drawing.h"
#include "launcher_font.h"

static const char *const console_button_names[]={"Left","Right","Up","Down","Primary","Secondary","Start","Menu"};
static inline int console_menu_move(int *selected,int count,int direction)
{
    int next=*selected+direction;
    if(next<0 || next>=count)return 0;
    int moved=next-*selected;*selected=next;return moved;
}
/* Every menu uses Primary to confirm and Secondary to go back. A movement
   consumes the frame so simultaneous directions/confirmation cannot spill. */
static inline int console_menu_update(int *selected,int count,const struct chirky_input *input,bool back)
{
    int direction=(int)input->button_pressed[CHIRKY_BUTTON_DOWN]-(int)input->button_pressed[CHIRKY_BUTTON_UP];
    if(count<=0)return -1;
    if(direction){console_menu_move(selected,count,direction);return -1;}
    if(back || input->button_pressed[CHIRKY_BUTTON_SECONDARY])return -2;
    return input->button_pressed[CHIRKY_BUTTON_PRIMARY]?*selected:-1;
}
struct launcher_colour { unsigned char r,g,b; };
static const struct launcher_colour launcher_navy={37,36,51},launcher_cream={244,233,202},launcher_gold={244,184,69};
static inline struct launcher_colour launcher_mix(struct launcher_colour a,struct launcher_colour b,int alpha)
{
    return (struct launcher_colour){(a.r*alpha+b.r*(255-alpha))/255,(a.g*alpha+b.g*(255-alpha))/255,(a.b*alpha+b.b*(255-alpha))/255};
}
static inline void launcher_rect(const struct chirky_host_api *api,int x,int y,int w,int h,struct launcher_colour colour,int low,int high)
{
    if(y<low){h-=low-y;y=low;}if(y+h>high)h=high-y;
    if(chirky_clip_rect(api,&x,&y,&w,&h))api->fill_rect(api->context,x,y,w,h,colour.r,colour.g,colour.b);
}
static inline void launcher_round(const struct chirky_host_api *api,int x,int y,int w,int h,struct launcher_colour colour,int low,int high)
{
    launcher_rect(api,x+4,y,w-8,h,colour,low,high);
    launcher_rect(api,x+2,y+1,w-4,h-2,colour,low,high);
    launcher_rect(api,x+1,y+2,w-2,h-4,colour,low,high);
    launcher_rect(api,x,y+4,w,h-8,colour,low,high);
}
static inline int launcher_text_width(const char *text,int font)
{
    int width=0;for(;*text;text++){unsigned c=(unsigned char)*text;if(c<32 || c>126)c='?';width+=launcher_font_advance[font][c-32];}return width;
}
static inline void launcher_text(const struct chirky_host_api *api,int x,int top,const char *text,int font,
    struct launcher_colour colour,struct launcher_colour background,int right,int low,int high)
{
    for(;*text;text++) {
        unsigned c=(unsigned char)*text;if(c<32 || c>126)c='?';c-=32;
        if(x+launcher_font_advance[font][c]>right)break;
        for(int row=0;row<20;row++)for(int col=0;col<16;) {
            unsigned coverage=(launcher_font_mask[font][c][row]>>(col*2))&3u;
            if(!coverage){col++;continue;}
            int end=col+1;while(end<16 && ((launcher_font_mask[font][c][row]>>(end*2))&3u)==coverage)end++;
            launcher_rect(api,x+col,top+2-row,end-col,1,launcher_mix(colour,background,(int)coverage*85),low,high);col=end;
        }
        x+=launcher_font_advance[font][c];
    }
}
static inline void launcher_mascot(const struct chirky_host_api *api,const struct splash_art *art,int x,int y)
{
    /* Runtime cell is authored at 64x64. Never scale to fit the viewport. */
    if(art->asset_requested) {
        if(!art->image || !api->draw_sprite || art->asset_api.asset_status(art->asset_api.context,art->image)!=CHIRKY_ASSET_READY)return;
        struct chirky_asset_view view=art->asset_api.asset_data(art->asset_api.context,art->image);
        if(view.width==64 && view.height==64)api->draw_sprite(api->context,art->image,x,y,64,64,0,0,64,64,255,255,255,255,false);
    } else if(art->pixels && art->width==64 && art->height==64) {
        for(int row=0;row<64;row++)for(int col=0;col<64;) {
            const unsigned char *p=art->pixels+(row*64+col)*3;int end=col+1;
            while(end<64 && !memcmp(p,art->pixels+(row*64+end)*3,3))end++;
            launcher_rect(api,x+col,y+63-row,end-col,1,(struct launcher_colour){p[0],p[1],p[2]},0,api->screen_height);col=end;
        }
    }
}
static const struct launcher_colour console_card={60,57,74},console_muted={201,191,173},console_shadow={23,23,35};

static inline void console_page(const struct chirky_host_api *api,const struct splash_art *art,const char *title)
{
    int h=api->screen_height,x=(api->screen_width-230)/2;
    launcher_rect(api,0,0,api->screen_width,h,launcher_navy,0,h);
    launcher_mascot(api,art,x,h-70);
    if(title)launcher_text(api,x+76,h-32,title,2,launcher_cream,launcher_navy,api->screen_width-8,0,h);
    else launcher_wordmark_at(api,x+76,h-32,2);
}
static inline void console_menu_text(const struct chirky_host_api *api,int x,int top,const char *text,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    launcher_text(api,x,top,text,scale>1?2:0,(struct launcher_colour){r,g,b},launcher_navy,api->screen_width-8,0,api->screen_height);
}
static inline void console_menu_footer(const struct chirky_host_api *api,bool can_go_back)
{
    char primary[32]="Primary",secondary[32]="Secondary",line[96];
    if(api->button_label){api->button_label(api->context,CHIRKY_BUTTON_PRIMARY,primary,sizeof(primary));api->button_label(api->context,CHIRKY_BUTTON_SECONDARY,secondary,sizeof(secondary));}
    if(can_go_back)snprintf(line,sizeof(line),"%s select   %s back",primary,secondary);
    else snprintf(line,sizeof(line),"Up/Down choose   %s select",primary);
    int x=(api->screen_width-launcher_text_width(line,0))/2;if(x<8)x=8;
    launcher_text(api,x,17,line,0,console_muted,launcher_navy,api->screen_width-8,0,api->screen_height);
}
static inline void console_draw_loading(const struct chirky_host_api *api)
{
    int w=launcher_text_width("Loading",0)+12,x=api->screen_width-w-4,y=api->screen_height-23;
    launcher_round(api,x,y,w,19,console_card,0,api->screen_height);
    launcher_text(api,x+6,y+16,"Loading",0,launcher_cream,console_card,api->screen_width-5,0,api->screen_height);
}
static inline void console_scroll_list(const struct chirky_host_api *api,const char *const *labels,int count,int selected,float offset,int low,int high)
{
    int width=api->screen_width-24,centre=(low+high)/2,middle=(int)-offset;
    for(int pass=0;pass<2;pass++) {
        if(pass==1) {
            launcher_round(api,12,centre-16,width,26,console_shadow,low,high);
            launcher_round(api,12,centre-13,width,26,launcher_gold,low,high);
        }
        for(int row=middle-3;row<=middle+3 && count>0;row++) {
            float distance=(row+offset)*30;
            int delta=(int)(distance+(distance<0?-.5f:.5f)),y=centre-delta;
            if(y+13<low || y-13>=high)continue;
            int index=selected+row;if(index<0 || index>=count)continue;
            int magnitude=delta<0?-delta:delta,alpha=255-magnitude*magnitude*255/3600;if(alpha<0)alpha=0;
            struct launcher_colour card=launcher_mix(console_card,launcher_navy,alpha);
            if(pass==0)launcher_round(api,12,y-13,width,26,card,low,high);
            else {
                int curve=magnitude*magnitude/400;if(curve>5)curve=5;
                struct launcher_colour ink=launcher_mix(launcher_cream,launcher_navy,alpha);
                launcher_text(api,23+curve,y+8,labels[index],1,ink,card,api->screen_width-20,low,centre-13);
                launcher_text(api,23+curve,y+8,labels[index],1,ink,card,api->screen_width-20,centre+13,high);
                launcher_text(api,23+curve,y+8,labels[index],1,launcher_navy,launcher_gold,api->screen_width-20,centre-13,centre+13);
            }
        }
    }
}
static inline void console_draw_launcher(const struct chirky_host_api *api,const struct splash_art *art,const struct launcher_config *launcher,int selected,float offset)
{
    console_page(api,art,NULL);
    const char *labels[LAUNCHER_MAX];int count=launcher_count(launcher,false);
    for(int i=0;i<count;i++)labels[i]=launcher_at(launcher,false,i)->label;
    console_scroll_list(api,labels,count,selected,offset,28,api->screen_height-78);
    console_menu_footer(api,false);
}
static inline void console_draw_settings_menu(const struct chirky_host_api *api,const struct launcher_config *launcher,int selected,float offset)
{
    const char *labels[LAUNCHER_MAX+1];int count=launcher_count(launcher,true);
    for(int i=0;i<count;i++)labels[i]=launcher_at(launcher,true,i)->label;
    labels[count]="Back";
    console_scroll_list(api,labels,count+1,selected,offset,28,api->screen_height-78);
    console_menu_footer(api,true);
}
static inline void console_draw_live_inputs(const struct chirky_host_api *api,unsigned pad_mask,unsigned key_mask,const char *pad_line,const char *key_line)
{
    int h=api->screen_height,extra=(api->screen_width-24-208)/4;
    const int widths[]={58+extra,66+extra,42+extra,42+extra};
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        bool pad=(pad_mask&(1u<<i))!=0,key=(key_mask&(1u<<i))!=0;
        int cell=widths[i%4],x=12,y=h-108-(i/4)*26;
        for(int column=0;column<i%4;column++)x+=widths[column];
        launcher_round(api,x,y,cell-3,22,console_card,0,h);
        launcher_text(api,x+3,y+19,console_button_names[i],0,pad||key?launcher_cream:console_muted,console_card,x+cell-4,0,h);
        launcher_rect(api,x+3,y+3,(cell-9)/2,3,pad?(struct launcher_colour){93,220,153}:launcher_navy,0,h);
        launcher_rect(api,x+cell/2,y+3,(cell-9)/2,3,key?launcher_gold:launcher_navy,0,h);
    }
    launcher_text(api,12,h-144,pad_line,0,(struct launcher_colour){93,220,153},launcher_navy,api->screen_width-12,0,h);
    launcher_text(api,12,h-158,key_line,0,launcher_gold,launcher_navy,api->screen_width-12,0,h);
}
static inline void console_draw_controller_settings(const struct chirky_host_api *api,const struct binding_setup *setup,bool input_test,int selected,const char *message,float offset)
{
    int h=api->screen_height;
    if(setup->active) {
        int step=setup->step<CHIRKY_BUTTON_COUNT?setup->step:CHIRKY_BUTTON_COUNT-1;
        const char *name=console_button_names[step];
        launcher_text(api,(api->screen_width-launcher_text_width(name,2))/2,h-83,name,2,launcher_gold,launcher_navy,api->screen_width-12,0,h);
        char line[80];snprintf(line,sizeof(line),"%d of %d  -  %s",step+1,CHIRKY_BUTTON_COUNT,setup->wait_release?"Release all inputs":"Press now");
        launcher_text(api,(api->screen_width-launcher_text_width(line,0))/2,h-107,line,0,launcher_cream,launcher_navy,api->screen_width-12,0,h);
        int cell=(api->screen_width-32)/CHIRKY_BUTTON_COUNT;
        for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)launcher_rect(api,16+i*cell,h-133,cell-3,5,i<=step?launcher_gold:console_card,0,h);
        launcher_text(api,12,h-145,setup->message,0,console_muted,launcher_navy,api->screen_width-12,0,h);
        const char *hint=setup->keyboard?"F1 cancels - saves after all 8":"Hold two buttons to cancel";
        launcher_text(api,(api->screen_width-launcher_text_width(hint,0))/2,17,hint,0,console_muted,launcher_navy,api->screen_width-8,0,h);
    } else if(input_test) {
        const char *hint="Hold Secondary to go back";
        launcher_text(api,(api->screen_width-launcher_text_width(hint,0))/2,17,hint,0,console_muted,launcher_navy,api->screen_width-8,0,h);
    } else {
        const char *labels[]={"Map controller","Map keyboard","Test buttons","Back"};
        console_scroll_list(api,labels,4,selected,offset,28,h-78);
        if(message && *message)launcher_text(api,12,h-65,message,0,launcher_gold,launcher_navy,api->screen_width-12,0,h);
        console_menu_footer(api,true);
    }
}
static inline void console_draw_pause_menu(const struct chirky_host_api *api,int selected)
{
    int width=232,height=132,x=(api->screen_width-width)/2,y=(api->screen_height-height)/2;
    launcher_round(api,x+3,y-3,width,height,console_shadow,0,api->screen_height);
    launcher_round(api,x,y,width,height,console_card,0,api->screen_height);
    launcher_round(api,x+2,y+2,width-4,height-4,launcher_navy,0,api->screen_height);
    launcher_text(api,x+14,y+116,"Paused",2,launcher_cream,launcher_navy,x+width-14,0,api->screen_height);
    const char *labels[]={"Continue game","Return to launcher"};
    for(int i=0;i<2;i++) {
        int cy=y+78-i*32;struct launcher_colour background=i==selected?launcher_gold:console_card;
        launcher_round(api,x+10,cy-13,width-20,26,background,0,api->screen_height);
        launcher_text(api,x+20,cy+8,labels[i],1,i==selected?launcher_navy:launcher_cream,background,x+width-18,0,api->screen_height);
    }
    launcher_text(api,x+14,y+19,"Primary select   Secondary back",0,console_muted,launcher_navy,x+width-12,0,api->screen_height);
}

#endif
