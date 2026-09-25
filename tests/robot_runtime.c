#include "robot.h"
#include "../src/trace.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char pixels[64][64][3];
static unsigned draws;
static struct trace_capture capture;
static void scope(void *context,const char *name,bool begin)
{
    (void)context;trace_scope(&capture,name,begin,draws);
}
static void fill(void *context,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)context;draws++;
    assert(w>0 && h>0);
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)if(xx>=0 && xx<64 && yy>=0 && yy<64) {
        pixels[yy][xx][0]=r;pixels[yy][xx][1]=g;pixels[yy][xx][2]=b;
    }
}
static uint64_t hash(void)
{
    uint64_t result=1469598103934665603ull;
    for(size_t i=0;i<sizeof(pixels);i++)result=(result^((unsigned char *)pixels)[i])*1099511628211ull;
    return result;
}
static double seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
int main(void)
{
    struct chirky_host_api api={.screen_width=64,.screen_height=64,.fill_rect=fill};
    const char *config="games/phosphor-run/game.conf";
    assert(robot_load(config) && robot_ready());
    assert(trace_arm(&capture,1,16667));capture.recording=true;api.profile_scope=scope;
    assert(robot_draw(&api,32,4,1,ROBOT_RUN,0));
    assert(!capture.invalid && !capture.depth && capture.count==7);
    uint64_t profiled_hash=hash();
    api.profile_scope=NULL;memset(pixels,0,sizeof(pixels));
    assert(robot_draw(&api,32,4,1,ROBOT_RUN,0));assert(hash()==profiled_hash);
    free(capture.spans);capture=(struct trace_capture){0};
    struct robot_motion motion={0},mirror={0};
    FILE *trace=fopen("build/robot-motion.csv","w");assert(trace);
    fputs("frame,intent,distance,roll,lean\n",trace);
    for(int tick=0;tick<150;tick++) {
        int intent=tick>=15 && tick<70?1:0;
        float distance=intent?fminf((tick-14)*.34f,2.35f):0;
        robot_motion_update(&motion,intent,distance,true);
        robot_motion_update(&mirror,-intent,-distance,true);
        assert(fabsf(motion.lean+mirror.lean)<.00001f && fabsf(motion.roll+mirror.roll)<.00001f);
        if(tick>=15 && tick<19){assert(motion.lean>0);assert(motion.roll==0);}
        if(tick==20)assert(motion.roll>0);
        if(tick==72){assert(fabsf(motion.roll_speed)<.02f);assert(motion.lean>.15f);}
        fprintf(trace,"%d,%d,%.7f,%.7f,%.7f\n",tick+1,intent,distance,motion.roll,motion.lean);
        struct robot_motion saved=motion;
        assert(robot_draw_weighted(&api,32,4,1,intent?ROBOT_RUN:ROBOT_IDLE,(float)tick,&motion));
        assert(!memcmp(&motion,&saved,sizeof(motion)));
    }
    fclose(trace);assert(fabsf(motion.lean)<.001f && motion.roll_speed==0);
    static unsigned char sheet[512][768][3];
    for(int clip=0;clip<6;clip++) {
        uint64_t frames[4];
        for(int frame=0;frame<4;frame++) {
            memset(pixels,0,sizeof(pixels));draws=0;
            assert(robot_draw(&api,32,4,frame==3?-1:1,(enum robot_clip)clip,(float)(frame*5)));
            assert(draws>40 && draws<500);frames[frame]=hash();
            unsigned char copy[sizeof(pixels)];memcpy(copy,pixels,sizeof(pixels));
            robot_draw(&api,32,4,frame==3?-1:1,(enum robot_clip)clip,(float)(frame*5));
            assert(!memcmp(copy,pixels,sizeof(pixels)));
            for(int y=0;y<32;y++)for(int x=0;x<32;x++)for(int yy=0;yy<4;yy++)for(int xx=0;xx<4;xx++)
                memcpy(sheet[frame*128+(31-y)*4+yy][clip*128+x*4+xx],pixels[y][x+16],3);
        }
        assert(frames[0]!=frames[3]);
        if(clip==ROBOT_RUN || clip==ROBOT_JUMP || clip==ROBOT_DEATH)assert(frames[0]!=frames[2]);
    }
    FILE *image=fopen("build/robot-poses.ppm","wb");assert(image);
    fprintf(image,"P6\n768 512\n255\n");fwrite(sheet,1,sizeof(sheet),image);fclose(image);
    double start=seconds();
    for(int i=0;i<1000;i++)robot_draw(&api,32,4,1,ROBOT_RUN,(float)i);
    printf("Robot: 1000 animated mesh frames, %.3f ms/frame on this machine (includes pixel callback).\n",(seconds()-start));
    /* Offscreen and partially clipped draws remain bounded. */
    draws=0;assert(robot_draw(&api,-1000,4,1,ROBOT_IDLE,0));assert(!draws);
    assert(robot_draw(&api,0,-3,-1,ROBOT_DASH,4));
    assert(robot_draw(&api,64,60,1,ROBOT_JUMP,4));
    assert(!robot_draw(&api,32,4,1,(enum robot_clip)99,0));
    robot_free();robot_free();assert(!robot_ready());
    assert(!robot_draw(&api,32,4,1,ROBOT_IDLE,0));
    assert(!robot_load("/missing/game.conf"));
    /* Corrupt/truncated files must clean up rather than expose partial models. */
    char temp[]="/tmp/chirky-robot-XXXXXX";assert(mkdtemp(temp));char path[512],config_path[512];
    snprintf(path,sizeof(path),"%s/assets",temp);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/assets/models",temp);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/assets/models/player.robot",temp);
    snprintf(config_path,sizeof(config_path),"%s/game.conf",temp);
    FILE *source=fopen("games/phosphor-run/assets/models/player.robot","rb");assert(source);
    fseek(source,0,SEEK_END);long length=ftell(source);rewind(source);
    unsigned char *data=malloc((size_t)length);assert(data);assert(fread(data,1,(size_t)length,source)==(size_t)length);fclose(source);
    unsigned char *original=malloc((size_t)length);assert(original);memcpy(original,data,(size_t)length);
    unsigned vertex_count=(unsigned)data[4]|(unsigned)data[5]<<8;
    unsigned vertex_offset=28+data[16]*4;
    for(int mutation=0;mutation<7;mutation++) {
        memcpy(data,original,(size_t)length);
        if(mutation==1)data[4]=255; /* Vertex count no longer matches the stream. */
        if(mutation==4)data[vertex_offset+12]=255; /* Invalid bone. */
        if(mutation==5){data[vertex_offset+2]=0xc0;data[vertex_offset+3]=0x7f;} /* NaN. */
        if(mutation==6){data[vertex_offset+vertex_count*16]=255;data[vertex_offset+vertex_count*16+1]=255;}
        FILE *file=fopen(path,"wb");assert(file);
        fwrite(data,1,mutation==0?20:mutation==2?(size_t)length-1:(size_t)length,file);
        if(mutation==3)fputc(1,file);
        fclose(file);
        assert(!robot_load(config_path));assert(!robot_ready());
    }
    free(original);free(data);unlink(path);snprintf(path,sizeof(path),"%s/assets/models",temp);rmdir(path);
    snprintf(path,sizeof(path),"%s/assets",temp);rmdir(path);rmdir(temp);
    assert(robot_load(config));robot_free();
    puts("Robot: six clips, facing, determinism, bounds, corrupt assets and lifecycle passed.");
}
