#include "../src/gpu_timing.h"
#include <assert.h>
#include <stdio.h>

static unsigned ready,reads,begins,ends,deletes;
static int disjoint,bits=64,disjoint_after_read;
static uint64_t duration=2500000;
static void gen(int n,unsigned *ids) {for(int i=0;i<n;i++)ids[i]=(unsigned)i+1;}
static void del(int n,const unsigned *ids) {(void)n;(void)ids;deletes++;}
static void begin(unsigned target,unsigned id) {assert(target==GPU_ELAPSED && id);begins++;}
static void end(unsigned target) {assert(target==GPU_ELAPSED);ends++;}
static void available(unsigned id,unsigned name,unsigned *out) {(void)id;assert(name==0x8867);*out=ready;}
static void result(unsigned id,unsigned name,uint64_t *out) {(void)id;assert(name==0x8866 && ready);*out=duration;reads++;if(disjoint_after_read)disjoint=1;}
static void integer(unsigned name,int *out) {assert(name==0x8fbb);*out=disjoint;disjoint=0;}
static void query(unsigned target,unsigned name,int *out) {assert(target==GPU_ELAPSED && name==0x8864);*out=bits;}
static unsigned malformed;
static void groups(int *n,int size,unsigned *ids) {assert(size>=1);*n=1;ids[0]=7;}
static void counters(unsigned group,int *n,int *max,int size,unsigned *ids) {assert(group==7 && size>=2);*n=*max=2;ids[0]=14;ids[1]=15;}
static void counter_name(unsigned group,unsigned id,int size,int *length,char *name)
{(void)group;(void)length;snprintf(name,(size_t)size,"%s",id==14?"QPU-total-clk-cycles-vertex-coord-shading":"QPU-total-clk-cycles-fragment-shading");}
static void info(unsigned group,unsigned id,unsigned name,void *out) {(void)group;(void)id;assert(name==0x8bc0);*(unsigned *)out=0x8bc2;}
static void select_counters(unsigned id,unsigned char enable,unsigned group,int n,unsigned *ids)
{assert(id && enable && group==7 && n==2 && ids[0]==14 && ids[1]==15);}
static void monitor_begin(unsigned id) {assert(id);begins++;}
static void monitor_end(unsigned id) {assert(id);ends++;}
static void monitor_data(unsigned id,unsigned name,int size,unsigned *data,int *bytes)
{
    assert(id);
    if(name==0x8bc4) {*data=ready;return;}
    assert(name==0x8bc6 && size==32 && ready);reads++;
    unsigned result[8]={7,15,8000,0,7,14,2000,0};
    if(malformed==1)result[1]=99;
    if(malformed==2)result[5]=15;
    if(malformed==3)result[3]=1;
    memcpy(data,result,sizeof(result));*bytes=malformed==4?16:32;
}
static gpu_proc resolve(const char *name)
{
#define PROC(s,f) if(!strcmp(name,s))return (gpu_proc)f
    PROC("glGenQueriesEXT",gen);PROC("glDeleteQueriesEXT",del);
    PROC("glBeginQueryEXT",begin);PROC("glEndQueryEXT",end);
    PROC("glGetQueryObjectuivEXT",available);PROC("glGetQueryObjectui64vEXT",result);
    PROC("glGetIntegerv",integer);PROC("glGetQueryivEXT",query);
    PROC("glGetPerfMonitorGroupsAMD",groups);PROC("glGetPerfMonitorCountersAMD",counters);
    PROC("glGetPerfMonitorCounterStringAMD",counter_name);PROC("glGetPerfMonitorCounterInfoAMD",info);
    PROC("glSelectPerfMonitorCountersAMD",select_counters);
    PROC("glGenPerfMonitorsAMD",gen);PROC("glDeletePerfMonitorsAMD",del);
    PROC("glBeginPerfMonitorAMD",monitor_begin);PROC("glEndPerfMonitorAMD",monitor_end);
    PROC("glGetPerfMonitorCounterDataAMD",monitor_data);
#undef PROC
    return NULL;
}
static gpu_proc missing(const char *name) {(void)name;return NULL;}
int main(void)
{
    struct gpu_timing t;
    gpu_timing_init(&t,NULL,resolve);assert(!t.supported);
    gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);assert(!begins && !reads);
    gpu_timing_init(&t,"GL_EXT_disjoint_timer_query_extra",resolve);assert(!t.supported);
    gpu_timing_init(&t,"GL_EXT_disjoint_timer_query",missing);assert(!t.supported);
    bits=0;gpu_timing_init(&t,"GL_EXT_disjoint_timer_query",resolve);assert(!t.supported);bits=64;
    gpu_timing_init(&t,"GL_other GL_EXT_disjoint_timer_query GL_last",resolve);assert(t.supported);
    for(int i=0;i<GPU_QUERIES+2;i++) {gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);}
    assert(begins==GPU_QUERIES && ends==begins && reads==0); /* no blocking read or busy reuse */
    ready=1;gpu_timing_poll(&t);assert(reads==GPU_QUERIES);
    assert(t.history[0].valid && t.history[0].value==2500 && !t.history[GPU_QUERIES].valid);
    gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);assert(begins==GPU_QUERIES+1);
    disjoint=1;gpu_timing_poll(&t);assert(!t.count && !t.history[0].valid);
    gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);
    assert(t.count==1 && t.history[(t.head+GPU_HISTORY-1)%GPU_HISTORY].valid);
    disjoint_after_read=1;gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);assert(!t.count);
    disjoint_after_read=0;
    /* A delayed result must not overwrite a newer sample after history wraps. */
    ready=0;
    for(int i=0;i<GPU_HISTORY+10;i++) {gpu_timing_begin(&t);gpu_timing_end(&t);}
    ready=1;gpu_timing_poll(&t);
    for(int i=0;i<GPU_HISTORY;i++)assert(!t.history[i].valid);
    duration=UINT64_MAX;gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);
    assert(t.history[(t.head+GPU_HISTORY-1)%GPU_HISTORY].value==UINT32_MAX);
    gpu_timing_destroy(&t);unsigned old=deletes;gpu_timing_destroy(&t);assert(deletes==old && !t.supported);
    gpu_timing_init_counters(&t,"GL_AMD_performance_monitor",resolve);assert(t.supported && t.cycles);
    unsigned read_before=reads;ready=0;
    gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);assert(reads==read_before);
    ready=1;gpu_timing_poll(&t);assert(t.history[0].valid && t.history[0].value==10000);
    for(malformed=1;malformed<=4;malformed++) {
        gpu_timing_begin(&t);gpu_timing_end(&t);gpu_timing_poll(&t);
        struct gpu_sample *s=&t.history[(t.head+GPU_HISTORY-1)%GPU_HISTORY];
        if(malformed==3)assert(s->valid && s->value==UINT32_MAX);else assert(!s->valid);
    }
    gpu_timing_destroy(&t);
    puts("GPU timing: capability, asynchronous results, saturation, disjoint recovery, history wrap and cleanup passed.");
}
