#include "rect_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GLES2 runtime ABI, matching the host's offline/no-development-headers build. */
extern unsigned int glCreateShader(unsigned int type);
extern void glShaderSource(unsigned int shader,int count,const char *const *strings,const int *lengths);
extern void glCompileShader(unsigned int shader);
extern void glGetShaderiv(unsigned int shader,unsigned int pname,int *value);
extern void glGetShaderInfoLog(unsigned int shader,int capacity,int *length,char *log);
extern void glDeleteShader(unsigned int shader);
extern unsigned int glCreateProgram(void);
extern void glAttachShader(unsigned int program,unsigned int shader);
extern void glBindAttribLocation(unsigned int program,unsigned int index,const char *name);
extern void glLinkProgram(unsigned int program);
extern void glGetProgramiv(unsigned int program,unsigned int pname,int *value);
extern void glGetProgramInfoLog(unsigned int program,int capacity,int *length,char *log);
extern void glDeleteProgram(unsigned int program);
extern void glUseProgram(unsigned int program);
extern void glGenBuffers(int count,unsigned int *buffers);
extern void glDeleteBuffers(int count,const unsigned int *buffers);
extern void glBindBuffer(unsigned int target,unsigned int buffer);
extern void glBufferData(unsigned int target,ptrdiff_t size,const void *data,unsigned int usage);
extern void glEnableVertexAttribArray(unsigned int index);
extern void glVertexAttribPointer(unsigned int index,int size,unsigned int type,unsigned char normalized,int stride,const void *pointer);
extern void glDrawArrays(unsigned int mode,int first,int count);
extern void glDisable(unsigned int cap);
extern void glEnable(unsigned int cap);
extern void glDisableVertexAttribArray(unsigned int index);
extern void glBlendEquation(unsigned int mode);
extern void glBlendFuncSeparate(unsigned int src_rgb,unsigned int dst_rgb,unsigned int src_alpha,unsigned int dst_alpha);
extern void glGenTextures(int count,unsigned int *textures);
extern void glDeleteTextures(int count,const unsigned int *textures);
extern void glActiveTexture(unsigned int texture);
extern void glBindTexture(unsigned int target,unsigned int texture);
extern void glTexParameteri(unsigned int target,unsigned int pname,int value);
extern void glTexImage2D(unsigned int target,int level,int internal_format,int width,int height,int border,unsigned int format,unsigned int type,const void *pixels);
extern void glGetIntegerv(unsigned int pname,int *value);
extern void glPixelStorei(unsigned int pname,int value);
extern unsigned int glGetError(void);
extern int glGetUniformLocation(unsigned int program,const char *name);
extern void glUniform1i(int location,int value);
extern void glUniform2f(int location,float x,float y);
extern void glGetShaderPrecisionFormat(unsigned int type,unsigned int precision_type,int *range,int *precision);

static unsigned int shader(unsigned int type,const char *source)
{
    unsigned int id=glCreateShader(type); int ok=0;
    glShaderSource(id,1,&source,NULL);glCompileShader(id);glGetShaderiv(id,0x8b81,&ok);
    if (!ok) {
        char log[512]={0};glGetShaderInfoLog(id,sizeof(log),NULL,log);
        fprintf(stderr,"Rectangle shader: %s\n",log);glDeleteShader(id);return 0;
    }
    return id;
}

