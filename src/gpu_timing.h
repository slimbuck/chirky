#ifndef GPU_TIMING_H
#define GPU_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define GPU_HISTORY 96
#define GPU_QUERIES 8
#define GPU_ELAPSED 0x88bf
typedef void (*gpu_proc)(void);
struct gpu_sample { uint64_t serial,stamp_us; uint32_t value; bool valid; };
struct gpu_timing {
    void (*gen)(int,unsigned *);
    void (*del)(int,const unsigned *);
    void (*begin)(unsigned,unsigned);
    void (*end)(unsigned);
    void (*available)(unsigned,unsigned,unsigned *);
    void (*result)(unsigned,unsigned,uint64_t *);
    void (*integer)(unsigned,int *);
    void (*query)(unsigned,unsigned,int *);
    void (*monitor_begin)(unsigned);
    void (*monitor_end)(unsigned);
    void (*monitor_data)(unsigned,unsigned,int,unsigned *,int *);
    unsigned group,counters[2];
    unsigned ids[GPU_QUERIES];
    uint64_t pending[GPU_QUERIES],serial;
    struct gpu_sample history[GPU_HISTORY];
    unsigned head,count;
    int active;
    bool supported,cycles;
};

/* VC4 exposes shader-cycle counters, but no elapsed-time queries. Keep their
   units explicit: cycles measure work, not wall time or whole-GPU utilisation. */
static inline void gpu_timing_init_counters(struct gpu_timing *t,const char *extensions,gpu_proc (*resolve)(const char *))
{
    if(t->supported || !extensions || !strstr(extensions,"GL_AMD_performance_monitor"))return;
    void (*groups)(int *,int,unsigned *);
    void (*counters)(unsigned,int *,int *,int,unsigned *);
    void (*name)(unsigned,unsigned,int,int *,char *);
    void (*info)(unsigned,unsigned,unsigned,void *);
    void (*select)(unsigned,unsigned char,unsigned,int,unsigned *);
#define GPU_LOAD(field,text) do {gpu_proc p=resolve(text);if(!p)return;memcpy(&field,&p,sizeof(p));} while(0)
    GPU_LOAD(groups,"glGetPerfMonitorGroupsAMD");GPU_LOAD(counters,"glGetPerfMonitorCountersAMD");
    GPU_LOAD(name,"glGetPerfMonitorCounterStringAMD");GPU_LOAD(info,"glGetPerfMonitorCounterInfoAMD");
    GPU_LOAD(select,"glSelectPerfMonitorCountersAMD");
    GPU_LOAD(t->gen,"glGenPerfMonitorsAMD");GPU_LOAD(t->del,"glDeletePerfMonitorsAMD");
    GPU_LOAD(t->monitor_begin,"glBeginPerfMonitorAMD");GPU_LOAD(t->monitor_end,"glEndPerfMonitorAMD");
    GPU_LOAD(t->monitor_data,"glGetPerfMonitorCounterDataAMD");
#undef GPU_LOAD
    unsigned list[32];int count=0;groups(&count,32,list);
    for(int g=0;g<count && g<32;g++) {
        unsigned ids[128];int n=0,max=0;counters(list[g],&n,&max,128,ids);if(max<2)continue;
        unsigned found=0;
        for(int c=0;c<n && c<128;c++) {
            char label[128]={0};unsigned type=0;name(list[g],ids[c],sizeof(label),NULL,label);
            info(list[g],ids[c],0x8bc0,&type);if(type!=0x8bc2)continue;
            if(!strcmp(label,"QPU-total-clk-cycles-vertex-coord-shading")) {t->counters[0]=ids[c];found|=1;}
            if(!strcmp(label,"QPU-total-clk-cycles-fragment-shading")) {t->counters[1]=ids[c];found|=2;}
        }
        if(found!=3)continue;
        t->group=list[g];t->gen(GPU_QUERIES,t->ids);
        for(int j=0;j<GPU_QUERIES;j++)select(t->ids[j],1,t->group,2,t->counters);
        t->cycles=t->supported=true;return;
    }
}

static inline bool gpu_timing_counter_result(struct gpu_timing *t,unsigned id,uint32_t *value)
{
    unsigned data[8]={0};int bytes=0;
    t->monitor_data(id,0x8bc6,sizeof(data),data,&bytes);
    if(bytes!=sizeof(data))return false;
    uint64_t total=0;unsigned found=0;
    for(int i=0;i<8;i+=4) {
        if(data[i]!=t->group)return false;
        unsigned bit=data[i+1]==t->counters[0]?1:data[i+1]==t->counters[1]?2:0;
        if(!bit || (found&bit))return false;
        found|=bit;uint64_t cycles;memcpy(&cycles,&data[i+2],sizeof(cycles));
        total=cycles>UINT32_MAX || total>UINT32_MAX-cycles?UINT32_MAX:total+cycles;
    }
    *value=(uint32_t)total;return found==3;
}

