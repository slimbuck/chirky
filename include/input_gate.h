#ifndef CHIRKY_INPUT_GATE_H
#define CHIRKY_INPUT_GATE_H
#include "chirky.h"
#include <string.h>

/* A fresh face, shoulder, or Start press begins a title screen. Select pauses. */
static inline bool chirky_title_pressed(const struct chirky_input *input)
{
    for(int button=CHIRKY_BUTTON_Y;button<=CHIRKY_BUTTON_START;button++)
        if(input->button_pressed[button])return true;
    return false;
}

/* Screen transitions consume the opening gesture. Require two neutral updates
   before accepting a fresh press; short release/press bounce cannot cross screens. */
struct chirky_input_gate { bool blocked; unsigned int neutral_frames; };
static inline void chirky_gate_begin(struct chirky_input_gate *gate)
{ gate->blocked=true;gate->neutral_frames=0; }
static inline bool chirky_gate_accept(struct chirky_input_gate *gate,bool neutral)
{
    if(!gate->blocked)return true;
    gate->neutral_frames=neutral?gate->neutral_frames+1:0;
    if(gate->neutral_frames>=2)gate->blocked=false;
    return false; /* the release frame itself never activates the new screen */
}
static inline void chirky_gate_filter(struct chirky_input_gate *gate,
                                        const struct chirky_input *input,
                                        struct chirky_input *output)
{
    bool neutral=true;
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)neutral &= !input->buttons[i] && !input->button_pressed[i];
    *output=*input;
    if(!chirky_gate_accept(gate,neutral)) {
        memset(output->buttons,0,sizeof(output->buttons));
        memset(output->button_pressed,0,sizeof(output->button_pressed));
    }
}
#endif