bool rect_renderer_init(struct rect_renderer *r,int width,int height)
{
    memset(r,0,sizeof(*r));
    if (width<=0 || height<=0) return false;
    r->width=width;r->height=height;
    unsigned int vertex=shader(0x8b31,"attribute vec2 position; attribute vec4 colour; varying lowp vec4 tint; void main(){gl_Position=vec4(position,0.0,1.0);tint=colour;}");
    unsigned int fragment=shader(0x8b30,"precision mediump float; varying lowp vec4 tint; void main(){gl_FragColor=tint;}");
    if (!vertex || !fragment) { if(vertex)glDeleteShader(vertex);if(fragment)glDeleteShader(fragment);return false; }
    r->program=glCreateProgram();
    glAttachShader(r->program,vertex);glAttachShader(r->program,fragment);
    glBindAttribLocation(r->program,0,"position");glBindAttribLocation(r->program,1,"colour");
    glLinkProgram(r->program);glDeleteShader(vertex);glDeleteShader(fragment);
    int ok=0;glGetProgramiv(r->program,0x8b82,&ok);
    if (!ok) {
        char log[512]={0};glGetProgramInfoLog(r->program,sizeof(log),NULL,log);
        fprintf(stderr,"Rectangle program: %s\n",log);rect_renderer_destroy(r);return false;
    }
    r->vertices=malloc(RECT_BATCH_CAPACITY*6*sizeof(*r->vertices));
    glGenBuffers(1,&r->buffer);
    if (!r->vertices || !r->buffer) { rect_renderer_destroy(r);return false; }
    return true;
}

void rect_renderer_destroy(struct rect_renderer *r)
{
    if (r->texture_buffer) glDeleteBuffers(1,&r->texture_buffer);
    if (r->texture_program) glDeleteProgram(r->texture_program);
    free(r->texture_vertices);
    if (r->buffer) glDeleteBuffers(1,&r->buffer);
    if (r->program) glDeleteProgram(r->program);
    free(r->vertices);memset(r,0,sizeof(*r));
}

void rect_renderer_begin(struct rect_renderer *r)
{ r->count=0;r->texture_count=0;r->batch_texture=0;r->rectangles=0;r->sprites=0;r->batches=0; }

void rect_renderer_flush(struct rect_renderer *r)
{
    if (r->texture_count) {
        glDisable(0x0c11);
        glUseProgram(r->texture_program);glBindBuffer(0x8892,r->texture_buffer);
        glActiveTexture(0x84c0);glBindTexture(0x0de1,r->batch_texture);
        glUniform2f(r->texture_size_location,(float)r->batch_texture_width,(float)r->batch_texture_height);
        glBufferData(0x8892,(ptrdiff_t)(r->texture_count*sizeof(*r->texture_vertices)),r->texture_vertices,0x88e0);
        glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
        glVertexAttribPointer(0,2,0x1406,0,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,x));
        glVertexAttribPointer(1,4,0x1401,1,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,r));
        glVertexAttribPointer(2,2,0x1406,0,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,u));
        glEnable(0x0be2);glBlendEquation(0x8006);
        glBlendFuncSeparate(0x0302,0x0303,1,0x0303);
        glDrawArrays(0x0004,0,(int)r->texture_count);
        glDisable(0x0be2);glDisableVertexAttribArray(2);
        r->texture_count=0;r->batch_texture=0;r->batches++;
    }
    if (!r->count) return;
    glDisable(0x0be2);
    glDisable(0x0c11); /* scissor: host has already clipped every rectangle */
    glUseProgram(r->program);glBindBuffer(0x8892,r->buffer);
    glBufferData(0x8892,(ptrdiff_t)(r->count*sizeof(*r->vertices)),r->vertices,0x88e0);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,0x1406,0,sizeof(struct rect_vertex),(void *)offsetof(struct rect_vertex,x));
    glVertexAttribPointer(1,4,0x1401,1,sizeof(struct rect_vertex),(void *)offsetof(struct rect_vertex,r));
    glDrawArrays(0x0004,0,(int)r->count);
    r->count=0;r->batches++;
}

void rect_renderer_rect(struct rect_renderer *r,int x,int y,int w,int h,uint8_t red,uint8_t green,uint8_t blue)
{
    if (w<=0 || h<=0) return;
    if (r->texture_count) rect_renderer_flush(r);
    if (r->count+6>RECT_BATCH_CAPACITY*6) rect_renderer_flush(r);
    float left=2.f*x/r->width-1.f,right=2.f*(x+w)/r->width-1.f;
    float bottom=2.f*y/r->height-1.f,top=2.f*(y+h)/r->height-1.f;
    struct rect_vertex a={left,bottom,red,green,blue,255},b={right,bottom,red,green,blue,255};
    struct rect_vertex c={right,top,red,green,blue,255},d={left,top,red,green,blue,255};
    struct rect_vertex *v=r->vertices+r->count;
    v[0]=a;v[1]=b;v[2]=c;v[3]=a;v[4]=c;v[5]=d;
    r->count+=6;r->rectangles++;
}

