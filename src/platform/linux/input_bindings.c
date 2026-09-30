#include "input_bindings.h"
#include <stdio.h>
#include <string.h>

void default_bindings(struct controller_binding *bindings)
{
    bindings[CHIRKY_BUTTON_LEFT]=(struct controller_binding){BINDING_ABS,ABS_HAT0X,-1};
    bindings[CHIRKY_BUTTON_RIGHT]=(struct controller_binding){BINDING_ABS,ABS_HAT0X,1};
    bindings[CHIRKY_BUTTON_UP]=(struct controller_binding){BINDING_ABS,ABS_HAT0Y,-1};
    bindings[CHIRKY_BUTTON_DOWN]=(struct controller_binding){BINDING_ABS,ABS_HAT0Y,1};
    bindings[CHIRKY_BUTTON_SECONDARY]=(struct controller_binding){BINDING_KEY,BTN_SOUTH,0};
    bindings[CHIRKY_BUTTON_PRIMARY]=(struct controller_binding){BINDING_KEY,BTN_EAST,0};
    bindings[CHIRKY_BUTTON_START]=(struct controller_binding){BINDING_KEY,BTN_TR2,0};
    bindings[CHIRKY_BUTTON_MENU]=(struct controller_binding){BINDING_KEY,BTN_TL2,0};
}

void default_keyboard_bindings(struct controller_binding *bindings)
{
    const unsigned int codes[]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN,
        KEY_X,KEY_Z,KEY_ENTER,KEY_ESC};
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
