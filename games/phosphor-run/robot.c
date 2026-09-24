#include "robot.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* One small animated 3D actor, composed through the existing rectangle API.
   2x samples resolve to 32x32 pixels; no full-screen software rendering, GL
   state changes, textures, runtime allocation or native/browser ABI changes. */
#define SIDE 64
#define LIMIT_VERTICES 2048
#define LIMIT_TRIANGLES 4096
#define LIMIT_BONES 32
#define LIMIT_FRAMES 256
struct vertex { float p[3]; uint32_t bone; };
struct triangle { uint16_t v[3], material; };
struct material { uint8_t r,g,b,emissive; };
struct clip { uint32_t first,count,ticks,loop; };
struct projected { float x,y,z; };
static struct {
    unsigned vertices,triangles,bones,materials,frames;
    struct vertex *vertex;
    struct triangle *triangle;
    struct material palette[32];
    struct clip clips[6];
    float *poses;
} model;
static struct projected transformed[LIMIT_VERTICES];
static float depth[SIDE*SIDE];
static uint32_t samples[SIDE*SIDE], resolved[SIDE*SIDE/4];

static bool word(FILE *file,uint32_t *out)
{
    unsigned char b[4];if(fread(b,1,4,file)!=4)return false;
    *out=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
    return true;
}
static bool real(FILE *file,float *out)
{
    uint32_t bits;if(!word(file,&bits))return false;
    memcpy(out,&bits,4);return isfinite(*out) && fabsf(*out)<=64.f;
}
void robot_free(void)
{
    free(model.vertex);free(model.triangle);free(model.poses);memset(&model,0,sizeof(model));
}
bool robot_ready(void) { return model.poses!=NULL; }
bool robot_load(const char *config)
{
    robot_free();if(!config)return false;
    const char *slash=strrchr(config,'/');
    const char *backslash=strrchr(config,'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    char path[1024];int prefix=slash?(int)(slash-config+1):0;
    if(snprintf(path,sizeof(path),"%.*sassets/models/player.robot",prefix,config)>=(int)sizeof(path))return false;
    FILE *file=fopen(path,"rb");if(!file)return false;
    char magic[4];uint32_t counts[6];bool ok=fread(magic,1,4,file)==4 && !memcmp(magic,"PRB1",4);
    for(int i=0;i<6 && ok;i++)ok=word(file,&counts[i]);
    if(!ok || !counts[0] || counts[0]>LIMIT_VERTICES || !counts[1] || counts[1]>LIMIT_TRIANGLES ||
       !counts[2] || counts[2]>LIMIT_BONES || !counts[3] || counts[3]>32 || counts[4]!=6 ||
       !counts[5] || counts[5]>LIMIT_FRAMES)goto invalid;
    model.vertices=counts[0];model.triangles=counts[1];model.bones=counts[2];model.materials=counts[3];model.frames=counts[5];
    model.vertex=calloc(model.vertices,sizeof(*model.vertex));
    model.triangle=calloc(model.triangles,sizeof(*model.triangle));
    size_t matrices=(size_t)model.frames*model.bones*12;
    model.poses=calloc(matrices,sizeof(float));
    if(!model.vertex || !model.triangle || !model.poses)goto invalid;
    if(fread(model.palette,4,model.materials,file)!=model.materials)goto invalid;
    for(unsigned i=0;i<model.vertices;i++) {
        for(int j=0;j<3;j++)if(!real(file,&model.vertex[i].p[j]))goto invalid;
        if(!word(file,&model.vertex[i].bone) || model.vertex[i].bone>=model.bones)goto invalid;
    }
    for(unsigned i=0;i<model.triangles;i++) {
        unsigned char b[8];if(fread(b,1,8,file)!=8)goto invalid;
        for(int j=0;j<3;j++) {
            model.triangle[i].v[j]=(uint16_t)(b[j*2]|b[j*2+1]<<8);
            if(model.triangle[i].v[j]>=model.vertices)goto invalid;
        }
        model.triangle[i].material=(uint16_t)(b[6]|b[7]<<8);
        if(model.triangle[i].material>=model.materials)goto invalid;
    }
    for(int i=0;i<6;i++) {
        struct clip *c=&model.clips[i];
        if(!word(file,&c->first) || !word(file,&c->count) || !word(file,&c->ticks) || !word(file,&c->loop) ||
           !c->count || c->first>=model.frames || c->count>model.frames-c->first ||
           !c->ticks || c->ticks>600 || c->loop>1)goto invalid;
    }
    for(size_t i=0;i<matrices;i++)if(!real(file,&model.poses[i]))goto invalid;
    if(fgetc(file)!=EOF)goto invalid;
    fclose(file);return true;
invalid:
    fclose(file);robot_free();return false;
}

static float edge(struct projected a,struct projected b,float x,float y)
{ return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
static int bounded(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }

/* Fixed 60 Hz secondary motion. Gameplay remains responsive; only the visual
   drive train has a four-tick take-up. The head spring keeps moving after the
   ball brakes, then settles without snapping back to the idle pose. */
void robot_motion_update(struct robot_motion *m,int intent,float distance,bool grounded)
{
    if(!m)return;
    if(!isfinite(distance))distance=0;
    distance=fmaxf(-8,fminf(8,distance));
    intent=intent<0?-1:intent>0?1:0;
    if(grounded && intent && !m->intent && fabsf(m->roll_speed)<.06f)m->delay=4;
    float target=(grounded?.30f:.12f)*intent;
    m->lean_speed=(m->lean_speed+(target-m->lean)*.065f)*.78f;
    m->lean+=m->lean_speed;
    if(fabsf(m->lean)<.0001f && fabsf(m->lean_speed)<.0001f && !intent)m->lean=m->lean_speed=0;
    float desired=distance/4.4f; /* 0.55 model radius at eight pixels/unit. */
    if(m->delay>0 && grounded){m->delay--;desired=0;}
    if(!intent && grounded)desired=0; /* Brakes lead the head's follow-through. */
    m->roll_speed+=(desired-m->roll_speed)*(intent?.38f:.70f);
    if(fabsf(m->roll_speed)<.0001f)m->roll_speed=0;
    m->roll=fmodf(m->roll+m->roll_speed,6.283185307f);
    m->intent=intent;
}

bool robot_draw(const struct chirky_host_api *api,int cx,int floor,int facing,enum robot_clip animation,float tick)
{ return robot_draw_weighted(api,cx,floor,facing,animation,tick,NULL); }

bool robot_draw_weighted(const struct chirky_host_api *api,int cx,int floor,int facing,enum robot_clip animation,float tick,const struct robot_motion *motion)
{
    if(!robot_ready() || !api || !api->fill_rect || animation<0 || animation>ROBOT_DEATH)return false;
    if(cx+16<0 || cx-16>=api->screen_width || floor+30<0 || floor-2>=api->screen_height)return true;
    if(!isfinite(tick) || tick<0)tick=0;
    const struct clip *clip=&model.clips[animation];
    float frame=tick/clip->ticks;
    if(clip->loop)frame=fmodf(frame,(float)clip->count);
    else if(frame>clip->count-1)frame=(float)(clip->count-1);
    unsigned a=(unsigned)frame,b=a+1;
    if(b>=clip->count)b=clip->loop?0:a;
    float blend=frame-a,pose[LIMIT_BONES*12];
    const float *pa=model.poses+(clip->first+a)*model.bones*12;
    const float *pb=model.poses+(clip->first+b)*model.bones*12;
    for(unsigned i=0;i<model.bones*12;i++)pose[i]=pa[i]+(pb[i]-pa[i])*blend;
    /* Ball rig order is root/body/head. Replace cyclic ball rotation with a
       continuous, distance-driven angle; keep authored jump/fall head poses. */
    float lean=0;
    if(motion && model.bones==3 && animation!=ROBOT_DEATH) {
        float angle=motion->roll*(facing<0?-1:1),c=cosf(angle),s=sinf(angle);
        float *body=pose+12;
        const float rotation[12]={1,0,0,0, 0,c,-s,.55f*s, 0,s,c,.55f*(1-c)};
        memcpy(body,rotation,sizeof(rotation));
        if(animation==ROBOT_IDLE || animation==ROBOT_RUN) {
            const float neutral[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
            memcpy(pose+24,neutral,sizeof(neutral));
        }
        lean=motion->lean*(facing<0?-1:1);
    }
    float lean_cos=cosf(lean),lean_sin=sinf(lean);
    for(unsigned i=0;i<model.vertices;i++) {
        const struct vertex *v=&model.vertex[i];const float *m=pose+v->bone*12;
        float p[3];for(int j=0;j<3;j++)p[j]=m[j*4]*v->p[0]+m[j*4+1]*v->p[1]+m[j*4+2]*v->p[2]+m[j*4+3];
        if(motion && model.bones==3 && v->bone==2 && animation!=ROBOT_DEATH) {
            float y=p[1],z=p[2]-1.16f;
            p[1]=lean_cos*y-lean_sin*z-lean*.55f;
            p[2]=1.16f+lean_sin*y+lean_cos*z;
        }
        float horizontal=.82f*p[0]-.57f*p[1],towards=-.57f*p[0]-.82f*p[1];
        transformed[i]=(struct projected){32+(facing<0?-1:1)*horizontal*16,
            4+(p[2]*.984f-towards*.178f)*16,towards*.984f+p[2]*.178f};
    }
    for(int i=0;i<SIDE*SIDE;i++){depth[i]=-FLT_MAX;samples[i]=0;}
    for(unsigned i=0;i<model.triangles;i++) {
        const struct triangle *tri=&model.triangle[i];
        struct projected v0=transformed[tri->v[0]],v1=transformed[tri->v[1]],v2=transformed[tri->v[2]];
        float area=edge(v0,v1,v2.x,v2.y);
        if(fabsf(area)<.001f)continue;
        /* Flat lighting uses the animated normal, with a fixed upper-left key.
           Two-sided rasterisation also handles mirroring without winding bugs. */
        float ux=(v1.x-v0.x)/16,uy=(v1.y-v0.y)/16,uz=v1.z-v0.z;
        float vx=(v2.x-v0.x)/16,vy=(v2.y-v0.y)/16,vz=v2.z-v0.z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        if(nz<0){nx=-nx;ny=-ny;nz=-nz;}
        float length=sqrtf(nx*nx+ny*ny+nz*nz);
        if(length<.000001f)continue;
        float light=(-.35f*nx+.60f*ny+.72f*nz)/length;
        light=.42f+.58f*fmaxf(0,light);
        struct material mat=model.palette[tri->material];if(mat.emissive)light=1;
        uint32_t colour=0xff000000u|((uint32_t)(mat.r*light)<<16)|((uint32_t)(mat.g*light)<<8)|(uint32_t)(mat.b*light);
        int x0=bounded((int)floorf(fminf(v0.x,fminf(v1.x,v2.x))),0,SIDE-1);
        int x1=bounded((int)ceilf(fmaxf(v0.x,fmaxf(v1.x,v2.x))),0,SIDE-1);
        int y0=bounded((int)floorf(fminf(v0.y,fminf(v1.y,v2.y))),0,SIDE-1);
        int y1=bounded((int)ceilf(fmaxf(v0.y,fmaxf(v1.y,v2.y))),0,SIDE-1);
        float inverse=1/area;
        for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++) {
            float w0=edge(v1,v2,x+.5f,y+.5f)*inverse,w1=edge(v2,v0,x+.5f,y+.5f)*inverse,w2=1-w0-w1;
            if(w0<-.00001f || w1<-.00001f || w2<-.00001f)continue;
            float z=w0*v0.z+w1*v1.z+w2*v2.z;int pixel=y*SIDE+x;
            if(z>depth[pixel]){depth[pixel]=z;samples[pixel]=colour;}
        }
    }
    for(int y=0;y<SIDE/2;y++)for(int x=0;x<SIDE/2;x++) {
        unsigned r=0,g=0,bvalue=0,count=0;
        for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++) {
            uint32_t c=samples[(y*2+yy)*SIDE+x*2+xx];
            if(c){r+=(c>>16)&255;g+=(c>>8)&255;bvalue+=c&255;count++;}
        }
        resolved[y*(SIDE/2)+x]=count>=2?0xff000000u|(r/count)<<16|(g/count)<<8|bvalue/count:0;
    }
    for(int y=0;y<SIDE/2;y++)for(int x=0;x<SIDE/2;) {
        uint32_t c=resolved[y*(SIDE/2)+x];int width=1;
        while(x+width<SIDE/2 && resolved[y*(SIDE/2)+x+width]==c)width++;
        if(c)api->fill_rect(api->context,cx-16+x,floor-2+y,width,1,(c>>16)&255,(c>>8)&255,c&255);
        x+=width;
    }
    return true;
}
