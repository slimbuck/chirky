#ifndef CHIRKY_TRACE_H
#define CHIRKY_TRACE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TRACE_DEPTH 32
struct trace_span {
    char name[64];
    uint64_t start,end,cpu_start,cpu_end;
    unsigned frame,rect_start,rect_end,parent;
    unsigned missed,interval;
    uint64_t gpu_serial;
    uint32_t gpu_us;
    bool gpu_valid;
};
struct trace_capture {
    char request_id[33];
    struct trace_span *spans;
    unsigned count,capacity,stack[TRACE_DEPTH],depth,frames,target;
    unsigned budget,missed,rectangles,batches,sprites;
    unsigned gpu_mode,gpu_cursor;
    uint64_t gpu_poll_after,finish_after;
    bool recording,invalid;
};
static inline uint64_t trace_clock(clockid_t clock)
{
    struct timespec t;clock_gettime(clock,&t);
    return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
static inline bool trace_arm(struct trace_capture *t,unsigned frames,unsigned budget)
{
    if(t->spans || !frames || frames>1800)return false;
    struct trace_span *spans=calloc((size_t)frames*64,sizeof(*spans));
    if(!spans)return false;
    *t=(struct trace_capture){.spans=spans,.capacity=frames*64,.target=frames,.budget=budget};
    return true;
}
static inline void trace_scope_at(struct trace_capture *t,const char *name,bool begin,
                                  unsigned rects,uint64_t wall,uint64_t cpu)
{
    if(!t->recording || t->invalid)return;
    if(begin) {
        if(t->depth==TRACE_DEPTH || t->count==t->capacity){t->invalid=true;return;}
        unsigned i=t->count++;
        unsigned parent=t->depth?t->stack[t->depth-1]:UINT32_MAX;
        t->stack[t->depth++]=i;
        t->spans[i]=(struct trace_span){.start=wall,.cpu_start=cpu,.frame=t->frames,.rect_start=rects,.parent=parent};
        snprintf(t->spans[i].name,sizeof(t->spans[i].name),"%s",name);
    } else {
        if(!t->depth){t->invalid=true;return;}
        struct trace_span *s=&t->spans[t->stack[--t->depth]];
        if(strcmp(s->name,name) || wall<s->start || cpu<s->cpu_start){t->invalid=true;return;}
        s->end=wall;s->cpu_end=cpu;s->rect_end=rects;
    }
}
static inline void trace_scope(struct trace_capture *t,const char *name,bool begin,unsigned rects)
{
    if(t->recording)trace_scope_at(t,name,begin,rects,
        trace_clock(CLOCK_MONOTONIC),trace_clock(CLOCK_THREAD_CPUTIME_ID));
}
static inline void trace_string(FILE *f,const char *s)
{
    fputc('"',f);
    for(;*s;s++) {
        unsigned char c=(unsigned char)*s;
        if(c=='"' || c=='\\'){fputc('\\',f);fputc(c,f);}
        else if(c<32)fprintf(f,"\\u%04x",c);
        else fputc(c,f);
    }
    fputc('"',f);
}
/* Export only after the last recorded frame; no file I/O inside CPU scopes. */
static inline bool trace_write(struct trace_capture *t,const char *path)
{
    FILE *f=fopen(path,"w");if(!f)return false;
    fprintf(f,"{\"format\":1,\"request_id\":");trace_string(f,t->request_id);
    fprintf(f,",\"valid\":%s,\"frames\":%u,\"target\":%u,"
        "\"budget_us\":%u,\"missed\":%u,\"rectangles\":%u,\"batches\":%u,\"sprites\":%u,"
        "\"gpu_attribution\":\"%s\",\"traceEvents\":[",
        !t->invalid && !t->depth && t->frames==t->target?"true":"false",
        t->frames,t->target,t->budget,t->missed,t->rectangles,t->batches,t->sprites,
        t->gpu_mode==1?"elapsed-query":t->gpu_mode==2?"kernel-estimate":"unavailable");
    for(unsigned i=0;i<t->count;i++) {
        const struct trace_span *s=&t->spans[i];
        if(i)fputc(',',f);
        fprintf(f,"{\"name\":");trace_string(f,s->name);
        fprintf(f,",\"cat\":\"cpu\",\"ph\":\"X\",\"pid\":1,\"tid\":1,"
            "\"ts\":%llu,\"dur\":%llu,\"args\":{\"frame\":%u,\"cpu_us\":%llu,\"rectangles\":%u,\"missed\":%u,\"interval_us\":%u,\"parent\":%u,\"gpu_us\":",
            (unsigned long long)s->start,(unsigned long long)(s->end>=s->start?s->end-s->start:0),
            s->frame,(unsigned long long)(s->cpu_end>=s->cpu_start?s->cpu_end-s->cpu_start:0),
            s->rect_end>=s->rect_start?s->rect_end-s->rect_start:0,s->missed,s->interval,s->parent);
        if(s->gpu_valid)fprintf(f,"%u",s->gpu_us);else fputs("null",f);
        fputs("}}",f);
    }
    fputs("]}\n",f);bool ok=!ferror(f);if(fclose(f))ok=false;return ok;
}
#endif
