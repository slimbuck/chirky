/* Offscreen GLES review sheet, using production mesh code; no live Pi input. */
#include "../games/phosphor-run/robot.h"
#include "rect_renderer.h"
#include "drawing.h"
#define main unused_host_main
#include "../src/platform/linux/host.c"
#undef main
#include <assert.h>
extern EGLSurface eglCreatePbufferSurface(EGLDisplay display,EGLConfig config,const EGLint *attributes);
extern unsigned int glGetError(void);
extern void glFinish(void);
static struct rect_renderer renderer;
static bool mesh(void *ctx,const struct chirky_mesh_vertex *v,size_t n,float ambient)
{ (void)ctx;return rect_renderer_mesh(&renderer,v,n,ambient,0,0,640,480); }
static void fill(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{ (void)ctx;rect_renderer_rect(&renderer,x,y,w,h,r,g,b); }
int main(int argc,char **argv)
{
    assert(argc==2);
    EGLDisplay display=eglGetPlatformDisplay(0x31dd,NULL,NULL);
    EGLint major,minor;assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attributes[]={EGL_SURFACE_TYPE,1,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,0x3025,16,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attributes,&config,1,&count) && count);
    const EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE},sa[]={0x3057,640,0x3056,480,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
    EGLSurface surface=eglCreatePbufferSurface(display,config,sa);
    assert(eglMakeCurrent(display,surface,surface,context));
    assert(rect_renderer_init(&renderer,640,480));assert(robot_load("games/phosphor-run/game.conf"));
    struct chirky_host_api api={.screen_width=640,.screen_height=480,.draw_mesh=mesh,.fill_rect=fill};
    glViewport(0,0,640,480);glDisable(0x0bd0);glClearColor(.025f,.05f,.065f,1);glClear(GL_COLOR_BUFFER_BIT);
    rect_renderer_begin(&renderer);
    const char *names[]={"IDLE","RUN","JUMP","FALL","DASH","DEATH"};
    for(int clip=0;clip<6;clip++) {
        int x=52+clip*106;chirky_draw_text(&api,x-18,465,names[clip],1,210,230,220);
        for(int frame=0;frame<4;frame++) {
            float tick=clip==ROBOT_IDLE?(float[]){0,90,112,200}[frame]:frame*5.f;
            int y=350-frame*100;
            fill(NULL,x-44,y-1,88,1,40,65,70);
            assert(robot_draw(&api,x-34,y+74,frame==3?-1:1,(enum robot_clip)clip,tick));
            assert(robot_draw_view(&api,x,y,frame==3?-1:1,(enum robot_clip)clip,tick,NULL,4));
        }
    }
    rect_renderer_flush(&renderer);unsigned char *pixels=malloc(640*480*4);assert(pixels);
    glReadPixels(0,0,640,480,0x1908,GL_UNSIGNED_BYTE,pixels);assert(!glGetError());
    FILE *out=fopen(argv[1],"wb");assert(out);fprintf(out,"P6\n640 480\n255\n");
    for(int y=479;y>=0;y--)for(int x=0;x<640;x++)fwrite(pixels+(y*640+x)*4,1,3,out);
    fclose(out);free(pixels);
    printf("Renderer: %s\n",glGetString(0x1f01));
    for(int scale=1;scale<=5;scale+=4) {
        glFinish();uint64_t start=monotonic_us();clock_t cpu=clock();
        for(int frame=0;frame<300;frame++) {
            rect_renderer_begin(&renderer);assert(robot_draw_view(&api,160,48,1,ROBOT_RUN,(float)frame,NULL,(float)scale));
            rect_renderer_flush(&renderer);glFinish();
        }
        printf("Robot %dx: %.3f ms mean including GPU completion, %.3f ms process CPU, one mesh draw\n",
               scale,(monotonic_us()-start)/300000.,1000.*(clock()-cpu)/CLOCKS_PER_SEC/300);
    }
    robot_free();rect_renderer_destroy(&renderer);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
