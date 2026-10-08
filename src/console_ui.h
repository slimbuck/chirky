#ifndef CHIRKY_CONSOLE_UI_H
#define CHIRKY_CONSOLE_UI_H
#include "chirky.h"
#include "input_setup.h"
#include "input_labels.h"
#include "launcher_config.h"
#include "launcher_wordmark.h"
#include "splash_art.h"
#include "drawing.h"
#include "launcher_font.h"
#include "launcher_icons.h"

static const char *const console_button_names[]={"Move left","Move right","Move up","Move down","Confirm / main action","Back / other action","Start action","Pause / menu"};
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
static inline int launcher_character_width(unsigned c,int font)
{
    if(c>=0x2190 && c<=0x2193)return font==2?12:7;
    if(c<32 || c>126)c='?';
    return launcher_font_advance[font][c-32];
}
static inline int launcher_text_width(const char *text,int font)
{
    int width=0;while(*text)width+=launcher_character_width(chirky_text_next(&text),font);return width;
}
static inline void launcher_text(const struct chirky_host_api *api,int x,int top,const char *text,int font,
    struct launcher_colour colour,struct launcher_colour background,int right,int low,int high)
{
    while(*text) {
        unsigned c=chirky_text_next(&text);int advance=launcher_character_width(c,font);
        if(x+advance>right)break;
        if(c>=0x2190 && c<=0x2193) {
            const uint8_t *rows=glyph(c);int scale=font==2?2:1;
            int y=top+(font==2?1:font==1?-3:-4);
            for(int row=0;row<7;row++)for(int col=0;col<5;col++)
                if(rows[row]&(1u<<(4-col)))launcher_rect(api,x+col*scale,y-row*scale,scale,scale,colour,low,high);
            x+=advance;continue;
        }
        if(c<32 || c>126)c='?';
        c-=32;
        for(int row=0;row<20;row++)for(int col=0;col<16;) {
            unsigned coverage=(launcher_font_mask[font][c][row]>>(col*2))&3u;
            if(!coverage){col++;continue;}
            int end=col+1;while(end<16 && ((launcher_font_mask[font][c][row]>>(end*2))&3u)==coverage)end++;
            launcher_rect(api,x+col,top+2-row,end-col,1,launcher_mix(colour,background,(int)coverage*85),low,high);col=end;
        }
        x+=advance;
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
static inline void console_prompt_pair(const struct chirky_host_api *api,const char *first,const char *second,int left,int right,int top)
{
    char line[160];snprintf(line,sizeof(line),"%s   %s",first,second);
    if(launcher_text_width(line,0)<=right-left) {
        int x=left+(right-left-launcher_text_width(line,0))/2;
        launcher_text(api,x,top,line,0,console_muted,launcher_navy,right,0,api->screen_height);
    } else {
        const char *lines[]={first,second};
        for(int i=0;i<2;i++) {
            int x=left+(right-left-launcher_text_width(lines[i],0))/2;if(x<left)x=left;
            launcher_text(api,x,top+6-i*12,lines[i],0,console_muted,launcher_navy,right,0,api->screen_height);
        }
    }
}
static inline void console_menu_footer(const struct chirky_host_api *api,bool can_go_back)
{
    char primary[24],secondary[24],direction[52],first[64],second[40];
    chirky_input_label(api,CHIRKY_BUTTON_PRIMARY,primary,sizeof(primary));
    chirky_input_label(api,CHIRKY_BUTTON_SECONDARY,secondary,sizeof(secondary));
    chirky_direction_label(api,CHIRKY_BUTTON_UP,2,direction,sizeof(direction));
    if(can_go_back){snprintf(first,sizeof(first),"%s select",primary);snprintf(second,sizeof(second),"%s back",secondary);}
    else{snprintf(first,sizeof(first),"%s choose",direction);snprintf(second,sizeof(second),"%s select",primary);}
    console_prompt_pair(api,first,second,8,api->screen_width-8,17);
}
static inline void console_draw_loading(const struct chirky_host_api *api)
{
    int w=launcher_text_width("Loading",0)+12,x=api->screen_width-w-4,y=api->screen_height-23;
    launcher_round(api,x,y,w,19,console_card,0,api->screen_height);
    launcher_text(api,x+6,y+16,"Loading",0,launcher_cream,console_card,api->screen_width-5,0,api->screen_height);
}
static inline const struct launcher_icon *console_icon(const char *id)
{
    if(id)for(unsigned i=0;i<sizeof(launcher_icons)/sizeof(*launcher_icons);i++)
        if(!strcmp(id,launcher_icons[i].id))return &launcher_icons[i];
    return NULL;
}
static inline void console_draw_icon(const struct chirky_host_api *api,const struct launcher_icon *icon,int x,int y,int alpha,int low,int high)
{
    for(int row=0;row<20;row++)for(int col=0;col<20;) {
        uint32_t pixel=icon->pixels[row*20+col];int end=col+1;
        while(end<20 && icon->pixels[row*20+end]==pixel)end++;
        if(pixel&255u) {
            struct launcher_colour colour={(unsigned char)(pixel>>24),(unsigned char)(pixel>>16),(unsigned char)(pixel>>8)};
            launcher_rect(api,x+col,y+19-row,end-col,1,launcher_mix(colour,launcher_navy,alpha),low,high);
        }
        col=end;
    }
}
static inline void console_scroll_list_disabled(const struct chirky_host_api *api,const char *const *labels,const char *const *ids,int count,int selected,float offset,int low,int high,int disabled)
{
    int width=api->screen_width-24,centre=(low+high)/2,middle=(int)-offset;
    struct launcher_colour highlight=selected==disabled?console_card:launcher_gold;
    for(int pass=0;pass<2;pass++) {
        if(pass==1) {
            launcher_round(api,12,centre-16,width,26,console_shadow,low,high);
            launcher_round(api,12,centre-13,width,26,highlight,low,high);
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
                int text_x=23+curve;
                int font=launcher_text_width(labels[index],1)>width-22?0:1;
                const struct launcher_icon *icon=console_icon(ids?ids[index]:NULL);
                if(icon){console_draw_icon(api,icon,text_x,y-10,alpha,low,high);text_x+=26;}
                struct launcher_colour ink=launcher_mix(index==disabled?console_muted:launcher_cream,launcher_navy,alpha);
                launcher_text(api,text_x,y+8,labels[index],font,ink,card,api->screen_width-20,low,centre-13);
                launcher_text(api,text_x,y+8,labels[index],font,ink,card,api->screen_width-20,centre+13,high);
                launcher_text(api,text_x,y+8,labels[index],font,index==disabled?console_muted:launcher_navy,highlight,api->screen_width-20,centre-13,centre+13);
            }
        }
    }
}
static inline void console_scroll_list(const struct chirky_host_api *api,const char *const *labels,const char *const *ids,int count,int selected,float offset,int low,int high)
{ console_scroll_list_disabled(api,labels,ids,count,selected,offset,low,high,-1); }
static inline void console_draw_launcher(const struct chirky_host_api *api,const struct splash_art *art,const struct launcher_config *launcher,int selected,float offset)
{
    console_page(api,art,NULL);
    const char *labels[LAUNCHER_MAX],*ids[LAUNCHER_MAX];int count=launcher_count(launcher,false);
    for(int i=0;i<count;i++){const struct launcher_item *item=launcher_at(launcher,false,i);labels[i]=item->label;ids[i]=item->id;}
    console_scroll_list(api,labels,ids,count,selected,offset,28,api->screen_height-78);
    console_menu_footer(api,false);
}
static inline void console_draw_settings_menu(const struct chirky_host_api *api,const struct launcher_config *launcher,int selected,float offset)
{
    const char *labels[LAUNCHER_MAX+1];int count=launcher_count(launcher,true);
    for(int i=0;i<count;i++)labels[i]=launcher_at(launcher,true,i)->label;
    labels[count]="Back";
    console_scroll_list(api,labels,NULL,count+1,selected,offset,28,api->screen_height-78);
    console_menu_footer(api,true);
}
static inline void console_draw_controller_selection(const struct chirky_host_api *api,const char *message)
{
    int h=api->screen_height;
    launcher_text(api,12,h-84,"Press a button on your",0,launcher_cream,launcher_navy,api->screen_width-12,0,h);
    launcher_text(api,12,h-98,"USB controller/joystick",0,launcher_cream,launcher_navy,api->screen_width-12,0,h);
    if(message && *message)launcher_text(api,12,h-116,message,0,console_muted,launcher_navy,api->screen_width-12,0,h);
    launcher_round(api,12,34,api->screen_width-24,26,launcher_gold,0,h);
    launcher_text(api,23,54,"Back",1,launcher_navy,launcher_gold,api->screen_width-12,0,h);
    console_menu_footer(api,true);
}
static inline void console_draw_controller_profile(const struct chirky_host_api *api,const char *name,int selected,const char *message)
{
    const char *labels[]={"SNES preset","Generic joystick","Remap buttons","Back"};
    launcher_text(api,12,api->screen_height-65,message && *message?message:name,0,console_muted,launcher_navy,api->screen_width-12,0,api->screen_height);
    console_scroll_list(api,labels,NULL,4,selected,0,28,api->screen_height-78);
    console_menu_footer(api,true);
}
static inline struct launcher_colour console_device_colour(unsigned row)
{
    const struct launcher_colour colours[]={{244,184,69},{239,121,106},{93,220,153},{112,184,255},
        {215,155,255},{255,168,98},{112,223,224},{238,166,203}};
    return colours[row%CHIRKY_INPUT_DEVICES];
}
static inline void console_draw_live_inputs(const struct chirky_host_api *api,const struct chirky_input *input,const char *pad_line,const char *key_line,bool hide_keyboard)
{
    int h=api->screen_height;unsigned keyboards=0,controllers=0;
    launcher_rect(api,0,0,api->screen_width,h,launcher_navy,0,h);
    launcher_text(api,12,h-12,"Test inputs",1,launcher_cream,launcher_navy,api->screen_width-12,0,h);
    launcher_text(api,12,h-33,"USB = controller/joystick",0,console_muted,launcher_navy,api->screen_width-12,0,h);
    int count=0,visible_row=0;
    for(unsigned i=0;i<input->device_count;i++)count+=!hide_keyboard || input->devices[i].kind!=CHIRKY_DEVICE_KEYBOARD;
    int row_height=count?(h-104)/count:24;
    if(row_height>24)row_height=24;
    for(unsigned row=0;row<input->device_count;row++) {
        const struct chirky_device_input *device=&input->devices[row];
        if(hide_keyboard && device->kind==CHIRKY_DEVICE_KEYBOARD)continue;
        struct launcher_colour colour=console_device_colour(row);
        char name[24],held[256]="";
        if(device->kind==CHIRKY_DEVICE_KEYBOARD)snprintf(name,sizeof(name),"KB %u",++keyboards);
        else if(device->kind==CHIRKY_DEVICE_CONTROLLER)snprintf(name,sizeof(name),"USB %u",++controllers);
        else snprintf(name,sizeof(name),"Touch");
        for(int b=0;b<CHIRKY_BUTTON_COUNT;b++)if(device->buttons[b] || device->button_pressed[b]) {
            size_t used=strlen(held);snprintf(held+used,sizeof(held)-used,"%s%s",used?" ":"",device->labels[b]);
        }
        bool active=*held;int y=h-49-visible_row++*row_height;
        launcher_rect(api,12,y-row_height+3,api->screen_width-24,row_height-1,console_card,0,h);
        launcher_rect(api,12,y-row_height+3,3,row_height-1,colour,0,h);
        launcher_text(api,20,y,name,0,colour,console_card,78,0,h);
        launcher_text(api,80,y,active?held:"-",0,active?colour:console_muted,console_card,api->screen_width-16,0,h);
    }
    if(!count)launcher_text(api,12,h-60,"No input devices",0,console_muted,launcher_navy,api->screen_width-12,0,h);
    launcher_text(api,12,47,pad_line,0,console_muted,launcher_navy,api->screen_width-12,0,h);
    if(!hide_keyboard)launcher_text(api,12,35,key_line,0,console_muted,launcher_navy,api->screen_width-12,0,h);
}
static inline void console_draw_controller_settings(const struct chirky_host_api *api,const struct binding_setup *setup,bool input_test,int selected,const char *message,float offset,bool controller_available,bool hide_keyboard)
{
    int h=api->screen_height;
    if(setup->active) {
        int step=setup->step<CHIRKY_BUTTON_COUNT?setup->step:CHIRKY_BUTTON_COUNT-1;
        const char *name=console_button_names[step];
        int font=launcher_text_width(name,2)>api->screen_width-24?1:2;
        launcher_text(api,(api->screen_width-launcher_text_width(name,font))/2,h-83,name,font,launcher_gold,launcher_navy,api->screen_width-12,0,h);
        char line[80];snprintf(line,sizeof(line),"%d of %d  -  %s",step+1,CHIRKY_BUTTON_COUNT,setup->wait_release?"Release all inputs":"Press now");
        launcher_text(api,(api->screen_width-launcher_text_width(line,0))/2,h-107,line,0,launcher_cream,launcher_navy,api->screen_width-12,0,h);
        int cell=(api->screen_width-32)/CHIRKY_BUTTON_COUNT;
        for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)launcher_rect(api,16+i*cell,h-133,cell-3,5,i<=step?launcher_gold:console_card,0,h);
        launcher_text(api,12,h-145,setup->message,0,console_muted,launcher_navy,api->screen_width-12,0,h);
        const char *hint=setup->keyboard?"F1 cancels - saves after all 8":"Hold two buttons to cancel";
        launcher_text(api,(api->screen_width-launcher_text_width(hint,0))/2,17,hint,0,console_muted,launcher_navy,api->screen_width-8,0,h);
    } else if(input_test) {
        char label[24],hint[64];chirky_input_label(api,CHIRKY_BUTTON_SECONDARY,label,sizeof(label));
        snprintf(hint,sizeof(hint),"Hold %s to go back",label);
        launcher_text(api,(api->screen_width-launcher_text_width(hint,0))/2,17,hint,0,console_muted,launcher_navy,api->screen_width-8,0,h);
    } else {
        const char *labels[]={"Test inputs",controller_available?"Map USB controller/joystick":"USB controller/joystick (none)","Map keyboard 1","Map keyboard 2","Back"};
        if(hide_keyboard)labels[2]="Back";
        console_scroll_list_disabled(api,labels,NULL,hide_keyboard?3:5,selected,offset,28,h-78,controller_available?-1:1);
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
    char primary[24],secondary[24],first[40],second[40];
    chirky_input_label(api,CHIRKY_BUTTON_PRIMARY,primary,sizeof(primary));
    chirky_input_label(api,CHIRKY_BUTTON_SECONDARY,secondary,sizeof(secondary));
    snprintf(first,sizeof(first),"%s select",primary);snprintf(second,sizeof(second),"%s back",secondary);
    console_prompt_pair(api,first,second,x+14,x+width-12,y+19);
}

#endif
