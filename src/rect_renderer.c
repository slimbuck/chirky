#include "rect_renderer.h"
#include "pixel_font.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
extern void glUniform4f(int location,float x,float y,float z,float w);
extern void glUniform1f(int location,float value);
extern void glScissor(int x,int y,int width,int height);
extern void glClear(unsigned int mask);
extern void glClearDepthf(float depth);
extern void glDepthMask(unsigned char flag);
extern void glDepthFunc(unsigned int func);
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
    for(unsigned i=0;i<8;i++)rect_renderer_texture_delete(r,&r->fonts[i]);
    if (r->mesh_buffer) glDeleteBuffers(1,&r->mesh_buffer);
    if (r->mesh_program) glDeleteProgram(r->mesh_program);
    if (r->texture_buffer) glDeleteBuffers(1,&r->texture_buffer);
    if (r->texture_program) glDeleteProgram(r->texture_program);
    free(r->texture_vertices);
    if (r->buffer) glDeleteBuffers(1,&r->buffer);
    if (r->program) glDeleteProgram(r->program);
    free(r->vertices);memset(r,0,sizeof(*r));
}

static bool mesh_init(struct rect_renderer *r)
{
    if(r->mesh_program)return true;
    unsigned int v=shader(0x8b31,
        "attribute vec3 position; attribute vec3 normal; attribute vec4 colour;"
        "uniform vec4 view; uniform float ambient; varying lowp vec3 tint;"
        "void main(){gl_Position=vec4((position.xy+view.xy)*view.zw-1.0,-position.z,1.0);"
        "vec3 n=normalize(normal); if(n.z<0.0)n=-n;"
        "float light=ambient+(1.0-ambient)*max(0.0,dot(n,normalize(vec3(-0.35,0.60,0.72))));"
        "tint=colour.rgb*mix(light,1.0,colour.a);}");
    unsigned int f=shader(0x8b30,"precision mediump float; varying lowp vec3 tint; void main(){gl_FragColor=vec4(tint,1.0);}");
    if(!v || !f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
    unsigned int p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    glBindAttribLocation(p,0,"position");glBindAttribLocation(p,1,"colour");glBindAttribLocation(p,2,"normal");
    glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    int ok=0;glGetProgramiv(p,0x8b82,&ok);
    if(!ok){glDeleteProgram(p);return false;}
    glGenBuffers(1,&r->mesh_buffer);
    if(!r->mesh_buffer){glDeleteProgram(p);return false;}
    r->mesh_program=p;r->mesh_view=glGetUniformLocation(p,"view");r->mesh_ambient=glGetUniformLocation(p,"ambient");
    return true;
}

bool rect_renderer_mesh(struct rect_renderer *r,const struct chirky_mesh_vertex *v,
                        size_t count,float ambient,int ox,int oy,int width,int height)
{
    if(!r || !r->program || !v || !count || count>12288 || count%3 ||
       !isfinite(ambient) || ambient<0 || ambient>1 || width<=0 || height<=0)return false;
    for(size_t i=0;i<count;i++)if(!isfinite(v[i].x) || !isfinite(v[i].y) ||
        !isfinite(v[i].z) || fabsf(v[i].z)>1 || !isfinite(v[i].nx) ||
        !isfinite(v[i].ny) || !isfinite(v[i].nz))return false;
    if(!mesh_init(r))return false;
    rect_renderer_flush(r);
    glEnable(0x0c11);glScissor(ox,oy,width,height);
    glEnable(0x0b71);glDepthMask(1);glDepthFunc(0x0203);glClearDepthf(1);glClear(0x00000100);
    glDisable(0x0be2);glDisable(0x0b44);
    glUseProgram(r->mesh_program);glBindBuffer(0x8892,r->mesh_buffer);
    glUniform4f(r->mesh_view,(float)ox,(float)oy,2.f/r->width,2.f/r->height);
    glUniform1f(r->mesh_ambient,ambient);
    glBufferData(0x8892,(ptrdiff_t)(count*sizeof(*v)),v,0x88e0);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    glVertexAttribPointer(0,3,0x1406,0,sizeof(*v),(void *)offsetof(struct chirky_mesh_vertex,x));
    glVertexAttribPointer(1,4,0x1401,1,sizeof(*v),(void *)offsetof(struct chirky_mesh_vertex,r));
    glVertexAttribPointer(2,3,0x1406,0,sizeof(*v),(void *)offsetof(struct chirky_mesh_vertex,nx));
    glDrawArrays(0x0004,0,(int)count);
    glDisableVertexAttribArray(2);glDisable(0x0b71);glDisable(0x0c11);r->batches++;
    return true;
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
        glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);glEnableVertexAttribArray(3);
        glVertexAttribPointer(0,2,0x1406,0,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,x));
        glVertexAttribPointer(1,4,0x1401,1,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,r));
        glVertexAttribPointer(2,2,0x1406,0,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,u));
        glVertexAttribPointer(3,2,0x1406,0,sizeof(struct texture_vertex),(void *)offsetof(struct texture_vertex,source_x));
        glEnable(0x0be2);glBlendEquation(0x8006);
        glBlendFuncSeparate(0x0302,0x0303,1,0x0303);
        glDrawArrays(0x0004,0,(int)r->texture_count);
        glDisable(0x0be2);glDisableVertexAttribArray(2);glDisableVertexAttribArray(3);
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
        "attribute vec2 position; attribute vec4 colour; attribute vec2 texcoord;attribute vec2 source;"
        "varying %s vec2 uv;varying %s vec2 origin; varying lowp vec4 tint;"
        "void main(){gl_Position=vec4(position,0.0,1.0);uv=texcoord;origin=source;tint=colour;}",qualifier,qualifier);
    unsigned int vertex=shader(0x8b31,source);
    /* Sample texel centers explicitly: some WebGL samplers round a coordinate
       near an edge into its neighbor even with highp interpolated coordinates. */
    snprintf(source,sizeof(source),
        "precision mediump float; uniform sampler2D image;"
        "uniform %s vec2 image_size; varying %s vec2 uv;varying %s vec2 origin; varying lowp vec4 tint;"
        "void main(){%s vec2 cell=floor(uv)+origin;"
        "gl_FragColor=texture2D(image,(cell+vec2(0.5))/image_size)*tint;}",qualifier,qualifier,qualifier,qualifier);
    unsigned int fragment=shader(0x8b30,source),program=0,buffer=0;
    struct texture_vertex *vertices=NULL;
    if (!vertex || !fragment) goto failed;
    program=glCreateProgram();
    glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"colour");
    glBindAttribLocation(program,2,"texcoord");glBindAttribLocation(program,3,"source");glLinkProgram(program);
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

