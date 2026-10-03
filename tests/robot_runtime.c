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

static struct chirky_mesh_vertex vertices[12288];
static size_t vertex_count;
static unsigned draws;
static struct trace_capture capture;
static void scope(void *context,const char *name,bool begin)
{ (void)context;trace_scope(&capture,name,begin,draws); }
static bool mesh(void *context,const struct chirky_mesh_vertex *v,size_t count,float ambient)
{
    (void)context;draws++;assert(count && count%3==0 && count<=12288 && ambient>=0 && ambient<=1);
    for(size_t i=0;i<count;i++)assert(isfinite(v[i].x) && isfinite(v[i].y) && isfinite(v[i].z) && fabsf(v[i].z)<=1);
    memcpy(vertices,v,count*sizeof(*v));vertex_count=count;return true;
}
static uint64_t hash(void)
{
    uint64_t result=1469598103934665603ull;
    for(size_t i=0;i<vertex_count*sizeof(*vertices);i++)result=(result^((unsigned char *)vertices)[i])*1099511628211ull;
    return result;
}
static void eye_center(float *x,float *y)
{
    unsigned count=0;*x=*y=0;
    for(size_t i=0;i<vertex_count;i++) {
        const struct chirky_mesh_vertex *v=&vertices[i];
        if(v->emissive && v->g>v->r && v->g>v->b+30) {
            *x+=v->x;*y+=v->y;count++;
        }
    }
    assert(count);*x/=count;*y/=count;
}
static double seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
static uint64_t casing_hash(float *mouth_y)
{
    uint64_t value=1469598103934665603ull;*mouth_y=1000;
    for(size_t i=0;i<vertex_count;i++) {
        const struct chirky_mesh_vertex *v=&vertices[i];
        if(v->emissive && v->g>v->r && v->g>v->b+30){*mouth_y=fminf(*mouth_y,v->y);continue;}
        for(size_t j=0;j<sizeof(*v);j++)value=(value^((const unsigned char *)v)[j])*1099511628211ull;
    }
    return value;
}
int main(void)
{
    struct chirky_host_api api={.screen_width=64,.screen_height=64,.draw_mesh=mesh};
    const char *config="games/phosphor-run/game.conf";
    assert(robot_load(config) && robot_ready());
    assert(trace_arm(&capture,1,16667));capture.recording=true;api.profile_scope=scope;
    assert(robot_draw(&api,32,4,1,ROBOT_RUN,0));
    assert(!capture.invalid && !capture.depth && capture.count==5);
    uint64_t profiled_hash=hash();
    api.profile_scope=NULL;memset(vertices,0,sizeof(vertices));
    assert(robot_draw(&api,32,4,1,ROBOT_RUN,0));assert(hash()==profiled_hash);
    memset(vertices,0,sizeof(vertices));draws=0;
    assert(robot_draw_scaled(&api,32,4,1,ROBOT_RUN,0,2));
    assert(draws==1);
    assert(!robot_draw_scaled(&api,32,4,1,ROBOT_RUN,0,0));
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
        if(tick==72){assert(fabsf(motion.roll_speed)*.396f*8<.088f);assert(motion.lean>.15f);}
        fprintf(trace,"%d,%d,%.7f,%.7f,%.7f\n",tick+1,intent,distance,motion.roll,motion.lean);
        struct robot_motion saved=motion;
        assert(robot_draw_weighted(&api,32,4,1,intent?ROBOT_RUN:ROBOT_IDLE,(float)tick,&motion));
        assert(!memcmp(&motion,&saved,sizeof(motion)));
    }
    fclose(trace);assert(fabsf(motion.lean)<.001f && motion.roll_speed==0);
    for(int clip=0;clip<6;clip++) {
        uint64_t frames[4];
        for(int frame=0;frame<4;frame++) {
            draws=0;assert(robot_draw(&api,32,4,frame==3?-1:1,(enum robot_clip)clip,(float)(frame*5)));
            assert(draws==1);frames[frame]=hash();
            robot_draw(&api,32,4,frame==3?-1:1,(enum robot_clip)clip,(float)(frame*5));
            assert(hash()==frames[frame]);
        }
        assert(frames[0]!=frames[3]);
        if(clip==ROBOT_RUN || clip==ROBOT_JUMP || clip==ROBOT_DEATH)assert(frames[0]!=frames[2]);
    }
    /* Falling moves the body immediately, with a delayed head and a bounded
       suspension stretch that settles rather than hanging above it forever. */
    for(int facing=-1;facing<=1;facing+=2) {
        float x0,y0,x4,y4,x36,y36,x120,y120;
        assert(robot_draw(&api,32,40,facing,ROBOT_FALL,0));eye_center(&x0,&y0);
        assert(robot_draw(&api,32,32,facing,ROBOT_FALL,4));eye_center(&x4,&y4);
        assert(fabsf(x4-x0)<.001f); /* Head pitch is still held at frame zero. */
        assert(y4>y0-7 && y4<y0-5); /* Body fell 8 px; head initially falls less. */
        assert(robot_draw(&api,32,32,facing,ROBOT_FALL,36));eye_center(&x36,&y36);
        assert(robot_draw(&api,32,32,facing,ROBOT_FALL,120));eye_center(&x120,&y120);
        assert(fabsf(x36-x120)<.001f && fabsf(y36-y120)<.05f);
    }
    /* Camera magnification changes geometry, never a pre-rasterized pixel grid. */
    robot_draw(&api,32,4,1,ROBOT_IDLE,0);float x=vertices[0].x,y=vertices[0].y;
    robot_draw_view(&api,32,4,1,ROBOT_IDLE,0,NULL,5);
    assert(fabsf(vertices[0].x-(32+(x-32)*5))<.001f);
    assert(fabsf(vertices[0].y-(4+(y-4)*5))<.001f);
    struct robot_face face,copy;robot_face_init(&face);
    /* Idle rests, leads a glance with the eyes, then settles back to neutral.
       A continuous sine wave or immediate head snap breaks these holds. */
    for(int i=0;i<59;i++)robot_face_update(&face);
    assert(face.gaze_x==0 && face.head_look==0);
    robot_face_update(&face);
    assert(face.gaze_x>0 && face.head_look>0 && face.head_look<face.gaze_x*.1f);
    while(face.tick<130)robot_face_update(&face);
    assert(face.head_look>.75f && face.head_look<.81f);
    while(face.tick<260)robot_face_update(&face);
    assert(fabsf(face.gaze_x)<.001f && fabsf(face.head_look)<.001f);
    robot_face_init(&face);robot_face_init(&copy);
    for(int i=0;i<110;i++){robot_face_update(&face);robot_face_update(&copy);}
    assert(face.blink==1 && !memcmp(&face,&copy,sizeof(face)));
    /* The same blink works during EVERY clip. It never squashes the smile,
       moves the casing, advances simulation, or starts a second mesh draw. */
    for(int clip=0;clip<6;clip++) {
        float mouth_open,mouth_closed;
        robot_style.blink=0;draws=0;
        assert(robot_draw_character(&api,32,4,1,clip,0,NULL,&face,1));
        uint64_t open=hash(),casing=casing_hash(&mouth_open);
        robot_style.blink=1;
        assert(robot_draw_character(&api,32,4,1,clip,0,NULL,&face,1));
        assert(hash()!=open && casing_hash(&mouth_closed)==casing && fabsf(mouth_closed-mouth_open)<.001f);
        assert(draws==2 && !memcmp(&face,&copy,sizeof(face)));
    }
    for(int i=0;i<500;i++) {
        robot_face_update(&face);robot_face_update(&copy);
        assert(face.blink>=0 && face.blink<=1 && fabsf(face.gaze_x)<=1 && fabsf(face.gaze_y)<=1);
        assert(robot_draw_character(&api,32,4,i%2?1:-1,i%6,0,NULL,&face,1));
        assert(!memcmp(&face,&copy,sizeof(face))); /* Changing clips cannot reset face time. */
    }
    uint64_t moods[4];
    for(int mood=0;mood<4;mood++) {
        robot_face_init(&face);face.expression=mood;
        for(int i=0;i<60;i++)robot_face_update(&face);
        assert(robot_draw_character(&api,32,4,1,ROBOT_IDLE,0,NULL,&face,1));moods[mood]=hash();
        for(int j=0;j<mood;j++)assert(moods[mood]!=moods[j]);
    }
    robot_face_init(&face);robot_face_blink(&face);
    for(int i=0;i<3;i++)robot_face_update(&face);
    assert(face.blink==1);
    for(int i=0;i<10;i++)robot_face_update(&face);
    assert(face.blink==0);
    /* Automatic idle moods have sustained, varied holds and neutral rests.
       Their private random clock never restarts gaze/blinking or changes on draw. */
    robot_face_init(&face);robot_face_init(&copy);
    face.expression=copy.expression=ROBOT_FACE_AUTO;face.idle=copy.idle=true;
    unsigned seen=0,changes=0;
    for(int i=0;i<7200;i++) {
        enum robot_expression before=face.idle_expression;
        float smile=face.smile;
        robot_face_update(&face);robot_face_update(&copy);
        assert(!memcmp(&face,&copy,sizeof(face)) && face.tick==(unsigned)(i+1)%960);
        assert(fabsf(face.smile-smile)<.11f);
        seen|=1u<<face.idle_expression;
        if(face.idle_expression!=before) {
            changes++;
            if(before==ROBOT_FACE_NEUTRAL)assert(face.expression_ticks>=120 && face.expression_ticks<=240);
            else assert(face.idle_expression==ROBOT_FACE_NEUTRAL && face.expression_ticks>=180 && face.expression_ticks<=360);
        }
        if(i<89)assert(face.idle_expression==ROBOT_FACE_NEUTRAL);
    }
    assert(changes>20 && changes<50 && seen==15);
    uint32_t seed=face.expression_random;unsigned face_tick=face.tick;
    face.idle=false;
    for(int i=0;i<240;i++)robot_face_update(&face);
    assert(face.idle_expression==ROBOT_FACE_NEUTRAL && face.expression_random==seed);
    assert(face.tick==(face_tick+240)%960 && fabsf(face.left_open-1)<.001f);
    face.expression=ROBOT_FACE_HAPPY;
    for(int i=0;i<60;i++)robot_face_update(&face);
    assert(fabsf(face.left_open-.72f)<.001f && face.expression_random==seed);
    struct chirky_host_api unsupported=api;unsupported.draw_mesh=NULL;
    assert(!robot_draw(&unsupported,32,4,1,ROBOT_IDLE,0));
    double start=seconds();
    for(int i=0;i<1000;i++)robot_draw(&api,32,4,1,ROBOT_RUN,(float)i);
    printf("Robot: 1000 animated mesh frames, %.3f ms/frame on this machine (pose + one mesh callback).\n",(seconds()-start));
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
    assert(!memcmp(data,"PRB2",4));
    unsigned vertex_offset=28+84+data[16]*4;
    for(int mutation=0;mutation<10;mutation++) {
        memcpy(data,original,(size_t)length);
        if(mutation==1)data[4]=255; /* Vertex count no longer matches the stream. */
        if(mutation==4)data[vertex_offset+12]=255; /* Invalid bone. */
        if(mutation==5){data[vertex_offset+2]=0xc0;data[vertex_offset+3]=0x7f;} /* NaN. */
        if(mutation==6){data[vertex_offset+vertex_count*20]=255;data[vertex_offset+vertex_count*20+1]=255;}
        if(mutation==7)data[vertex_offset+16]=255; /* Unknown face part. */
        if(mutation==8)memset(data+28,0,4); /* Invalid body radius. */
        if(mutation==9)memset(data+28+7*4,0,12); /* Invalid U basis. */
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
