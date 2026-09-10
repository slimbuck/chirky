#ifndef FRAME_TIMING_H
#define FRAME_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#define FRAME_HISTORY 96
struct frame_sample { uint32_t work_us,interval_us,missed; };
struct frame_timing {
    struct frame_sample history[FRAME_HISTORY];
    unsigned int head,count;
    uint32_t sequence,pending_work_us;
    uint64_t stamp_us,missed_total;
    bool have_flip;
};
/* DRM sequence numbers count display refreshes, not CPU loop iterations. */
static inline void frame_timing_present(struct frame_timing *t,uint32_t sequence,uint64_t stamp_us)
{
    if (t->have_flip && stamp_us>t->stamp_us) {
        uint32_t gap=sequence-t->sequence; /* handles sequence rollover */
        if (gap>0 && gap<10000) {
            struct frame_sample s={t->pending_work_us,(uint32_t)(stamp_us-t->stamp_us),gap-1};
            t->history[t->head]=s;t->head=(t->head+1)%FRAME_HISTORY;
            if (t->count<FRAME_HISTORY)t->count++;
            t->missed_total+=s.missed;
        }
    }
    t->sequence=sequence;t->stamp_us=stamp_us;t->have_flip=true;
}
#endif