static bool font_pixel(const uint8_t *rows,int x,int y,int scale)
{ return x>=0 && y>=0 && x<5*scale && y<7*scale && (rows[y/scale]&(1<<(4-x/scale))); }

bool rect_renderer_text(struct rect_renderer *r,int x,int y,const char *text,int scale,
                        uint8_t red,uint8_t green,uint8_t blue,bool outline,
                        int clip_x,int clip_y,int clip_w,int clip_h)
{
    if(!r || !r->program || !text || scale<1 || scale>8)return false;
    struct rect_renderer_texture *atlas=&r->fonts[scale-1];
    const int cw=5*scale+4,ch=7*scale+4,width=16*cw,height=14*ch;
    if(!atlas->id) {
        unsigned char *pixels=calloc((size_t)width*height,4);
        if(!pixels)return false;
        /* Two rows of glyph sets: plain and a one-pixel black dilation.
           Each cell has a transparent gutter outside the outline. No resampling. */
        for(int style=0;style<2;style++)for(int c=0;c<100;c++) {
            const uint8_t *rows=glyph(c<96?(unsigned)c+32:0x2190u+c-96);
            for(int yy=-1;yy<=7*scale;yy++)for(int xx=-1;xx<=5*scale;xx++) {
                bool ink=font_pixel(rows,xx,yy,scale),border=false;
                if(style && !ink)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)
                    border|=font_pixel(rows,xx+dx,yy+dy,scale);
                if(!ink && !border)continue;
                size_t i=((size_t)((c/16+style*7)*ch+yy+2)*width+c%16*cw+xx+2)*4;
                pixels[i]=pixels[i+1]=pixels[i+2]=ink?255:0;pixels[i+3]=255;
            }
        }
        bool ok=rect_renderer_texture_create(r,atlas,width,height,pixels,(size_t)width*height*4);
        free(pixels);if(!ok)return false;
    }
    int edge=outline?1:0;
    int64_t px=x,bottom=(int64_t)y-6*scale-edge;
    if(bottom<INT_MIN || bottom>INT_MAX)return true;
    for(const char *p=text;*p;px+=6*scale) {
        unsigned code=chirky_text_next(&p);
        if(code==' ' || px-edge<INT_MIN || px-edge>INT_MAX)continue;
        unsigned c=code>=0x2190 && code<=0x2193?96+code-0x2190:(code>=32 && code<128?code:'?')-32;
        rect_renderer_sprite_clipped(r,atlas,(int)px-edge,(int)bottom,5*scale+2*edge,7*scale+2*edge,
            c%16*cw+2-edge,(c/16+(outline?7:0))*ch+2-edge,5*scale+2*edge,7*scale+2*edge,
            red,green,blue,255,false,clip_x,clip_y,clip_w,clip_h);
    }
    return true;
}

