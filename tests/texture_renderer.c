/* Shared pixel oracle for software EGL/GLES2 and browser WebGL1. */
#include "../src/rect_renderer.h"
#include <GLES2/gl2.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

enum { W=320,H=240 };
static unsigned char actual[H][W][4],expected[H][W][4];
static struct rect_renderer renderer;

static void begin(void)
{
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    rect_renderer_begin(&renderer);memset(expected,0,sizeof(expected));
}

static void compare(const char *name,int tolerance)
{
    rect_renderer_flush(&renderer);
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    assert(glGetError()==GL_NO_ERROR);
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) for (int c=0;c<4;c++) {
        int delta=(int)actual[y][x][c]-expected[y][x][c];
        if (abs(delta)>tolerance) {
            fprintf(stderr,"%s: (%d,%d) channel %d actual=%u expected=%u\n",
                    name,x,y,c,actual[y][x][c],expected[y][x][c]);
            assert(0);
        }
    }
    printf("PASS %s (%u batches)\n",name,renderer.batches);
}

static void rect(int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    rect_renderer_rect(&renderer,x,y,w,h,r,g,b);
    for (int yy=y;yy<y+h;yy++) for (int xx=x;xx<x+w;xx++) {
        if (xx<0 || yy<0 || xx>=W || yy>=H) continue;
        unsigned char *p=expected[yy][xx];p[0]=r;p[1]=g;p[2]=b;p[3]=255;
    }
}

static void sprite(const struct rect_renderer_texture *t,const unsigned char *pixels,
                   int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                   unsigned char red,unsigned char green,unsigned char blue,unsigned char alpha,
                   bool flip,int cx,int cy,int cw,int ch)
{
    rect_renderer_sprite_clipped(&renderer,t,x,y,w,h,sx,sy,sw,sh,red,green,blue,alpha,flip,cx,cy,cw,ch);
    unsigned char tint[]={red,green,blue};
    for (int yy=0;yy<H;yy++) for (int xx=0;xx<W;xx++) {
        if (xx<x || yy<y || xx>=x+w || yy>=y+h || xx<cx || yy<cy || xx>=cx+cw || yy>=cy+ch) continue;
        int source_x=((xx-x+1)*sw-1)/w;
        int source_y=((y+h-yy)*sh-1)/h;
        if (flip) source_x=sw-1-source_x;
        const unsigned char *p=pixels+((sy+source_y)*t->width+sx+source_x)*4;
        unsigned char *d=expected[yy][xx];
        double a=p[3]*alpha/(255.0*255.0);
        for (int c=0;c<3;c++) d[c]=(unsigned char)lround(p[c]*tint[c]/255.0*a+d[c]*(1-a));
        d[3]=(unsigned char)lround(255*a+d[3]*(1-a));
    }
}

static void pattern(unsigned char *pixels,int width,int height)
{
    for (int y=0;y<height;y++) for (int x=0;x<width;x++) {
        unsigned char *p=pixels+(y*width+x)*4;
        p[0]=(unsigned char)(x*37+y*13);p[1]=(unsigned char)(y*53+x*7);
        p[2]=(unsigned char)(x*11+y*19);p[3]=255;
    }
}

