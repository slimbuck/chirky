#ifndef TWO_FORTY_INPUT_GATE_H
#define TWO_FORTY_INPUT_GATE_H
#include "two_forty.h"
#include <string.h>

/* Screen transitions consume the opening gesture. Require two neutral updates
   before accepting a fresh press; short release/press bounce cannot cross screens. */
struct two_forty_input_gate { bool blocked; unsigned int neutral_frames; };
static inline void two_forty_gate_begin(struct two_forty_input_gate *gate)
{ gate->blocked=true;gate->neutral_frames=0; }
static inline bool two_forty_gate_accept(struct two_forty_input_gate *gate,bool neutral)
{
    if(!gate->blocked)return true;
    gate->neutral_frames=neutral?gate->neutral_frames+1:0;
    if(gate->neutral_frames>=2)gate->blocked=false;
    return false; /* the release frame itself never activates the new screen */
}
static inline void two_forty_gate_filter(struct two_forty_input_gate *gate,
                                        const struct two_forty_input *input,
                                        struct two_forty_input *output)
{
    bool neutral=true;
    for(int i=0;i<TWO_FORTY_BUTTON_COUNT;i++)neutral &= !input->buttons[i] && !input->button_pressed[i];
    *output=*input;
    if(!two_forty_gate_accept(gate,neutral)) {
        memset(output->buttons,0,sizeof(output->buttons));
        memset(output->button_pressed,0,sizeof(output->button_pressed));
    }
}
#endif
