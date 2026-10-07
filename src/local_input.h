#ifndef CHIRKY_LOCAL_INPUT_H
#define CHIRKY_LOCAL_INPUT_H
#include "chirky.h"
#include <string.h>

/* Shared edge tracking. Adapters supply logical masks and buffered short taps.
   Finish every console tick, even while gameplay is paused or loading. */
struct local_input_history { uint32_t id; unsigned mask; };
struct local_input_bank { struct local_input_history previous[CHIRKY_INPUT_DEVICES]; };
static inline struct chirky_device_input *local_input_add(struct local_input_bank *bank,
    struct chirky_input *frame,uint32_t id,enum chirky_device_kind kind,unsigned mask,unsigned pending)
{
    if(!id || frame->device_count>=CHIRKY_INPUT_DEVICES)return NULL;
    unsigned before=0;
    for(unsigned i=0;i<CHIRKY_INPUT_DEVICES;i++)if(bank->previous[i].id==id)before=bank->previous[i].mask;
    for(unsigned i=0;i<frame->device_count;i++)if(frame->devices[i].id==id)return NULL;
    struct chirky_device_input *out=&frame->devices[frame->device_count++];
    memset(out,0,sizeof(*out));out->id=id;out->kind=kind;
    for(unsigned i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        out->buttons[i]=(mask&(1u<<i))!=0;
        out->button_pressed[i]=((pending|(mask&~before))&(1u<<i))!=0;
    }
    return out;
}
static inline void local_input_finish(struct local_input_bank *bank,const struct chirky_input *frame)
{
    memset(bank,0,sizeof(*bank));
    for(unsigned i=0;i<frame->device_count;i++) {
        bank->previous[i].id=frame->devices[i].id;
        for(unsigned b=0;b<CHIRKY_BUTTON_COUNT;b++)if(frame->devices[i].buttons[b])bank->previous[i].mask|=1u<<b;
    }
}
#endif
