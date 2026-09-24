#ifndef CHIRKY_PROFILE_H
#define CHIRKY_PROFILE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define PROFILE_HISTORY 120
#define PROFILE_JOBS 256
#define PROFILE_MAGIC UINT64_C(0x434849524b594750)
struct profile_job { uint64_t serial,start,end,pid; };
struct profile_shared {
    uint64_t magic,generation,watermark,total;
    struct profile_job jobs[PROFILE_JOBS];
};
struct profile_frame {
    uint64_t start,end;
    uint32_t cpu,gpu;
    bool resolved,valid;
};
struct profile {
    struct profile_frame frames[PROFILE_HISTORY];
    unsigned head,count;
    uint64_t drop_base,flash_until,retry_after;
    bool enabled;
};
struct profile_stats {
    uint64_t sum,latest_stamp;
    uint32_t latest,maximum,count;
};
static inline void profile_stats_add(struct profile_stats *s,uint32_t value,uint64_t stamp,uint64_t now)
{
    if(stamp>now || now-stamp>=1000000)return;
    if(!s->count || stamp>s->latest_stamp){s->latest=value;s->latest_stamp=stamp;}
    s->sum+=value;s->count++;
    if(value>s->maximum)s->maximum=value;
}
static inline struct profile_stats profile_window(const struct profile *p,uint64_t now,bool gpu)
{
    struct profile_stats s={0};
    for(unsigned age=0;age<p->count;age++) {
        const struct profile_frame *f=&p->frames[(p->head+PROFILE_HISTORY-1-age)%PROFILE_HISTORY];
        if(!gpu || f->valid)profile_stats_add(&s,gpu?f->gpu:f->cpu,f->end,now);
    }
    return s;
}
static inline void profile_reset(struct profile *p,bool enabled,uint64_t drops)
{
    memset(p,0,sizeof(*p));p->enabled=enabled;p->drop_base=drops;
}
/* All jobs in a frame are merged into a union, so overlapping GPU jobs are
   never double-counted. Cross-frame, foreign, missing and lost events invalidate
   the sample rather than producing an optimistic zero. */
static inline bool profile_gpu(const struct profile_shared *s,uint64_t start,uint64_t end,
                               uint64_t pid,uint32_t *us)
{
    struct profile_job jobs[PROFILE_JOBS];unsigned count=0;
    uint64_t first=s->total>PROFILE_JOBS?s->total-PROFILE_JOBS+1:1;
    if(first>1 && s->jobs[(first-1)%PROFILE_JOBS].end>=start)return false;
    for(uint64_t serial=first;serial<=s->total;serial++) {
        struct profile_job j=s->jobs[(serial-1)%PROFILE_JOBS];
        if(j.serial!=serial)return false;
        if(j.end<=start || j.start>=end)continue;
        if(j.pid!=pid || j.start<start || j.end>end || j.end<j.start)return false;
        unsigned i=count++;
        while(i && jobs[i-1].start>j.start){jobs[i]=jobs[i-1];i--;}
        jobs[i]=j;
    }
    if(!count)return false;
    uint64_t begin=jobs[0].start,finish=jobs[0].end,total=0;
    for(unsigned i=1;i<count;i++) {
        if(jobs[i].start>finish){total+=finish-begin;begin=jobs[i].start;}
        if(jobs[i].end>finish)finish=jobs[i].end;
    }
    total+=finish-begin;*us=total>UINT32_MAX?UINT32_MAX:(uint32_t)total;return true;
}
static inline void profile_poll(struct profile *p,uint64_t now)
{
    if(now<p->retry_after)return;
    p->retry_after=now+20000;
    int fd=open("/run/chirky-gpu/samples",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return;
    struct profile_shared s;struct stat st;
    bool ok=!fstat(fd,&st) && st.st_size==sizeof(s) && read(fd,&s,sizeof(s))==sizeof(s);
    close(fd);
    if(!ok || s.magic!=PROFILE_MAGIC || now>s.watermark+2000000)return;
    for(unsigned i=0;i<p->count;i++) {
        struct profile_frame *f=&p->frames[(p->head+PROFILE_HISTORY-1-i)%PROFILE_HISTORY];
        if(f->resolved || f->end>s.watermark)continue;
        f->resolved=true;f->valid=profile_gpu(&s,f->start,f->end,(uint64_t)getpid(),&f->gpu);
    }
}
static inline void profile_push(struct profile *p,uint64_t start,uint64_t end,uint32_t cpu)
{
    p->frames[p->head]=(struct profile_frame){.start=start,.end=end,.cpu=cpu};
    p->head=(p->head+1)%PROFILE_HISTORY;if(p->count<PROFILE_HISTORY)p->count++;
}
#endif
