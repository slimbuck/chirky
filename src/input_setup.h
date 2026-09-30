#ifndef CHIRKY_INPUT_SETUP_H
#define CHIRKY_INPUT_SETUP_H
#include "chirky.h"
#include <string.h>

/* Platform adapters assign raw codes; the capture workflow is portable. */
enum binding_kind { BINDING_NONE, BINDING_KEY, BINDING_ABS };
struct controller_binding { enum binding_kind kind; unsigned int code; int direction; };
struct binding_setup {
    bool active, keyboard, wait_release, ready, complete;
    int step;
    struct controller_binding candidate, pending[CHIRKY_BUTTON_COUNT];
    const char *message;
};
static inline void setup_begin(struct binding_setup *setup, bool keyboard)
{
    *setup=(struct binding_setup){.active=true,.keyboard=keyboard,.wait_release=true,.message=""};
}
static inline void chirky_setup_offer(struct binding_setup *setup, struct controller_binding binding)
{
    if (!setup->active || setup->wait_release || setup->ready || setup->complete || binding.kind==BINDING_NONE) return;
    setup->candidate=binding;setup->ready=true;setup->wait_release=true;
}
static inline void setup_release(struct binding_setup *setup, bool released)
{
    if (!setup->active || !setup->wait_release || !released) return;
    setup->wait_release=false;
    if (!setup->ready) return;
    setup->ready=false;
    for (int i=0;i<setup->step;i++) {
        const struct controller_binding *old=&setup->pending[i],*next=&setup->candidate;
        if (old->kind==next->kind && old->code==next->code && old->direction==next->direction) {
            setup->message="ALREADY USED - TRY ANOTHER";return;
        }
    }
    setup->pending[setup->step++]=setup->candidate;setup->message="";
    if(setup->step==CHIRKY_BUTTON_COUNT)setup->complete=true;
}
#endif
