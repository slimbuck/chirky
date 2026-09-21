/* Offscreen GLES benchmark: no DRM master, modeset, input, or running-game changes.
   Run from the repository root after compiling with --gc-sections and EGL/GLES. */
#define main unused_host_main
#include "../src/host.c"
#undef main
#include <assert.h>
extern EGLSurface eglCreatePbufferSurface(EGLDisplay display,EGLConfig config,const EGLint *attributes);
extern void glFinish(void);
extern unsigned int glGetError(void);
static void quiet_sound(void *ctx,const char *device,const char *path) { (void)ctx;(void)device;(void)path; }
static int compare_time(const void *a,const void *b)
{ uint64_t aa=*(const uint64_t *)a,bb=*(const uint64_t *)b;return (aa>bb)-(aa<bb); }
int main(int argc,char **argv)
{
    const char *module=argc>1?argv[1]:"build/games/rosey-chop.so";
    struct host h={0};h.mode.hdisplay=320;h.mode.vdisplay=240;h.safe_x=16;h.safe_y=12;
    h.egl_display=eglGetPlatformDisplay(0x31dd,NULL,NULL);
    EGLint major,minor;assert(eglInitialize(h.egl_display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attributes[]={EGL_SURFACE_TYPE,1,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(h.egl_display,attributes,&config,1,&count)&&count);
    EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    h.egl_context=eglCreateContext(h.egl_display,config,EGL_NO_CONTEXT,context_attributes);
    EGLint surface_attributes[]={0x3057,320,0x3056,240,EGL_NONE};
    h.egl_surface=eglCreatePbufferSurface(h.egl_display,config,surface_attributes);
    assert(eglMakeCurrent(h.egl_display,h.egl_surface,h.egl_surface,h.egl_context));
    glViewport(0,0,320,240);
    h.api=(struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,.context=&h,
        .fill_rect=fill_rect,.draw_text=draw_text,.play_sound=quiet_sound,.button_label=button_label};
    update_safe_area(&h);
    void *library=dlopen(module,RTLD_NOW);if(!library){fprintf(stderr,"%s\n",dlerror());return 1;}
    chirky_game_entry_fn entry=NULL;void *symbol=dlsym(library,"chirky_game_entry");memcpy(&entry,&symbol,sizeof(entry));assert(entry);
    const struct chirky_game_api *game=entry();assert(game->abi_version==CHIRKY_ABI_VERSION);
    assert(game->init(&h.api,"games/rosey-chop/game.conf"));
    struct chirky_input input={0};input.button_pressed[CHIRKY_BUTTON_B]=true;game->update(&input);
    /* RGBA/UNSIGNED_BYTE is the guaranteed GLES2 readback pair. RGB is not. */
    unsigned char baseline[320*240*4],result[320*240*4];
    for(int mode=0;mode<2;mode++) {
        if(mode)assert(rect_renderer_init(&h.renderer,320,240));
        uint64_t times[180],sum=0;
        for(int i=-20;i<180;i++) {
            h.submitted_rectangles=0;uint64_t began=monotonic_us();
            rect_renderer_begin(&h.renderer);clear_screen();game->render();rect_renderer_flush(&h.renderer);glFinish();
            if(i>=0){times[i]=monotonic_us()-began;sum+=times[i];}
        }
        qsort(times,180,sizeof(*times),compare_time);
        printf("%s rectangles=%u GPU_batches=%u mean=%.3fms p50=%.3fms p95=%.3fms max=%.3fms\n",
            mode?"BATCHED":"LEGACY",h.submitted_rectangles,mode?h.renderer.batches:h.submitted_rectangles,
            sum/180000.f,times[90]/1000.f,times[171]/1000.f,times[179]/1000.f);
        glReadPixels(0,0,320,240,0x1908,GL_UNSIGNED_BYTE,mode?result:baseline);
        assert(glGetError()==0);
    }
    int different=0;for(size_t i=0;i<sizeof(result);i++)different+=result[i]!=baseline[i];
    printf("Pixel comparison: %d different channels out of %zu\n",different,sizeof(result));
    if(different) {
        int shown=0;
        for(size_t i=0;i<sizeof(result) && shown<8;i++)if(result[i]!=baseline[i]) {
            printf("Difference x=%zu y=%zu channel=%zu old=%u new=%u\n",(i/4)%320,(i/4)/320,i%4,baseline[i],result[i]);shown++;
        }
    }
    /* Force multiple batch flushes with overlapping and clipped rectangles. */
    unsigned int program=h.renderer.program;
    for(int mode=0;mode<2;mode++) {
        h.renderer.program=mode?program:0;rect_renderer_begin(&h.renderer);clear_screen();
        for(int i=0;i<RECT_BATCH_CAPACITY*3+5;i++)
            fill_rect(&h,(i*17)%320-16,(i*31)%240-12,23,19,(unsigned char)i,(unsigned char)(i/3),(unsigned char)(i/7));
        rect_renderer_flush(&h.renderer);glFinish();
        glReadPixels(0,0,320,240,0x1908,GL_UNSIGNED_BYTE,mode?result:baseline);
        assert(glGetError()==0);
    }
    int stress_difference=0;
    for(size_t i=0;i<sizeof(result);i++)stress_difference+=result[i]!=baseline[i];
    printf("Overflow/clipping comparison: %d different channels, %u GPU batches\n",stress_difference,h.renderer.batches);
    assert(h.renderer.batches>=3);
    different+=stress_difference;
    game->shutdown();dlclose(library);rect_renderer_destroy(&h.renderer);
    eglMakeCurrent(h.egl_display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(h.egl_display,h.egl_surface);eglDestroyContext(h.egl_display,h.egl_context);eglTerminate(h.egl_display);
    return different?1:0;
}
