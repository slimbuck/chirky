#include "robot.h"
#include "asset_file.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


/* Rigid poses are evaluated once per vertex. GPU handles lighting, depth and rasterization. */
#define LIMIT_VERTICES 2048
#define LIMIT_TRIANGLES 4096
#define LIMIT_BONES 32
#define LIMIT_FRAMES 256
struct vertex { float p[3]; uint32_t bone,part; };
struct face_rig {
    float radius,head_pivot,neck_min,neck_max;
    float origin[3],u[3],v[3],normal[3];
    float half_width,half_height,bulge,eye_spacing,eye_v;
};
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
    struct face_rig rig;
    bool curved_face;
} model;
static struct projected transformed[LIMIT_VERTICES];
static struct chirky_mesh_vertex submitted[LIMIT_TRIANGLES*3];
struct robot_tuning robot_style={.ambient=.78f,.brightness=1.35f,.head_lead=.32f,.body_lag=.22f,.idle_look=.20f,.blink=1};

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
{ return robot_load_api(NULL,config); }
bool robot_load_api(const struct chirky_host_api *api,const char *config)
{
    robot_free();if(!config)return false;
    const char *slash=strrchr(config,'/');
    const char *backslash=strrchr(config,'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    char path[1024];int prefix=slash?(int)(slash-config+1):0;
    if(snprintf(path,sizeof(path),"%.*sassets/models/player.robot",prefix,config)>=(int)sizeof(path))return false;
    struct chirky_file source=chirky_file_open(api,path);
    FILE *file=source.stream;if(!file)return false;
    char magic[4];uint32_t counts[6];bool ok=fread(magic,1,4,file)==4;
    model.curved_face=ok && !memcmp(magic,"PRB2",4);
    ok=ok && (model.curved_face || !memcmp(magic,"PRB1",4));
    for(int i=0;i<6 && ok;i++)ok=word(file,&counts[i]);
    if(!ok || !counts[0] || counts[0]>LIMIT_VERTICES || !counts[1] || counts[1]>LIMIT_TRIANGLES ||
       !counts[2] || counts[2]>LIMIT_BONES || !counts[3] || counts[3]>32 || counts[4]!=6 ||
       !counts[5] || counts[5]>LIMIT_FRAMES)goto invalid;
    model.rig=(struct face_rig){.radius=.55f,.head_pivot=1.16f,.neck_min=1.045f,.neck_max=1.195f,
        .origin={0,-.425f,1.55f},.u={1,0,0},.v={0,0,1},.normal={0,-1,0},
        .half_width=.37f,.half_height=.18f,.eye_spacing=.205f};
    if(model.curved_face) {
        float values[21];
        for(unsigned i=0;i<21;i++)if(!real(file,&values[i]))goto invalid;
        _Static_assert(sizeof(struct face_rig)==sizeof(values),"PRB2 rig layout");
        memcpy(&model.rig,values,sizeof(values));
        const struct face_rig *r=&model.rig;
        if(r->radius<.1f || r->radius>2 || r->neck_max-r->neck_min<.01f ||
           r->half_width<.1f || r->half_height<.1f || r->bulge<0 || r->bulge>1 ||
           r->eye_spacing<=0 || r->eye_spacing>=r->half_width)goto invalid;
        const float *axes[]={r->u,r->v,r->normal};
        for(int a=0;a<3;a++)for(int b=a;b<3;b++) {
            float dot=0;for(int j=0;j<3;j++)dot+=axes[a][j]*axes[b][j];
            if(fabsf(dot-(a==b?1.f:0.f))>.001f)goto invalid;
        }
    }
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
        if(model.curved_face && (!word(file,&model.vertex[i].part) || model.vertex[i].part>4 ||
            (model.vertex[i].part && model.vertex[i].bone!=2)))goto invalid;
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
    if(!model.curved_face) {
        for(unsigned i=0;i<model.triangles;i++)if(model.triangle[i].material==6)
            for(int j=0;j<3;j++) {
                struct vertex *v=&model.vertex[model.triangle[i].v[j]];
                v->part=v->p[0]<0?1:2;
            }
        for(unsigned i=0;i<model.vertices;i++) {
            struct vertex *v=&model.vertex[i];
            if(v->bone==2 && fabsf(v->p[0])<.2f && fabsf(v->p[1])<.2f && v->p[2]<1.2f)v->part=4;
        }
    }
    chirky_file_close(&source);
    snprintf(path,sizeof(path),"%.*sassets/models/robot.conf",prefix,config);
    source=chirky_file_open(api,path);
    robot_style=(struct robot_tuning){.ambient=.78f,.brightness=1.35f,.head_lead=.32f,.body_lag=.22f,.idle_look=.20f,.blink=1};
    if(source.stream) {
        char line[160],key[64];float value;
        while(fgets(line,sizeof(line),source.stream))if(sscanf(line,"%63[^=]=%f",key,&value)==2 && isfinite(value)) {
            if(!strcmp(key,"ambient"))robot_style.ambient=fmaxf(0,fminf(1,value));
            else if(!strcmp(key,"brightness"))robot_style.brightness=fmaxf(.5f,fminf(1.8f,value));
            else if(!strcmp(key,"head_lead"))robot_style.head_lead=fmaxf(0,fminf(.6f,value));
            else if(!strcmp(key,"body_lag"))robot_style.body_lag=fmaxf(0,fminf(.4f,value));
            else if(!strcmp(key,"idle_look"))robot_style.idle_look=fmaxf(0,fminf(.5f,value));
            else if(!strcmp(key,"blink"))robot_style.blink=value>=.5f;
        }
        chirky_file_close(&source);
    }
    return true;
invalid:
    chirky_file_close(&source);robot_free();return false;
}

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
    float desired=distance/(8*(model.rig.radius>0?model.rig.radius:.396f));
    if(m->delay>0 && grounded){m->delay--;desired=0;}
    if(!intent && grounded)desired=0; /* Brakes lead the head's follow-through. */
    m->roll_speed+=(desired-m->roll_speed)*(intent?.38f:.70f);
    if(fabsf(m->roll_speed)<.0001f)m->roll_speed=0;
    m->roll=fmodf(m->roll+m->roll_speed,6.283185307f);
    m->intent=intent;
}