static bool texture_renderer_init(struct rect_renderer *r)
{
    if (r->texture_program) return true;
    int range[2],precision=0;
    glGetShaderPrecisionFormat(0x8b30,0x8df2,range,&precision);
    const char *qualifier=precision?"highp":"mediump";
    char source[512];
    snprintf(source,sizeof(source),
        "attribute vec2 position; attribute vec4 colour; attribute vec2 texcoord;"
        "varying %s vec2 uv; varying lowp vec4 tint;"
        "void main(){gl_Position=vec4(position,0.0,1.0);uv=texcoord;tint=colour;}",qualifier);
    unsigned int vertex=shader(0x8b31,source);
    /* Sample texel centers explicitly: some WebGL samplers round a coordinate
       near an edge into its neighbor even with highp interpolated coordinates. */
    snprintf(source,sizeof(source),
        "precision mediump float; uniform sampler2D image;"
        "uniform %s vec2 image_size; varying %s vec2 uv; varying lowp vec4 tint;"
        "void main(){%s vec2 cell=floor(uv);"
        "gl_FragColor=texture2D(image,(cell+vec2(0.5))/image_size)*tint;}",qualifier,qualifier,qualifier);
    unsigned int fragment=shader(0x8b30,source),program=0,buffer=0;
    struct texture_vertex *vertices=NULL;
    if (!vertex || !fragment) goto failed;
    program=glCreateProgram();
    glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"colour");
    glBindAttribLocation(program,2,"texcoord");glLinkProgram(program);
    int ok=0;glGetProgramiv(program,0x8b82,&ok);
    if (!ok) {
        char log[512]={0};glGetProgramInfoLog(program,sizeof(log),NULL,log);
        fprintf(stderr,"Texture program: %s\n",log);goto failed;
    }
    vertices=malloc(TEXTURE_BATCH_CAPACITY*6*sizeof(*vertices));
    glGenBuffers(1,&buffer);
    if (!vertices || !buffer) goto failed;
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    r->texture_size_location=glGetUniformLocation(program,"image_size");
    r->texture_program=program;r->texture_buffer=buffer;r->texture_vertices=vertices;
    glDeleteShader(vertex);glDeleteShader(fragment);return true;
failed:
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (program) glDeleteProgram(program);
    if (buffer) glDeleteBuffers(1,&buffer);
    free(vertices);return false;
}

bool rect_renderer_texture_create(struct rect_renderer *r,struct rect_renderer_texture *t,
                                 int width,int height,const void *rgba,size_t size)
{
    if (!r || !r->program || !t || t->id) return false;
    memset(t,0,sizeof(*t));
    if (!rgba || width<=0 || height<=0 || (size_t)width>SIZE_MAX/4/(size_t)height ||
        size<(size_t)width*(size_t)height*4) return false;
    int limit=0;glGetIntegerv(0x0d33,&limit);
    if (width>limit || height>limit || !texture_renderer_init(r)) return false;
    unsigned int id=0;glGenTextures(1,&id);
    if (!id) return false;
    glActiveTexture(0x84c0);glBindTexture(0x0de1,id);
    glTexParameteri(0x0de1,0x2801,0x2600); /* min/mag: NEAREST */
    glTexParameteri(0x0de1,0x2800,0x2600);
    glTexParameteri(0x0de1,0x2802,0x812f); /* NPOT-safe CLAMP_TO_EDGE */
    glTexParameteri(0x0de1,0x2803,0x812f);
    int alignment=4;glGetIntegerv(0x0cf5,&alignment);glPixelStorei(0x0cf5,1);
    glTexImage2D(0x0de1,0,0x1908,width,height,0,0x1908,0x1401,rgba);
    glPixelStorei(0x0cf5,alignment);
    if (glGetError()) { glDeleteTextures(1,&id);return false; }
    t->id=id;t->width=width;t->height=height;return true;
}

