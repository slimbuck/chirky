#include "input_bindings.h"
#include "text_encoding.h"
#include <stdio.h>
#include <string.h>

void controller_binding_label(const struct controller_binding *binding,
    enum controller_profile profile,char *out,size_t size)
{
    if(binding->kind==BINDING_NONE){snprintf(out,size,"UNBOUND");return;}
    if(binding->kind==BINDING_ABS) {
        if(binding->code==ABS_HAT0X || binding->code==ABS_HAT0Y) {
            snprintf(out,size,"%s",binding->code==ABS_HAT0X?
                (binding->direction<0?CHIRKY_ARROW_LEFT:CHIRKY_ARROW_RIGHT):
                (binding->direction<0?CHIRKY_ARROW_UP:CHIRKY_ARROW_DOWN));return;
        }
        snprintf(out,size,"AX%u %s",binding->code,binding->direction<0?"NEG":"POS");return;
    }
    /* Tested GP2040 Generic HID wiring, independent of the logical mapping.
       These are not the conventional evdev face-button positions. */
    if(profile==CONTROLLER_SNES) {
        const char *names[]={"Y","B","X","A","L","R"};
        if(binding->code>=304 && binding->code<=309){snprintf(out,size,"%s",names[binding->code-304]);return;}
        if(binding->code==312 || binding->code==313){snprintf(out,size,"%s",binding->code==312?"SELECT":"START");return;}
        /* The installed GP2040 adapter also exposes its D-pad as buttons. */
        const char *directions[]={CHIRKY_ARROW_UP,CHIRKY_ARROW_DOWN,CHIRKY_ARROW_LEFT,CHIRKY_ARROW_RIGHT};
        if(binding->code>=BTN_TRIGGER_HAPPY1 && binding->code<=BTN_TRIGGER_HAPPY4) {
            snprintf(out,size,"%s",directions[binding->code-BTN_TRIGGER_HAPPY1]);return;
        }
    }
    const char *label=NULL;
    switch(binding->code) {
        case BTN_DPAD_LEFT:case KEY_LEFT:label=CHIRKY_ARROW_LEFT;break;
        case BTN_DPAD_RIGHT:case KEY_RIGHT:label=CHIRKY_ARROW_RIGHT;break;
        case BTN_DPAD_UP:case KEY_UP:label=CHIRKY_ARROW_UP;break;
        case BTN_DPAD_DOWN:case KEY_DOWN:label=CHIRKY_ARROW_DOWN;break;
    }
    if(label)snprintf(out,size,"%s",label);
    else snprintf(out,size,"BTN %u",binding->code);
}

const struct controller_binding snes_bindings[CHIRKY_BUTTON_COUNT]={
    [CHIRKY_BUTTON_LEFT]={BINDING_ABS,ABS_HAT0X,-1},
    [CHIRKY_BUTTON_RIGHT]={BINDING_ABS,ABS_HAT0X,1},
    [CHIRKY_BUTTON_UP]={BINDING_ABS,ABS_HAT0Y,-1},
    [CHIRKY_BUTTON_DOWN]={BINDING_ABS,ABS_HAT0Y,1},
    [CHIRKY_BUTTON_SECONDARY]={BINDING_KEY,BTN_SOUTH,0},
    [CHIRKY_BUTTON_PRIMARY]={BINDING_KEY,BTN_EAST,0},
    [CHIRKY_BUTTON_START]={BINDING_KEY,BTN_TR2,0},
    [CHIRKY_BUTTON_MENU]={BINDING_KEY,BTN_TL2,0},
};
void default_bindings(struct controller_binding *bindings)
{ memcpy(bindings,snes_bindings,sizeof(snes_bindings)); }

void default_player_keyboard_bindings(struct controller_binding *bindings,unsigned player)
{
    const unsigned codes[2][CHIRKY_BUTTON_COUNT]={
        {KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN,KEY_N,KEY_M,KEY_ENTER,KEY_ESC},
        {KEY_A,KEY_D,KEY_W,KEY_S,KEY_F,KEY_G,KEY_ENTER,KEY_ESC}};
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)bindings[i]=(struct controller_binding){BINDING_KEY,codes[player?1:0][i],0};
}

void default_keyboard_bindings(struct controller_binding *bindings)
{
    default_player_keyboard_bindings(bindings,0);
}

bool parse_binding(const char *text, struct controller_binding *binding)
{
    if (!strcmp(text,"none")) { *binding=(struct controller_binding){0}; return true; }
    unsigned int code=0; int direction=0; char extra;
    if (sscanf(text,"key:%u%c",&code,&extra)==1 && code<=KEY_MAX) {
        *binding=(struct controller_binding){BINDING_KEY,code,0}; return true;
    }
    if (sscanf(text,"abs:%u:%d%c",&code,&direction,&extra)==2 && code<=ABS_MAX && (direction==-1 || direction==1)) {
        *binding=(struct controller_binding){BINDING_ABS,code,direction}; return true;
    }
    return false;
}
