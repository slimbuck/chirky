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
    if (r->buffer) glDeleteBuffers(1,&r->buffer);
    if (r->program) glDeleteProgram(r->program);
    free(r->vertices);memset(r,0,sizeof(*r));
}

void rect_renderer_begin(struct rect_renderer *r) { r->count=0;r->rectangles=0;r->batches=0; }

void rect_renderer_flush(struct rect_renderer *r)
{
    if (!r->count) return;
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
    if (r->count+6>RECT_BATCH_CAPACITY*6) rect_renderer_flush(r);
    float left=2.f*x/r->width-1.f,right=2.f*(x+w)/r->width-1.f;
    float bottom=2.f*y/r->height-1.f,top=2.f*(y+h)/r->height-1.f;
    struct rect_vertex a={left,bottom,red,green,blue,255},b={right,bottom,red,green,blue,255};
    struct rect_vertex c={right,top,red,green,blue,255},d={left,top,red,green,blue,255};
    struct rect_vertex *v=r->vertices+r->count;
    v[0]=a;v[1]=b;v[2]=c;v[3]=a;v[4]=c;v[5]=d;
    r->count+=6;r->rectangles++;
}