void rect_renderer_texture_delete(struct rect_renderer *r,struct rect_renderer_texture *t)
{
    if (!t) return;
    if (t->id) {
        if (r->texture_count && r->batch_texture==t->id) rect_renderer_flush(r);
        glDeleteTextures(1,&t->id);
    }
    memset(t,0,sizeof(*t));
}

void rect_renderer_sprite(struct rect_renderer *r,const struct rect_renderer_texture *t,
                          int x,int y,int width,int height,int sx,int sy,int sw,int sh,
                          uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,bool flip_x)
{
    rect_renderer_sprite_clipped(r,t,x,y,width,height,sx,sy,sw,sh,red,green,blue,alpha,flip_x,
                                 0,0,r->width,r->height);
}

void rect_renderer_sprite_clipped(struct rect_renderer *r,const struct rect_renderer_texture *t,
                                  int x,int y,int width,int height,int sx,int sy,int sw,int sh,
                                  uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,bool flip_x,
                                  int clip_x,int clip_y,int clip_w,int clip_h)
{
    if (!t || !t->id || !r->texture_program || width<=0 || height<=0 || !alpha ||
        clip_w<=0 || clip_h<=0 ||
        sx<0 || sy<0 || sw<=0 || sh<=0 || sw>t->width || sh>t->height ||
        sx>t->width-sw || sy>t->height-sh) return;
    /* Widen additions before intersecting; offscreen INT_MIN/MAX inputs must
       not overflow or stretch the original source mapping. */
    int64_t x0=x,y0=y,x1=(int64_t)x+width,y1=(int64_t)y+height;
    int64_t cx1=(int64_t)clip_x+clip_w,cy1=(int64_t)clip_y+clip_h;
    if (x0<clip_x) x0=clip_x;
    if (y0<clip_y) y0=clip_y;
    if (x1>cx1) x1=cx1;
    if (y1>cy1) y1=cy1;
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>r->width) x1=r->width;
    if (y1>r->height) y1=r->height;
    if (x0>=x1 || y0>=y1) return;
    if (r->count || (r->texture_count && r->batch_texture!=t->id) ||
        r->texture_count+6>TEXTURE_BATCH_CAPACITY*6) rect_renderer_flush(r);
    float left=2.f*x0/r->width-1.f,right=2.f*x1/r->width-1.f;
    float bottom=2.f*y0/r->height-1.f,top=2.f*y1/r->height-1.f;
    /* Bias center sampling to the CPU splash's integer cell edges. The half
       numerator keeps samples off texel boundaries, including exact ratios. */
    double bx=(sw-1)*0.5/width,by=(sh-1)*0.5/height;
    double start=((double)x0-x)*sw/width+bx,end=((double)x1-x)*sw/width+bx;
    float u0=sx+start,u1=sx+end;
    if (flip_x) { u0=sx+sw-start;u1=sx+sw-end; }
    float v0=sy+((double)y+height-y1)*sh/height+by;
    float v1=sy+((double)y+height-y0)*sh/height+by;
    struct texture_vertex a={left,bottom,u0,v1,red,green,blue,alpha};
    struct texture_vertex b={right,bottom,u1,v1,red,green,blue,alpha};
    struct texture_vertex c={right,top,u1,v0,red,green,blue,alpha};
    struct texture_vertex d={left,top,u0,v0,red,green,blue,alpha};
    struct texture_vertex *v=r->texture_vertices+r->texture_count;
    v[0]=a;v[1]=b;v[2]=c;v[3]=a;v[4]=c;v[5]=d;
    r->batch_texture=t->id;r->batch_texture_width=t->width;r->batch_texture_height=t->height;
    r->texture_count+=6;r->sprites++;
}