void robot_face_init(struct robot_face *f)
{ if(f)*f=(struct robot_face){.left_open=1,.right_open=1,.smile=1}; }
void robot_face_blink(struct robot_face *f)
{ if(f)f->requested_blink=9; }
static float blink_envelope(unsigned age)
{
    if(age>8)return 0;
    return age<3?age/2.f:age<5?1.f:(9-age)/5.f;
}
void robot_face_update(struct robot_face *f)
{
    if(!f)return;
    f->tick=(f->tick+1)%960;
    /* Long, unequal holds with short eased eye movements. This clock never
       depends on a movement clip, so a jump cannot restart a glance or blink. */
    static const unsigned times[]={0,130,290,430,570,740,870};
    static const float gaze[][2]={{0,0},{.8f,.25f},{.1f,-.2f},{-.7f,.4f},{0,0},{.5f,-.25f},{0,0}};
    unsigned target=0;for(unsigned i=1;i<7;i++)if(f->tick>=times[i])target=i;
    f->gaze_x+=(gaze[target][0]-f->gaze_x)*.22f;
    f->gaze_y+=(gaze[target][1]-f->gaze_y)*.22f;
    f->blink=0;
    const unsigned blinks[]={108,337,527,541,813};
    for(unsigned i=0;i<5;i++)if(f->tick>=blinks[i])f->blink=fmaxf(f->blink,blink_envelope(f->tick-blinks[i]));
    if(f->requested_blink) {
        f->blink=fmaxf(f->blink,blink_envelope(9-f->requested_blink));
        f->requested_blink--;
    }
    float left=1,right=1,smile=1;
    if(f->expression==ROBOT_FACE_HAPPY){left=right=.72f;smile=1.7f;}
    else if(f->expression==ROBOT_FACE_CURIOUS){left=1.05f;right=.75f;smile=.6f;}
    else if(f->expression==ROBOT_FACE_SLEEPY){left=right=.4f;smile=.5f;}
    f->left_open+=(left-f->left_open)*.15f;
    f->right_open+=(right-f->right_open)*.15f;
    f->smile+=(smile-f->smile)*.15f;
}