static void texture_quad(struct rect_renderer *r,const struct rect_renderer_texture *t,
    double x0,double y0,double x1,double y1,float u0,float v0,float u1,float v1,float source_x,float source_y,
    uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha)
{
    if (r->count || (r->texture_count && r->batch_texture!=t->id) ||
        r->texture_count+6>TEXTURE_BATCH_CAPACITY*6) rect_renderer_flush(r);
    float left=2.f*x0/r->width-1.f,right=2.f*x1/r->width-1.f;
    float bottom=2.f*y0/r->height-1.f,top=2.f*y1/r->height-1.f;
    struct texture_vertex a={left,bottom,u0,v1,red,green,blue,alpha,source_x,source_y};
    struct texture_vertex b={right,bottom,u1,v1,red,green,blue,alpha,source_x,source_y};
    struct texture_vertex c={right,top,u1,v0,red,green,blue,alpha,source_x,source_y};
    struct texture_vertex d={left,top,u0,v0,red,green,blue,alpha,source_x,source_y};
    struct texture_vertex *v=r->texture_vertices+r->texture_count;
    v[0]=a;v[1]=b;v[2]=c;v[3]=a;v[4]=c;v[5]=d;
    r->batch_texture=t->id;r->batch_texture_width=t->width;r->batch_texture_height=t->height;
    r->texture_count+=6;r->sprites++;
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
    /* Bias center sampling to the CPU splash's integer cell edges. The half
       numerator keeps samples off texel boundaries, including exact ratios. */
    double bx=(sw-1)*0.5/width,by=(sh-1)*0.5/height;
    double start=((double)x0-x)*sw/width+bx,end=((double)x1-x)*sw/width+bx;
    float u0=sx+start,u1=sx+end;
    if (flip_x) { u0=sx+sw-start;u1=sx+sw-end; }
    float v0=sy+((double)y+height-y1)*sh/height+by;
    float v1=sy+((double)y+height-y0)*sh/height+by;
    texture_quad(r,t,x0,y0,x1,y1,u0,v0,u1,v1,0,0,red,green,blue,alpha);
}

void rect_renderer_sprite_projected(struct rect_renderer *r,const struct rect_renderer_texture *t,
    float x,float y,float scale,int sx,int sy,int sw,int sh,uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,
    bool flip,int offset_x,int offset_y,int clip_w,int clip_h)
{
    if(!t || !t->id || !r->texture_program || !alpha || !isfinite(x) || !isfinite(y) ||
       !isfinite(scale) || scale<.25f || scale>16 || sx<0 || sy<0 || sw<=0 || sh<=0 ||
       sw>t->width || sh>t->height || sx>t->width-sw || sy>t->height-sh || clip_w<=0 || clip_h<=0)return;
    /* Round in logical coordinates before adding the host's integer offset.
       Interpolate source coordinates from the unrounded camera transform. */
    double x0=fmax(0,fmax(offset_x,round((double)x)+offset_x));
    double y0=fmax(0,fmax(offset_y,round((double)y)+offset_y));
    double x1=fmin(r->width,fmin((double)offset_x+clip_w,round((double)x+sw*scale)+offset_x));
    double y1=fmin(r->height,fmin((double)offset_y+clip_h,round((double)y+sh*scale)+offset_y));
    if(x0>=x1 || y0>=y1)return;
    /* Resolve half-pixel ties consistently with rounded positive screen edges.
       The tiny tie bias must not move genuinely fractional boundaries. */
    double a=(x0-offset_x-x)/scale-.00001,b=(x1-offset_x-x)/scale-.00001;
    float u0=a,u1=b;
    if(flip){u0=sw-a;u1=sw-b;}
    float v0=sh-(y1-offset_y-y)/scale+.00001;
    float v1=sh-(y0-offset_y-y)/scale+.00001;
    texture_quad(r,t,x0,y0,x1,y1,u0,v0,u1,v1,sx,sy,red,green,blue,alpha);
}