static void tests(void)
{
    assert(rect_renderer_init(&renderer,W,H));
    assert(!renderer.texture_program && !renderer.texture_vertices);
    begin();rect(3,7,11,17,23,29,31);
    assert(renderer.count==6 && sizeof(struct rect_vertex)==12);
    struct rect_vertex a={2.f*3/W-1.f,2.f*7/H-1.f,23,29,31,255};
    assert(!memcmp(renderer.vertices,&a,sizeof(a)));
    compare("unchanged opaque rectangle vertices",0);

    unsigned char pixels[7*5*4];pattern(pixels,7,5);
    unsigned char translucent[]={240,80,160,128};
    struct rect_renderer_texture t={0},overlay={0},invalid={0};
    assert(!rect_renderer_texture_create(&renderer,&invalid,7,5,pixels,sizeof(pixels)-1));
    assert(!rect_renderer_texture_create(&renderer,&invalid,INT_MAX,INT_MAX,pixels,sizeof(pixels)));
    assert(!rect_renderer_texture_create(&renderer,&invalid,0,5,pixels,sizeof(pixels)));
    assert(!invalid.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    assert(rect_renderer_texture_create(&renderer,&t,7,5,pixels,sizeof(pixels)));
    GLint alignment=0;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);assert(alignment==8);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    assert(!rect_renderer_texture_create(&renderer,&t,7,5,pixels,sizeof(pixels)) && t.id);
    assert(rect_renderer_texture_create(&renderer,&overlay,1,1,translucent,sizeof(translucent)));

    begin();
    sprite(&t,pixels,4,6,35,25,0,0,7,5,255,255,255,255,false,0,0,W,H);
    sprite(&t,pixels,50,6,13,19,1,1,5,3,255,255,255,255,true,0,0,W,H);
    assert(renderer.texture_count==12 && renderer.batches==0);
    compare("NPOT top-down orientation, source region and flip",0);

    begin();
    sprite(&t,pixels,-7,-9,67,49,1,1,5,3,255,255,255,255,false,16,12,37,21);
    sprite(&t,pixels,61,3,61,43,1,0,5,5,255,255,255,255,true,72,12,37,21);
    /* Different clip bounds must coexist in one texture batch. */
    assert(renderer.texture_count==12 && renderer.batches==0);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
    compare("partial safe-inset clipping preserves original UV and flip",0);

    begin();
    rect(0,0,W,H,10,30,50);
    sprite(&overlay,translucent,4,5,42,37,0,0,1,1,128,255,64,128,false,0,0,W,H);
    sprite(&t,pixels,15,17,35,25,0,0,7,5,255,255,255,128,true,0,0,W,H);
    rect(22,20,9,11,200,190,180);
    sprite(&overlay,translucent,20,18,40,30,0,0,1,1,255,128,255,200,false,0,0,W,H);
    compare("rect/texture switches, tint and straight-alpha ordering",2);
    assert(renderer.batches==5 && renderer.rectangles==2 && renderer.sprites==3);
    begin();
    sprite(&overlay,translucent,4,5,42,37,0,0,1,1,255,255,255,255,false,0,0,W,H);
    compare("straight-alpha output on transparent framebuffer",1);

    begin();
    for (int i=0;i<TEXTURE_BATCH_CAPACITY*2+1;i++)
        sprite(&t,pixels,i%W,(i/W)*5,1,1,0,0,1,1,255,255,255,255,false,0,0,W,H);
    compare("bounded texture batch overflow",0);assert(renderer.batches==3);
    begin();
    for (int i=0;i<RECT_BATCH_CAPACITY*2+1;i++) rect(i%W,(i/W)%H,1,1,(unsigned char)i,70,90);
    compare("bounded opaque rectangle batch overflow",0);assert(renderer.batches==3);

    static unsigned char splash[288*216*4];pattern(splash,288,216);
    struct rect_renderer_texture art={0};
    assert(rect_renderer_texture_create(&renderer,&art,288,216,splash,sizeof(splash)));
    const int sizes[][2]={{320,240},{288,216},{256,192},{17,13}};
    for (size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++) for (int flip=0;flip<2;flip++) {
        begin();
        sprite(&art,splash,0,0,sizes[i][0],sizes[i][1],0,0,288,216,
               255,255,255,255,flip,0,0,W,H);
        compare("splash integer-edge pixel mapping",0);
    }
    begin();
    sprite(&art,splash,-19,-13,320,240,0,0,288,216,255,255,255,255,true,16,12,256,192);
    compare("scaled splash clipped to playable viewport",0);

    begin();
    rect_renderer_sprite(&renderer,&t,0,0,10,10,6,0,2,1,255,255,255,255,false);
    rect_renderer_sprite(&renderer,&t,0,0,10,10,-1,0,1,1,255,255,255,255,false);
    rect_renderer_sprite(&renderer,&t,0,0,10,10,0,0,7,5,255,255,255,0,false);
    rect_renderer_sprite(&renderer,&t,INT_MAX,INT_MIN,INT_MAX,INT_MAX,0,0,7,5,255,255,255,255,false);
    rect_renderer_sprite_clipped(&renderer,&t,0,0,10,10,0,0,7,5,255,255,255,255,false,0,0,0,1);
    assert(!renderer.texture_count && !renderer.sprites && !renderer.batches);
    compare("invalid and fully clipped sprites are no-ops",0);

    begin();
    sprite(&t,pixels,0,0,7,5,0,0,7,5,255,255,255,255,false,0,0,W,H);
    /* Creating/deleting a different texture must not disturb the queued one. */
    struct rect_renderer_texture temporary={0};
    assert(rect_renderer_texture_create(&renderer,&temporary,1,1,translucent,sizeof(translucent)));
    rect_renderer_texture_delete(&renderer,&temporary);assert(!renderer.batches);
    GLuint old_id=t.id;rect_renderer_texture_delete(&renderer,&t);
    assert(!t.id && !t.width && !t.height && renderer.batches==1 && !glIsTexture(old_id));
    compare("delete flushes queued uses, upload preserves pending batch",0);
    rect_renderer_texture_delete(&renderer,&t);
    rect_renderer_texture_delete(&renderer,&overlay);rect_renderer_texture_delete(&renderer,&art);
    rect_renderer_destroy(&renderer);rect_renderer_destroy(&renderer);
    assert(glGetError()==GL_NO_ERROR);
}

int main(void)
{
#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha=true;attrs.antialias=false;attrs.premultipliedAlpha=false;attrs.majorVersion=1;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context=emscripten_webgl_create_context("#canvas",&attrs);
    assert(context>0 && emscripten_webgl_make_context_current(context)==EMSCRIPTEN_RESULT_SUCCESS);
#else
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    assert(get_display);
    EGLDisplay display=get_display(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    EGLint major,minor;assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attributes,&config,1,&count) && count);
    const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);assert(context!=EGL_NO_CONTEXT);
    const EGLint surface_attributes[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attributes);assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
#endif
    glViewport(0,0,W,H);glDisable(GL_DITHER);
    tests();puts("TEXTURE_TESTS_PASSED");
#ifdef __EMSCRIPTEN__
    emscripten_run_script("document.body.dataset.textureTests='passed';");
    emscripten_webgl_destroy_context(context);
#else
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
#endif
    return 0;
}