static void face_position(const struct vertex *vertex,const struct robot_face *f,float out[3])
{
    memcpy(out,vertex->p,sizeof(vertex->p));
    if(!f || !vertex->part || vertex->part>3)return;
    const struct face_rig *r=&model.rig;
    float u=0,v=0;
    for(int j=0;j<3;j++){u+=(out[j]-r->origin[j])*r->u[j];v+=(out[j]-r->origin[j])*r->v[j];}
    if(vertex->part<=2) {
        float center=(vertex->part==1?-1:1)*r->eye_spacing;
        float blink=robot_style.blink?f->blink:0;
        u=center+(u-center)*(1+.08f*blink)+f->gaze_x*.07f;
        v=r->eye_v+(v-r->eye_v)*(vertex->part==1?f->left_open:f->right_open)*(1-.94f*blink)+f->gaze_y*.045f;
    } else v=-.205f+(v+.205f)*f->smile;
    float depth=r->bulge*fmaxf(0,1-u*u/(r->half_width*r->half_width))*fmaxf(0,1-v*v/(r->half_height*r->half_height))+.035f;
    if(!model.curved_face) { out[0]=u;out[2]=r->origin[2]+v;return; }
    for(int j=0;j<3;j++)out[j]=r->origin[j]+u*r->u[j]+v*r->v[j]+depth*r->normal[j];
}

bool robot_draw(const struct chirky_host_api *api,int cx,int floor,int facing,enum robot_clip animation,float tick)
{ return robot_draw_scaled(api,cx,floor,facing,animation,tick,1); }

static bool robot_draw_at_scale(const struct chirky_host_api *api,int cx,int floor,int facing,
                                enum robot_clip animation,float tick,
                                const struct robot_motion *motion,const struct robot_face *face,float scale);

bool robot_draw_scaled(const struct chirky_host_api *api,int cx,int floor,int facing,
                       enum robot_clip animation,float tick,int scale)
{ return robot_draw_at_scale(api,cx,floor,facing,animation,tick,NULL,NULL,scale); }

bool robot_draw_weighted(const struct chirky_host_api *api,int cx,int floor,int facing,enum robot_clip animation,float tick,const struct robot_motion *motion)
{ return robot_draw_at_scale(api,cx,floor,facing,animation,tick,motion,NULL,1); }

