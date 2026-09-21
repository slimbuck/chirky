#include "input_bindings.h"
#include <stdio.h>
#include <string.h>

void default_bindings(struct controller_binding *bindings)
{
    bindings[CHIRKY_BUTTON_LEFT]=(struct controller_binding){BINDING_ABS,ABS_HAT0X,-1};
    bindings[CHIRKY_BUTTON_RIGHT]=(struct controller_binding){BINDING_ABS,ABS_HAT0X,1};
    bindings[CHIRKY_BUTTON_UP]=(struct controller_binding){BINDING_ABS,ABS_HAT0Y,-1};
    bindings[CHIRKY_BUTTON_DOWN]=(struct controller_binding){BINDING_ABS,ABS_HAT0Y,1};
    bindings[CHIRKY_BUTTON_Y]=(struct controller_binding){BINDING_KEY,BTN_SOUTH,0};
    bindings[CHIRKY_BUTTON_B]=(struct controller_binding){BINDING_KEY,BTN_EAST,0};
    bindings[CHIRKY_BUTTON_A]=(struct controller_binding){BINDING_KEY,BTN_NORTH,0};
    bindings[CHIRKY_BUTTON_X]=(struct controller_binding){BINDING_KEY,BTN_C,0};
    bindings[CHIRKY_BUTTON_L]=(struct controller_binding){BINDING_KEY,BTN_WEST,0};
    bindings[CHIRKY_BUTTON_R]=(struct controller_binding){BINDING_KEY,BTN_Z,0};
    bindings[CHIRKY_BUTTON_START]=(struct controller_binding){BINDING_KEY,BTN_TR2,0};
    bindings[CHIRKY_BUTTON_SELECT]=(struct controller_binding){BINDING_KEY,BTN_TL2,0};
}

void default_keyboard_bindings(struct controller_binding *bindings)
{
    const unsigned int codes[]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN,
        KEY_Z,KEY_X,KEY_C,KEY_S,KEY_A,KEY_D,KEY_ENTER,KEY_ESC};
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) bindings[i]=(struct controller_binding){BINDING_KEY,codes[i],0};
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

void setup_begin(struct binding_setup *setup, bool keyboard)
{
    *setup=(struct binding_setup){.active=true,.keyboard=keyboard,.wait_release=true,.message=""};
}

void setup_offer(struct binding_setup *setup, struct controller_binding binding)
{
    if (!setup->active || setup->wait_release || setup->ready || setup->complete) return;
    if (setup->keyboard && (binding.kind!=BINDING_KEY || binding.code>=BTN_MISC || binding.code==KEY_F1 || binding.code==KEY_F12)) return;
    setup->candidate=binding; setup->ready=true; setup->wait_release=true;
}

void setup_release(struct binding_setup *setup, bool released)
{
    if (!setup->active || !setup->wait_release || !released) return;
    setup->wait_release=false;
    if (!setup->ready) return;
    setup->ready=false;
    for (int i=0;i<setup->step;i++) {
        const struct controller_binding *old=&setup->pending[i], *next=&setup->candidate;
        if (old->kind==next->kind && old->code==next->code && old->direction==next->direction) {
            setup->message="ALREADY USED - TRY ANOTHER"; return;
        }
    }
    setup->pending[setup->step++]=setup->candidate; setup->message="";
    if (setup->step==CHIRKY_BUTTON_COUNT) setup->complete=true;
}
