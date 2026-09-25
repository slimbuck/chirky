/* On-device, real-scanout benchmark. Run through run-performance.py, which
   stops/restores the normal host and captures an isolated VC4 kernel trace.
   Custom: OUTPUT MODE LOOPS SHADER LAYERS SCENE [FRAMES [legacy|assets [GAME]]]
   GAME defaults to phosphor-run; rosey-chop is also supported. */
#include "../src/audio_mixer.h"
/* Host activation normally starts audio even with a quiet sound callback.
   Keep the benchmark free of audio devices and mixer CPU in either mode. */
static struct audio_mixer *benchmark_audio_start(const char *device) {(void)device;return NULL;}
#define audio_mixer_start benchmark_audio_start
#define main unused_host_main
#include "../src/host.c"
#undef main
#undef audio_mixer_start
#include "../src/rect_renderer.c"
#include <assert.h>
extern void glFinish(void);
extern void glBlendFunc(unsigned,unsigned);
extern int glGetUniformLocation(unsigned,const char *);
extern void glUniform1i(int,int);
extern unsigned glGetError(void);

static struct host h;
static struct gbm_bo *queued;
static int marker=-1;
static unsigned normal_program,work_program;
static volatile uint32_t checksum;
static bool asset_mode;
static const char *benchmark_game="phosphor-run";
static int benchmark_game_index=-1;
static void quiet_sound(void *ctx,const char *device,const char *path) {(void)ctx;(void)device;(void)path;}
static void quiet_sound_handle(void *ctx,chirky_asset sound) {(void)ctx;(void)sound;}
enum {START,WORK,DRAW,FINISH,PREWAIT,SWAP,LOCK,FB,SUBMIT,POINTS};
struct record {
    unsigned id,stage,index,sequence,missed;
    uint64_t wall[POINTS],cpu[POINTS],present,callback;
};
static struct record records[20000];
static unsigned used,last_sequence;
static uint64_t last_present;
static void mark(unsigned id,const char *phase)
{
    if(marker<0)return;
    char line[100];int n=snprintf(line,sizeof(line),"chirky-bench frame=%u %s\n",id,phase);
    ssize_t written=write(marker,line,(size_t)n);assert(written==n);
}
static uint64_t cpu_us(void)
{
    struct timespec t;assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void point(struct record *r,int p) {r->wall[p]=monotonic_us();r->cpu[p]=cpu_us();}
static void work(uint64_t loops)
{
    uint32_t x=checksum+12345;
    for(uint64_t i=0;i<loops;i++){x^=x<<13;x^=x>>17;x^=x<<5;}
    checksum=x;
}
static void completed(int fd,unsigned sequence,unsigned sec,unsigned usec,void *data)
{
    (void)fd;struct record *r=data;
    r->sequence=sequence;r->present=(uint64_t)sec*1000000+usec;r->callback=monotonic_us();
    if(last_present)r->missed=sequence-last_sequence-1;
    last_sequence=sequence;last_present=r->present;h.flip_pending=false;
}
static void retire(void)
{
    if(!h.flip_pending)return;
    struct pollfd fd={h.drm_fd,POLLIN,0};
    drmEventContext events={.version=DRM_EVENT_CONTEXT_VERSION,.page_flip_handler=completed};
    while(h.flip_pending) {
        int n=poll(&fd,1,10000);assert(n>0);assert(!drmHandleEvent(h.drm_fd,&events));
    }
    if(h.front_bo)gbm_surface_release_buffer(h.gbm_surface,h.front_bo);
    h.front_bo=queued;queued=NULL;
}
static void make_work_shader(void)
{
    unsigned v=shader(0x8b31,"attribute vec2 position;attribute vec4 colour;varying lowp vec4 tint;void main(){gl_Position=vec4(position,0.,1.);tint=colour;}");
    unsigned f=shader(0x8b30,"precision mediump float;varying lowp vec4 tint;uniform int work;void main(){vec4 c=tint*.173+vec4(.13,.27,.39,.51);for(int i=0;i<128;i++){if(i>=work)break;c=fract(c*vec4(1.17,1.31,1.47,1.61)+c.wxyz*.73+.017);}gl_FragColor=vec4(c.rgb*.12,.25);}");
    assert(v && f);work_program=glCreateProgram();glAttachShader(work_program,v);glAttachShader(work_program,f);
    glBindAttribLocation(work_program,0,"position");glBindAttribLocation(work_program,1,"colour");
    glLinkProgram(work_program);int ok=0;glGetProgramiv(work_program,0x8b82,&ok);assert(ok);
    glDeleteShader(v);glDeleteShader(f);
}
static void render(int shader_work,int layers,int scene)
{
    h.renderer.program=scene?normal_program:work_program;
    if(scene) {glDisable(0x0be2);draw_host(&h);return;}
    rect_renderer_begin(&h.renderer);clear_screen();
    glUseProgram(work_program);glUniform1i(glGetUniformLocation(work_program,"work"),shader_work);
    glEnable(0x0be2);glBlendFunc(0x0302,0x0303);
    for(int i=0;i<layers;i++)rect_renderer_rect(&h.renderer,0,0,h.mode.hdisplay,h.mode.vdisplay,40,60,80);
    rect_renderer_flush(&h.renderer);assert(!glGetError());
}
static bool wait_assets(void)
{
    uint64_t deadline=monotonic_us()+15000000;
    for(;;) {
        if(stop_requested)return false;
        finish_loading(&h);
        enum chirky_asset_state launcher=h.assets?
            asset_store_state(h.assets,h.launcher_art.image):CHIRKY_ASSET_READY;
        if(!h.pending_game && launcher!=CHIRKY_ASSET_LOADING) {
            if(launcher!=CHIRKY_ASSET_READY)fprintf(stderr,"Benchmark launcher asset failed\n");
            return launcher==CHIRKY_ASSET_READY;
        }
        if(monotonic_us()>=deadline) {fprintf(stderr,"Benchmark asset loading timed out\n");return false;}
        usleep(1000);
    }
}
static bool setup(void)
{
    h.drm_fd=-1;h.control_fd=-1;h.running=true;
    signal(SIGINT,on_stop);signal(SIGTERM,on_stop);
    discover_games(&h);load_launcher(&h);load_host_config(&h);
    for(int i=0;i<h.game_count;i++)if(!strcmp(h.games[i].id,benchmark_game))benchmark_game_index=i;
    if(benchmark_game_index<0) {fprintf(stderr,"Benchmark game not found: %s\n",benchmark_game);return false;}
    for(int i=0;i<launcher_count(&h.launcher,false);i++)
        if(launcher_at(&h.launcher,false,i)->action==benchmark_game_index)h.selected_game=i;
    h.drm_fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);assert(h.drm_fd>=0);
    assert(!drmSetMaster(h.drm_fd) && choose_display(&h) && init_graphics(&h));
    h.api=(struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,.screen_width=h.mode.hdisplay,.screen_height=h.mode.vdisplay,
        .context=&h,.fill_rect=fill_rect,.draw_text=draw_text,.button_label=button_label,.play_sound=quiet_sound};
    if(asset_mode) {
        h.assets=asset_store_create();if(!h.assets)return false;
        h.api.asset_request=request_asset;h.api.asset_status=status_asset;
        h.api.asset_data=data_asset;h.api.asset_release=release_asset;
        h.api.draw_sprite=draw_sprite;h.api.sound_play=quiet_sound_handle;
    }
    update_safe_area(&h);
    splash_load_file_api(&h.launcher_art,&h.api,"assets/launcher/splash.ppm");
    if(!wait_assets())return false;
    normal_program=h.renderer.program;make_work_shader();
    clear_screen();assert(eglSwapBuffers(h.egl_display,h.egl_surface));
    h.front_bo=gbm_surface_lock_front_buffer(h.gbm_surface);assert(h.front_bo);
    uint32_t fb=framebuffer_for_bo(&h,h.front_bo);assert(fb);
    assert(!drmModeSetCrtc(h.drm_fd,h.crtc_id,fb,0,0,&h.connector_id,1,&h.mode));
    return true;
}
struct stage {const char *name;int mode,shader,layers,scene,sleep;uint64_t loops;};
static bool freeze_gameplay(void)
{
    struct chirky_input input={0};
    /* Two neutral updates also release a gate that was already blocked. */
    h.game_api->update(&input);h.game_api->update(&input);
    input.buttons[CHIRKY_BUTTON_B]=input.button_pressed[CHIRKY_BUTTON_B]=true;
    h.game_api->update(&input);memset(&input,0,sizeof(input));
    h.game_api->update(&input);h.game_api->update(&input);
    /* Both module enums use TITLE=0, PLAY=1; garden starts with its phase.
       Copy the native enum representation without aliasing it through int*. */
    const char *name=!strcmp(benchmark_game,"rosey-chop")?"garden":"phase";
    dlerror();void *state=dlsym(h.game_library,name);const char *error=dlerror();
    if(error || !state) {fprintf(stderr,"Cannot verify frozen gameplay: %s\n",error?error:name);return false;}
    int phase=0;memcpy(&phase,state,sizeof(phase));
    if(phase!=1) {fprintf(stderr,"Benchmark game %s did not enter PLAY (phase=%d)\n",benchmark_game,phase);return false;}
    printf("Frozen gameplay verified: game=%s phase=PLAY\n",benchmark_game);
    return true;
}
static bool run(struct stage s,unsigned stage,int frames)
{
    retire();
    if(s.scene>=2 && !h.active_game) {
        if(!load_game(&h,benchmark_game_index) || !wait_assets() || !h.active_game) {
            fprintf(stderr,"Benchmark game activation failed: %s\n",benchmark_game);return false;
        }
    }
    if(s.scene==3 && !freeze_gameplay())return false;
    printf("STAGE %u %s mode=%d loops=%llu shader=%d layers=%d\n",stage,s.name,s.mode,(unsigned long long)s.loops,s.shader,s.layers);fflush(stdout);
    for(int i=0;i<frames+12 && !stop_requested;i++) {
        assert(used<sizeof(records)/sizeof(records[0]));struct record *r=&records[used++];
        r->id=used;r->stage=stage;r->index=(unsigned)i;
        mark(r->id,"start");point(r,START);
        work(s.loops);if(s.sleep)usleep((useconds_t)s.sleep);
        point(r,WORK);mark(r->id,"draw");
        render(s.shader,s.layers,s.scene);point(r,DRAW);
        if(s.mode==2)glFinish();
        point(r,FINISH);
        /* Pipeline experiment: prepare frame N while N-1 is pending, then
           retire N-1 before swap so GBM cannot deadlock on locked buffers. */
        if(s.mode==1)retire();
        point(r,PREWAIT);mark(r->id,"swap");
        assert(eglSwapBuffers(h.egl_display,h.egl_surface));point(r,SWAP);
        queued=gbm_surface_lock_front_buffer(h.gbm_surface);assert(queued);point(r,LOCK);
        uint32_t fb=framebuffer_for_bo(&h,queued);assert(fb);point(r,FB);
        h.flip_pending=true;
        assert(!drmModePageFlip(h.drm_fd,h.crtc_id,fb,DRM_MODE_PAGE_FLIP_EVENT,r));
        point(r,SUBMIT);mark(r->id,"submitted");
        if(s.mode!=1)retire();
    }
    retire();
    return true;
}
int main(int argc,char **argv)
{
    assert(argc>=2);const char *marker_env=getenv("CHIRKY_BENCH_MARKER_FD");if(marker_env)marker=atoi(marker_env);
    if(argc>=9) {
        assert(!strcmp(argv[8],"legacy") || !strcmp(argv[8],"assets"));
        asset_mode=!strcmp(argv[8],"assets");
    }
    if(argc>=10)benchmark_game=argv[9];
    assert(!strcmp(benchmark_game,"phosphor-run") || !strcmp(benchmark_game,"rosey-chop"));
    printf("Benchmark assets=%s game=%s audio=disabled\n",asset_mode?"enabled":"legacy",benchmark_game);
    struct timespec resolution;clock_getres(CLOCK_MONOTONIC,&resolution);
    printf("CLOCK_MONOTONIC resolution %ld ns\n",resolution.tv_nsec);
    clock_getres(CLOCK_THREAD_CPUTIME_ID,&resolution);printf("CLOCK_THREAD_CPUTIME_ID resolution %ld ns\n",resolution.tv_nsec);
    uint64_t t=cpu_us();work(1000000);uint64_t cost=cpu_us()-t;
    uint64_t unit=1000000000/(cost?cost:1); /* calibrated approximate 1 ms; fixed loops thereafter */
    printf("CPU calibration: 1000000 iterations %lluus, unit %llu iterations\n",(unsigned long long)cost,(unsigned long long)unit);
    if(!setup()) {cleanup(&h);return stop_requested?130:EXIT_FAILURE;}
    struct stage stages[64];unsigned n=0;
    if(argc>=7) {
        stages[n++]=(struct stage){.name="custom",.mode=atoi(argv[2]),.loops=strtoull(argv[3],NULL,10),.shader=atoi(argv[4]),.layers=atoi(argv[5]),.scene=atoi(argv[6])};
        assert(stages[0].mode>=0 && stages[0].mode<=2 && stages[0].shader>=0 && stages[0].shader<=128);
        assert(stages[0].layers>=0 && stages[0].layers<=64 && stages[0].scene>=0 && stages[0].scene<=3);
    } else {
        stages[n++]=(struct stage){.name="baseline",.layers=1};
        stages[n++]=(struct stage){.name="sleep_10ms",.layers=1,.sleep=10000};
        int multipliers[]={1,2,4,8,12,16,24,32};
        for(unsigned i=0;i<sizeof(multipliers)/sizeof(*multipliers);i++)stages[n++]=(struct stage){.name="cpu",.layers=1,.loops=unit*multipliers[i]};
        int workloads[]={1,2,4,8,16,32,64,128};
        for(unsigned i=0;i<sizeof(workloads)/sizeof(*workloads);i++)stages[n++]=(struct stage){.name="gpu",.layers=1,.shader=workloads[i]};
        for(int layers=2;layers<=8;layers*=2)stages[n++]=(struct stage){.name="overdraw",.layers=layers,.shader=32};
        for(int mode=0;mode<=2;mode++) {
            stages[n++]=(struct stage){.name="mixed",.mode=mode,.layers=1,.shader=32,.loops=unit*8};
            stages[n++]=(struct stage){.name="mixed",.mode=mode,.layers=1,.shader=64,.loops=unit*12};
        }
        stages[n++]=(struct stage){.name="launcher",.scene=1};
        stages[n++]=(struct stage){.name="phosphor_title",.scene=2};
        stages[n++]=(struct stage){.name="phosphor_play",.scene=3};
    }
    int frames=argc>=8?atoi(argv[7]):60;assert(frames>0 && frames<=1000);
    bool ok=true;
    for(unsigned i=0;i<n && !stop_requested;i++)if(!run(stages[i],i,frames)) {ok=false;break;}
    FILE *out=fopen(argv[1],"w");assert(out);
    fputs("pid,id,stage,name,mode,index,loops,shader,layers,scene",out);
    const char *names[]={"start","work","draw","finish","prewait","swap","lock","fb","submit"};
    for(int i=0;i<POINTS;i++)fprintf(out,",%s_wall,%s_cpu",names[i],names[i]);
    fputs(",present,callback,sequence,missed\n",out);
    for(unsigned i=0;i<used;i++) {
        struct record *r=&records[i];struct stage *s=&stages[r->stage];
        fprintf(out,"%d,%u,%u,%s,%d,%u,%llu,%d,%d,%d",getpid(),r->id,r->stage,s->name,s->mode,r->index,(unsigned long long)s->loops,s->shader,s->layers,s->scene);
        for(int j=0;j<POINTS;j++)fprintf(out,",%llu,%llu",(unsigned long long)r->wall[j],(unsigned long long)r->cpu[j]);
        fprintf(out,",%llu,%llu,%u,%u\n",(unsigned long long)r->present,(unsigned long long)r->callback,r->sequence,r->missed);
    }
    fclose(out);h.renderer.program=normal_program;glDeleteProgram(work_program);cleanup(&h);
    printf("Saved %u frames; checksum %u\n",used,checksum);return stop_requested?130:ok?0:EXIT_FAILURE;
}