static bool robot_draw_at_scale(const struct chirky_host_api *api,int cx,int floor,int facing,
                                enum robot_clip animation,float tick,
                                const struct robot_motion *motion,const struct robot_face *face,float scale)
{
    if(!robot_ready() || !api || !api->draw_mesh || animation<0 || animation>ROBOT_DEATH ||
       !isfinite(scale) || scale<.25f || scale>16)return false;
    if(cx+16*scale<0 || cx-16*scale>=api->screen_width ||
       floor+30*scale<0 || floor-2*scale>=api->screen_height)return true;
    if(!isfinite(tick) || tick<0)tick=0;
    chirky_scope(api,"robot",true);
    static const char *clip_names[]={"robot.idle","robot.run","robot.jump","robot.fall","robot.dash","robot.death"};
    const char *clip_name=clip_names[animation];
    chirky_scope(api,clip_name,true);
    chirky_scope(api,"robot.pose",true);
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
    /* On descent the ball leads. Delay the authored head pitch by four ticks
       while its suspension briefly holds it above the falling body. */
    if(animation==ROBOT_FALL && model.bones==3) {
        float head_frame=fmaxf(0,tick-4)/clip->ticks;
        head_frame=fminf(head_frame,(float)(clip->count-1));
        unsigned ha=(unsigned)head_frame,hb=ha+1<clip->count?ha+1:ha;
        float mix=head_frame-ha;
        const float *head_a=model.poses+(clip->first+ha)*model.bones*12+24;
        const float *head_b=model.poses+(clip->first+hb)*model.bones*12+24;
        for(unsigned i=0;i<12;i++)pose[24+i]=head_a[i]+(head_b[i]-head_a[i])*mix;
    }
    /* Ball rig order is root/body/head. Replace cyclic ball rotation with a
       continuous, distance-driven angle; keep authored jump/fall head poses. */
    float lean=0;
    if(motion && model.bones==3 && animation!=ROBOT_DEATH) {
        float angle=motion->roll*(facing<0?-1:1),c=cosf(angle),s=sinf(angle);
        float *body=pose+12;
        float radius=model.rig.radius;
        const float rotation[12]={1,0,0,0, 0,c,-s,radius*s, 0,s,c,radius*(1-c)};
        memcpy(body,rotation,sizeof(rotation));
        lean=motion->lean*(facing<0?-1:1);
    }
    float lean_cos=cosf(lean),lean_sin=sinf(lean);
    chirky_scope(api,"robot.pose",false);
    chirky_scope(api,"robot.transform",true);
    float idle=fmodf(tick,360.f);
    float glance=animation==ROBOT_IDLE?robot_style.idle_look*sinf(idle*.017453293f)*sinf(idle*.008726646f):0;
    float head_offset=animation==ROBOT_JUMP?robot_style.head_lead*(tick/3.f)*expf(1-tick/3.f):0;
    float body_offset=animation==ROBOT_JUMP?-robot_style.body_lag*expf(-tick/9.f):0;
    if(animation==ROBOT_FALL)
        head_offset=.24f*(tick/5.f)*expf(1-tick/5.f);
    for(unsigned i=0;i<model.vertices;i++) {
        const struct vertex *v=&model.vertex[i];const float *m=pose+v->bone*12;
        float local[3];face_position(v,face,local);
        float p[3];for(int j=0;j<3;j++)p[j]=m[j*4]*local[0]+m[j*4+1]*local[1]+m[j*4+2]*local[2]+m[j*4+3];
        if(model.bones==3 && v->bone==2) {
            float xx=p[0],yy=p[1];p[0]=cosf(glance)*xx-sinf(glance)*yy;p[1]=sinf(glance)*xx+cosf(glance)*yy;
            /* Stretch the existing stabiliser between the ball and helmet
               during either jump lead or fall follow-through. */
            float neck=(v->p[2]-model.rig.neck_min)/(model.rig.neck_max-model.rig.neck_min);
            p[2]+=v->part==4?body_offset+(head_offset-body_offset)*fmaxf(0,fminf(1,neck)):head_offset;
        } else if(model.bones==3 && v->bone==1)p[2]+=body_offset;
        if(motion && model.bones==3 && v->bone==2 && animation!=ROBOT_DEATH) {
            float y=p[1],z=p[2]-model.rig.head_pivot;
            p[1]=lean_cos*y-lean_sin*z-lean*model.rig.radius;
            p[2]=model.rig.head_pivot+lean_sin*y+lean_cos*z;
        }
        float horizontal=.82f*p[0]-.57f*p[1],towards=-.57f*p[0]-.82f*p[1];
        transformed[i]=(struct projected){(facing<0?-1:1)*horizontal,
            p[2]*.984f-towards*.178f,towards*.984f+p[2]*.178f};
    }
    chirky_scope(api,"robot.transform",false);
    chirky_scope(api,"robot.submit",true);
    size_t count=0;
    uint8_t colours[32][3];
    for(unsigned i=0;i<model.materials;i++) {
        const struct material m=model.palette[i];
        const uint8_t rgb[]={m.r,m.g,m.b};
        for(int c=0;c<3;c++)colours[i][c]=(uint8_t)(255*powf(rgb[c]/255.f,1/robot_style.brightness));
    }
    for(unsigned i=0;i<model.triangles;i++) {
        const struct triangle *tri=&model.triangle[i];
        struct projected a=transformed[tri->v[0]],b=transformed[tri->v[1]],c=transformed[tri->v[2]];
        float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        if(nx*nx+ny*ny+nz*nz<1e-12f)continue;
        const struct material mat=model.palette[tri->material];
        unsigned char r=colours[tri->material][0];
        unsigned char g=colours[tri->material][1];
        unsigned char bl=colours[tri->material][2];
        for(int j=0;j<3;j++) {
            struct projected p=transformed[tri->v[j]];
            submitted[count++]=(struct chirky_mesh_vertex){cx+p.x*8*scale,floor+p.y*8*scale,p.z/128,
                nx,ny,nz,r,g,bl,mat.emissive?255:0};
        }
    }
    bool ok=api->draw_mesh(api->context,submitted,count,robot_style.ambient);
    chirky_scope(api,"robot.submit",false);
    chirky_scope(api,clip_name,false);
    chirky_scope(api,"robot",false);
    return ok;
}

bool robot_draw_view(const struct chirky_host_api *api,int cx,int floor,int facing,
                     enum robot_clip animation,float tick,const struct robot_motion *motion,float scale)
{ return robot_draw_at_scale(api,cx,floor,facing,animation,tick,motion,NULL,scale); }
bool robot_draw_character(const struct chirky_host_api *api,int cx,int floor,int facing,
                     enum robot_clip animation,float tick,const struct robot_motion *motion,
                     const struct robot_face *face,float scale)
{ return robot_draw_at_scale(api,cx,floor,facing,animation,tick,motion,face,scale); }