static inline void gpu_timing_init(struct gpu_timing *t,const char *extensions,gpu_proc (*resolve)(const char *))
{
    memset(t,0,sizeof(*t));t->active=-1;
    const char *name="GL_EXT_disjoint_timer_query";
    const char *match=extensions?strstr(extensions,name):NULL;
    if(!match || (match!=extensions && match[-1]!=' ') || (match[strlen(name)] && match[strlen(name)]!=' '))return;
#define GPU_LOAD(field,name) do { gpu_proc p=resolve(name); if(!p)return; memcpy(&t->field,&p,sizeof(p)); } while(0)
    GPU_LOAD(gen,"glGenQueriesEXT");GPU_LOAD(del,"glDeleteQueriesEXT");
    GPU_LOAD(begin,"glBeginQueryEXT");GPU_LOAD(end,"glEndQueryEXT");
    GPU_LOAD(available,"glGetQueryObjectuivEXT");GPU_LOAD(result,"glGetQueryObjectui64vEXT");
    GPU_LOAD(integer,"glGetIntegerv");GPU_LOAD(query,"glGetQueryivEXT");
#undef GPU_LOAD
    int bits=0;t->query(GPU_ELAPSED,0x8864,&bits);if(!bits)return;
    t->gen(GPU_QUERIES,t->ids);
    for(int i=0;i<GPU_QUERIES;i++)if(!t->ids[i]) {t->del(GPU_QUERIES,t->ids);memset(t->ids,0,sizeof(t->ids));return;}
    t->supported=true;
}

/* Read only completed queries. Never wait for the GPU or reuse a busy slot. */
static inline void gpu_timing_poll(struct gpu_timing *t)
{
    if(!t->supported)return;
    if(t->cycles) {
        for(int i=0;i<GPU_QUERIES;i++)if(t->pending[i]) {
            unsigned ready=0;t->monitor_data(t->ids[i],0x8bc4,sizeof(ready),&ready,NULL);if(!ready)continue;
            uint32_t value=0;bool valid=gpu_timing_counter_result(t,t->ids[i],&value);
            struct gpu_sample *s=&t->history[(t->pending[i]-1)%GPU_HISTORY];
            if(s->serial==t->pending[i]) {s->value=value;s->valid=valid;}
            t->pending[i]=0;
        }
        return;
    }
    int disjoint=0;t->integer(0x8fbb,&disjoint);
    if(disjoint) {
        t->del(GPU_QUERIES,t->ids);t->gen(GPU_QUERIES,t->ids);
        memset(t->pending,0,sizeof(t->pending));
        memset(t->history,0,sizeof(t->history));t->head=t->count=0;
        return;
    }
    for(int i=0;i<GPU_QUERIES;i++)if(t->pending[i]) {
        unsigned ready=0;t->available(t->ids[i],0x8867,&ready);if(!ready)continue;
        uint64_t ns=0;t->result(t->ids[i],0x8866,&ns);
        struct gpu_sample *s=&t->history[(t->pending[i]-1)%GPU_HISTORY];
        if(s->serial==t->pending[i]) {s->value=ns/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(ns/1000);s->valid=true;}
        t->pending[i]=0;
    }
    /* A clock change during retrieval invalidates these results too. */
    t->integer(0x8fbb,&disjoint);
    if(disjoint) {
        t->del(GPU_QUERIES,t->ids);t->gen(GPU_QUERIES,t->ids);
        memset(t->pending,0,sizeof(t->pending));
        memset(t->history,0,sizeof(t->history));t->head=t->count=0;
    }
}

static inline void gpu_timing_begin(struct gpu_timing *t)
{
    if(!t->supported)return;
    uint64_t serial=++t->serial;
    unsigned index=(unsigned)((serial-1)%GPU_HISTORY);
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    t->history[index]=(struct gpu_sample){.serial=serial,.stamp_us=(uint64_t)now.tv_sec*1000000+now.tv_nsec/1000};
    t->head=(index+1)%GPU_HISTORY;if(t->count<GPU_HISTORY)t->count++;
    for(int i=0;i<GPU_QUERIES;i++)if(!t->pending[i]) {
        t->active=i;t->pending[i]=serial;
        if(t->cycles)t->monitor_begin(t->ids[i]);else t->begin(GPU_ELAPSED,t->ids[i]);return;
    }
}
static inline void gpu_timing_end(struct gpu_timing *t)
{
    if(t->supported && t->active>=0) {
        if(t->cycles)t->monitor_end(t->ids[t->active]);else t->end(GPU_ELAPSED);
        t->active=-1;
    }
}
static inline void gpu_timing_destroy(struct gpu_timing *t)
{
    if(t->supported) {gpu_timing_end(t);t->del(GPU_QUERIES,t->ids);}
    memset(t,0,sizeof(*t));t->active=-1;
}
#endif
