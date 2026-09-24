#include "../src/profile.h"
#include <assert.h>
#include <stdio.h>
static void job(struct profile_shared *s,uint64_t begin,uint64_t end,uint64_t pid)
{
    uint64_t n=++s->total;s->jobs[(n-1)%PROFILE_JOBS]=(struct profile_job){n,begin,end,pid};
}
int main(void)
{
    struct profile_shared s={0};uint32_t us=0;
    assert(!profile_gpu(&s,100,200,42,&us));
    job(&s,110,130,42);job(&s,120,150,42);job(&s,160,170,42);
    assert(profile_gpu(&s,100,200,42,&us) && us==50); /* overlap counted once */
    assert(!profile_gpu(&s,115,200,42,&us));
    assert(!profile_gpu(&s,100,145,42,&us));
    assert(!profile_gpu(&s,100,200,9,&us));
    job(&s,175,180,0);assert(!profile_gpu(&s,100,200,42,&us)); /* lost/foreign */
    s.total--;s.jobs[0].serial=7;assert(!profile_gpu(&s,100,200,42,&us));
    s=(struct profile_shared){0};
    for(unsigned i=0;i<PROFILE_JOBS+2;i++)job(&s,100+i*10,105+i*10,42);
    assert(!profile_gpu(&s,100,3000,42,&us)); /* overwritten history */
    assert(profile_gpu(&s,2600,2700,42,&us) && us==40);
    struct profile p={0};profile_reset(&p,true,100);
    for(unsigned i=0;i<PROFILE_HISTORY+5;i++)profile_push(&p,i*100,i*100+90,i);
    assert(p.count==PROFILE_HISTORY && p.drop_base==100);
    profile_reset(&p,false,101);assert(!p.count && !p.enabled);
    /* Window is a wall-clock second, not a fixed frame count: works at 30 Hz,
       after stalls, and when GPU results arrive late. */
    profile_push(&p,0,900000,90000);
    p.frames[0].valid=true;p.frames[0].gpu=80000;
    profile_push(&p,0,1500000,2000);
    p.frames[1].valid=true;p.frames[1].gpu=500;
    profile_push(&p,0,1900000,4000); /* GPU result still pending. */
    struct profile_stats cpu=profile_window(&p,2000000,false);
    struct profile_stats gpu=profile_window(&p,2000000,true);
    assert(cpu.count==2 && cpu.latest==4000 && cpu.sum/cpu.count==3000 && cpu.maximum==4000);
    assert(gpu.count==1 && gpu.latest==500 && gpu.maximum==500);
    p.frames[2].valid=true;p.frames[2].gpu=700;
    gpu=profile_window(&p,2000000,true);assert(gpu.count==2 && gpu.latest==700 && gpu.sum/2==600);
    cpu=profile_window(&p,2500000,false);assert(cpu.count==1 && cpu.maximum==4000);
    assert(!profile_window(&p,2900000,false).count); /* exactly one second expires */
    struct profile_stats future={0};profile_stats_add(&future,9000,3000000,2000000);assert(!future.count);
    puts("Profile: multi-job union, boundaries, foreign/lost events, overwritten history and session reset passed.");
}
