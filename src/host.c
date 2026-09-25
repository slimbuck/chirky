#include "chirky.h"
#include "input_bindings.h"
#include "rect_renderer.h"
#include "frame_timing.h"
#include "gpu_timing.h"
#include "profile.h"
#include "trace.h"
#include "asset_store.h"
#include "audio_mixer.h"
#include "image_cache.h"
#include "asset_file.h"
#include "input_gate.h"
#include "launcher_config.h"
#include "launcher_wordmark.h"
#include "splash_art.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Minimal ABI declarations keep the Raspberry Pi OS Lite build offline. */
#define DRM_DISPLAY_MODE_LEN 32
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#define DRM_EVENT_CONTEXT_VERSION 2
#define DRM_FORMAT_XRGB8888 0x34325258u

typedef struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh, flags, type;
    char name[DRM_DISPLAY_MODE_LEN];
} drmModeModeInfo;

typedef struct drm_mode_res {
    int count_fbs;
    uint32_t *fbs;
    int count_crtcs;
    uint32_t *crtcs;
    int count_connectors;
    uint32_t *connectors;
    int count_encoders;
    uint32_t *encoders;
    uint32_t min_width, max_width, min_height, max_height;
} drmModeRes;

typedef struct drm_mode_connector {
    uint32_t connector_id, encoder_id, connector_type, connector_type_id;
    int connection;
    uint32_t mmWidth, mmHeight;
    int subpixel, count_modes;
    drmModeModeInfo *modes;
    int count_props;
    uint32_t *props;
    uint64_t *prop_values;
    int count_encoders;
    uint32_t *encoders;
} drmModeConnector;

typedef struct drm_mode_encoder {
    uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
} drmModeEncoder;

typedef struct drm_mode_crtc {
    uint32_t crtc_id, buffer_id, x, y, width, height;
    int mode_valid;
    drmModeModeInfo mode;
    int gamma_size;
} drmModeCrtc;

typedef struct drm_event_context {
    int version;
    void (*vblank_handler)(int, unsigned int, unsigned int, unsigned int, void *);
    void (*page_flip_handler)(int, unsigned int, unsigned int, unsigned int, void *);
} drmEventContext;

extern drmModeRes *drmModeGetResources(int fd);
extern void drmModeFreeResources(drmModeRes *ptr);
extern drmModeConnector *drmModeGetConnector(int fd, uint32_t connector_id);
extern void drmModeFreeConnector(drmModeConnector *ptr);
extern drmModeEncoder *drmModeGetEncoder(int fd, uint32_t encoder_id);
extern void drmModeFreeEncoder(drmModeEncoder *ptr);
extern drmModeCrtc *drmModeGetCrtc(int fd, uint32_t crtc_id);
extern void drmModeFreeCrtc(drmModeCrtc *ptr);
extern int drmModeAddFB2(int fd, uint32_t width, uint32_t height,
                         uint32_t pixel_format, const uint32_t bo_handles[4],
                         const uint32_t pitches[4], const uint32_t offsets[4],
                         uint32_t *buf_id, uint32_t flags);
extern int drmModeRmFB(int fd, uint32_t buffer_id);
extern int drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t buffer_id,
                          uint32_t x, uint32_t y, uint32_t *connectors,
                          int count, drmModeModeInfo *mode);
extern int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                           uint32_t flags, void *user_data);
extern int drmHandleEvent(int fd, drmEventContext *evctx);
extern int drmSetMaster(int fd);
extern int drmDropMaster(int fd);

#define GBM_BO_USE_SCANOUT (1u << 0)
#define GBM_BO_USE_RENDERING (1u << 2)
struct gbm_device;
struct gbm_surface;
struct gbm_bo;
union gbm_bo_handle { void *ptr; int32_t s32; uint32_t u32; uint64_t u64; };
extern struct gbm_device *gbm_create_device(int fd);
extern void gbm_device_destroy(struct gbm_device *gbm);
extern struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                               uint32_t width, uint32_t height,
                                               uint32_t format, uint32_t flags);
extern void gbm_surface_destroy(struct gbm_surface *surface);
extern struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surface);
extern void gbm_surface_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo);
extern uint32_t gbm_bo_get_width(struct gbm_bo *bo);
extern uint32_t gbm_bo_get_height(struct gbm_bo *bo);
extern uint32_t gbm_bo_get_stride(struct gbm_bo *bo);
extern union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo);
extern void *gbm_bo_get_user_data(struct gbm_bo *bo);
extern void gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                                  void (*destroy_user_data)(struct gbm_bo *, void *));

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
typedef uint32_t EGLenum;
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PLATFORM_GBM_KHR 0x31D7
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
extern EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id);
extern EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                         const EGLint *attrib_list);
extern EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor);
extern EGLBoolean eglTerminate(EGLDisplay display);
extern EGLBoolean eglBindAPI(EGLenum api);
extern EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint *attrib_list,
                                   EGLConfig *configs, EGLint config_size,
                                   EGLint *num_config);
extern EGLContext eglCreateContext(EGLDisplay display, EGLConfig config,
                                    EGLContext share_context, const EGLint *attrib_list);
extern EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context);
extern EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                          EGLNativeWindowType win,
                                          const EGLint *attrib_list);
extern EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface);
extern EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw,
                                  EGLSurface read, EGLContext context);
extern EGLBoolean eglSwapInterval(EGLDisplay display, EGLint interval);
extern EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface);
extern EGLint eglGetError(void);
extern gpu_proc eglGetProcAddress(const char *name);
extern const unsigned char *glGetString(unsigned int name);

typedef int32_t GLsizei;
typedef int32_t GLint;
typedef uint32_t GLenum;
typedef uint32_t GLbitfield;
typedef float GLfloat;
typedef unsigned char GLubyte;
#define GL_COLOR_BUFFER_BIT 0x00004000u
#define GL_SCISSOR_TEST 0x0C11u
#define GL_RGB 0x1907u
#define GL_UNSIGNED_BYTE 0x1401u
extern void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
extern void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void glClear(GLbitfield mask);
extern void glEnable(GLenum cap);
extern void glDisable(GLenum cap);
extern void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
extern void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                         GLenum format, GLenum type, void *pixels);

#define MAX_INPUTS 32
#define MAX_GAMES 32
#define HOST_CONFIG_PATH "config/host.conf"

struct input_device {
    int fd;
    bool controller;
    char name[128];
    bool keys[KEY_MAX + 1];
    int abs_values[ABS_MAX + 1];
    int abs_minimums[ABS_MAX + 1];
    int abs_maximums[ABS_MAX + 1];
    int abs_flats[ABS_MAX + 1];
};

struct framebuffer { int drm_fd; uint32_t fb_id; };

struct input_set {
    struct input_device devices[MAX_INPUTS];
    int count;
    bool previous_buttons[CHIRKY_BUTTON_COUNT];
    bool pending_buttons[CHIRKY_BUTTON_COUNT];
    bool controller_up_pressed;
    bool controller_down_pressed;
    struct chirky_input state;
};

struct game_record {
    char id[64];
    char name[128];
    char description[256];
    char module_path[512];
    char config_path[512];
};

struct host {
    struct splash_art launcher_art;
    int drm_fd;
    uint32_t connector_id, crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;
    struct gbm_device *gbm;
    struct gbm_surface *gbm_surface;
    struct gbm_bo *front_bo;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    struct input_set inputs;
    struct game_record games[MAX_GAMES];
    int game_count;
    int selected_game;
    bool paused;
    int pause_option;
    struct launcher_config launcher;
    void *game_library;
    const struct chirky_game_api *game_api;
    const struct game_record *active_game;
    struct chirky_host_api api;
    int control_fd;
    bool flip_pending;
    bool running;
    pid_t sound_pid;
    unsigned int snapshot_sequence;
    unsigned long frame_number;
    struct rect_renderer renderer;
    unsigned int submitted_rectangles;
    struct frame_timing timing;
    struct gpu_timing gpu_timing;
    struct profile profile;
    struct trace_capture trace;
    struct asset_store *assets;
    struct audio_mixer *audio;
    char audio_device[128];
    enum audio_mixer_state audio_state;
    bool audio_reported;
    chirky_asset sound_pins[ASSET_STORE_SLOTS];
    size_t sound_pin_count;
    struct image_cache images;
    const struct game_record *pending_game;
    uint64_t loading_started;
    bool frame_timing_enabled;
    bool timing_start_held, timing_start_toggled;
    uint64_t timing_start_us;
    char boot_game_id[64];
    struct controller_binding bindings[CHIRKY_BUTTON_COUNT];
    struct controller_binding keyboard_bindings[CHIRKY_BUTTON_COUNT];
    struct binding_setup setup;
    bool settings_menu, controller_settings, display_settings, input_test, ui_wait_release, last_keyboard;
    struct chirky_input_gate transition_gate;
    int settings_option, selected_option, display_option, safe_x, safe_y, saved_safe_x, saved_safe_y;
    int safe_offset_x,safe_offset_y,saved_safe_offset_x,saved_safe_offset_y;
    const char *settings_message;
    unsigned int controller_menu_chord_frames;

};

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t snapshot_requested;
static uint64_t monotonic_us(void);
static unsigned int frame_budget_us(const struct host *host);

static chirky_asset request_asset(void *context,const char *path,enum chirky_asset_type type)
{ return asset_store_request(((struct host *)context)->assets,path,type); }
static enum chirky_asset_state status_asset(void *context,chirky_asset asset)
{ return asset_store_state(((struct host *)context)->assets,asset); }
static struct chirky_asset_view data_asset(void *context,chirky_asset asset)
{ return asset_store_view(((struct host *)context)->assets,asset); }
static void release_asset(void *context,chirky_asset asset)
{ asset_store_release(((struct host *)context)->assets,asset); }
static void draw_sprite(void *context,chirky_asset image,int x,int y,int w,int h,
    int sx,int sy,int sw,int sh,unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    struct host *host=context;
    image_cache_draw(&host->images,&host->renderer,&host->api,image,x,y,w,h,sx,sy,sw,sh,r,g,b,a,flip,
        host->safe_x+host->safe_offset_x,host->safe_y+host->safe_offset_y);
}
static void sound_play(void *context,chirky_asset sound)
{
    struct host *host=context;
    chirky_scope(&host->api,"audio.enqueue",true);
    if(host->audio && asset_store_state(host->assets,sound)==CHIRKY_ASSET_READY) {
        struct chirky_asset_view view=asset_store_view(host->assets,sound);
        size_t i=0;
        while(i<host->sound_pin_count && host->sound_pins[i]!=sound)i++;
        if(i==host->sound_pin_count && i<ASSET_STORE_SLOTS && view.rate && view.channels &&
            asset_store_retain(host->assets,sound))host->sound_pins[host->sound_pin_count++]=sound;
        if(i<host->sound_pin_count)audio_mixer_play(host->audio,view.data,view.size,view.rate,view.channels);
    }
    chirky_scope(&host->api,"audio.enqueue",false);
}

static void capture_scope(void *context,const char *name,bool begin)
{
    struct host *host=context;
    trace_scope(&host->trace,name,begin,host->submitted_rectangles);
}

static void block_transition_input(struct host *host)
{
    host->ui_wait_release=true;
    chirky_gate_begin(&host->transition_gate);
    memset(host->inputs.pending_buttons,0,sizeof(host->inputs.pending_buttons));
    memset(host->inputs.state.button_pressed,0,sizeof(host->inputs.state.button_pressed));
    memset(host->inputs.state.pressed,0,sizeof(host->inputs.state.pressed));
}

enum host_screen { SCREEN_LAUNCHER,SCREEN_SETTINGS,SCREEN_INPUT,SCREEN_SETUP,SCREEN_TEST,SCREEN_DISPLAY,SCREEN_GAME,SCREEN_PAUSE };
static enum host_screen current_screen(const struct host *host)
{
    if(host->setup.active)return SCREEN_SETUP;
    if(host->active_game)return host->paused?SCREEN_PAUSE:SCREEN_GAME;
    if(host->display_settings)return SCREEN_DISPLAY;
    if(host->controller_settings)return host->input_test?SCREEN_TEST:SCREEN_INPUT;
    if(host->settings_menu)return SCREEN_SETTINGS;
    return SCREEN_LAUNCHER;
}

static void on_stop(int signal_number) { (void)signal_number; stop_requested = 1; }
static void on_snapshot(int signal_number) { (void)signal_number; snapshot_requested = 1; }

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
    return text;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity > 0) snprintf(destination, capacity, "%s", source);
}

static const char *const button_config_keys[CHIRKY_BUTTON_COUNT] = {
    "bind_left", "bind_right", "bind_up", "bind_down",
    "bind_y", "bind_b", "bind_a", "bind_x", "bind_l", "bind_r", "bind_start", "bind_select"
};

static const char *const button_names[CHIRKY_BUTTON_COUNT] = {
    "LEFT", "RIGHT", "UP", "DOWN", "Y", "B", "A", "X", "L", "R", "START", "SELECT"
};

static const char *const keyboard_config_keys[CHIRKY_BUTTON_COUNT] = {
    "key_left", "key_right", "key_up", "key_down",
    "key_y", "key_b", "key_a", "key_x", "key_l", "key_r", "key_start", "key_select"
};

static int binding_button_for_key(const char *key)
{
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        if (!strcmp(key,button_config_keys[i])) return i;
        if (!strcmp(key,keyboard_config_keys[i])) return i+CHIRKY_BUTTON_COUNT;
    }
    return -1;
}

/* Read old action mappings once, then save only SNES button names. */
static int legacy_binding_for_key(const char *key)
{
    const char *names[]={"bind_jump","bind_dash","bind_confirm","bind_menu",
                         "key_jump","key_dash","key_confirm","key_menu"};
    for (int i=0;i<8;i++) if (!strcmp(key,names[i])) return i;
    return -1;
}

static void write_binding(FILE *file, const char *key, const struct controller_binding *binding)
{
    if (binding->kind==BINDING_KEY) fprintf(file,"%s=key:%u\n",key,binding->code);
    else if (binding->kind==BINDING_ABS) fprintf(file,"%s=abs:%u:%d\n",key,binding->code,binding->direction);
    else fprintf(file,"%s=none\n",key);
}

static bool save_bindings(const struct host *host)
{
    FILE *source=fopen(HOST_CONFIG_PATH,"r"), *target=fopen(HOST_CONFIG_PATH ".tmp","w");
    if (!target) { if (source) fclose(source); return false; }
    char line[512];
    while (source && fgets(line,sizeof(line),source)) {
        char parsed[512]; copy_text(parsed,sizeof(parsed),line);
        char *key=trim(parsed), *separator=strchr(key,'=');
        if (separator) *separator=0;
        key=trim(key);
        if (binding_button_for_key(key)<0 && legacy_binding_for_key(key)<0 && strcmp(key,"safe_x") && strcmp(key,"safe_y") && strcmp(key,"safe_offset_x") && strcmp(key,"safe_offset_y") && strcmp(key,"input_version"))
            fputs(line,target);
    }
    bool failed=source && ferror(source);
    if (source) fclose(source);
    fputs("\ninput_version=3\n",target);
    fprintf(target,"safe_x=%d\nsafe_y=%d\n",host->safe_x,host->safe_y);
    fprintf(target,"safe_offset_x=%d\nsafe_offset_y=%d\n",host->safe_offset_x,host->safe_offset_y);
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        write_binding(target,button_config_keys[i],&host->bindings[i]);
        write_binding(target,keyboard_config_keys[i],&host->keyboard_bindings[i]);
    }
    if (fflush(target)!=0 || fsync(fileno(target))!=0) failed=true;
    if (fclose(target)!=0) failed=true;
    if (!failed && rename(HOST_CONFIG_PATH ".tmp",HOST_CONFIG_PATH)!=0) failed=true;
    if (failed) remove(HOST_CONFIG_PATH ".tmp");
    return !failed;
}

static void clamp_safe_position(struct host *host)
{
    if(host->safe_offset_x < -host->safe_x)host->safe_offset_x=-host->safe_x;
    if(host->safe_offset_x > host->safe_x)host->safe_offset_x=host->safe_x;
    if(host->safe_offset_y < -host->safe_y)host->safe_offset_y=-host->safe_y;
    if(host->safe_offset_y > host->safe_y)host->safe_offset_y=host->safe_y;
}

static void update_safe_area(struct host *host)
{
    clamp_safe_position(host);
    host->api.screen_width=host->mode.hdisplay-host->safe_x*2;
    host->api.screen_height=host->mode.vdisplay-host->safe_y*2;
}

static void restore_display_area(struct host *host)
{
    host->safe_x=host->saved_safe_x;host->safe_y=host->saved_safe_y;
    host->safe_offset_x=host->saved_safe_offset_x;host->safe_offset_y=host->saved_safe_offset_y;
    update_safe_area(host);
}

static void keyboard_name(const struct controller_binding *binding, char *name, size_t capacity)
{
    if (binding->kind==BINDING_NONE) { copy_text(name,capacity,"UNBOUND"); return; }
    const char *label=NULL;
    switch (binding->code) {
        case KEY_UP: label="UP"; break; case KEY_DOWN: label="DOWN"; break;
        case KEY_LEFT: label="LEFT"; break; case KEY_RIGHT: label="RIGHT"; break;
        case KEY_ENTER: label="ENTER"; break; case KEY_ESC: label="ESC"; break;
        case KEY_SPACE: label="SPACE"; break; case KEY_TAB: label="TAB"; break;
        case KEY_F1: label="F1"; break; case KEY_F12: label="F12"; break;
        case KEY_LEFTSHIFT: label="LEFT SHIFT"; break; case KEY_RIGHTSHIFT: label="RIGHT SHIFT"; break;
        case KEY_LEFTCTRL: label="LEFT CTRL"; break; case KEY_RIGHTCTRL: label="RIGHT CTRL"; break;
    }
    static const unsigned int letters[]={KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    if (label) { copy_text(name,capacity,label); return; }
    for (int i=0;i<26;i++) if (binding->code==letters[i]) { snprintf(name,capacity,"%c",'A'+i); return; }
    snprintf(name,capacity,"KEY %u",binding->code);
}

static void button_label(void *context, enum chirky_button button,
                         char *text, size_t capacity)
{
    (void)context;
    copy_text(text,capacity,button>=0 && button<CHIRKY_BUTTON_COUNT ? button_names[button] : "UNBOUND");
}

static void fill_rect(void *context, int x, int y, int width, int height,
                      unsigned char red, unsigned char green, unsigned char blue)
{
    struct host *host = context;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > host->api.screen_width) width = host->api.screen_width - x;
    if (y + height > host->api.screen_height) height = host->api.screen_height - y;
    if (width <= 0 || height <= 0) return;
    host->submitted_rectangles++;
    if (host->renderer.program) {
        rect_renderer_rect(&host->renderer,x+host->safe_x+host->safe_offset_x,y+host->safe_y+host->safe_offset_y,width,height,red,green,blue);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(x+host->safe_x+host->safe_offset_x, y+host->safe_y+host->safe_offset_y, width, height);
    glClearColor(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void play_sound(void *context, const char *device, const char *path)
{
    struct host *host = context;
    if(host->assets) {
        chirky_asset asset=asset_store_request(host->assets,path,CHIRKY_ASSET_SOUND);
        sound_play(context,asset);asset_store_release(host->assets,asset);
        return;
    }
    if (host->sound_pid > 0) {
        if (waitpid(host->sound_pid, NULL, WNOHANG) == 0) return;
        host->sound_pid = 0;
    }
    pid_t child = fork();
    if (child == 0) {
        execlp("aplay", "aplay", "-q", "-D", device, path, (char *)NULL);
        _exit(127);
    }
    if (child > 0) host->sound_pid = child;
}

static void power_down_pi(void)
{
    pid_t child = fork();
    if (child == 0) {
        execlp("sudo", "sudo", "-n", "systemctl", "poweroff", (char *)NULL);
        _exit(127);
    }
}

static void write_status(const struct host *host)
{
    mkdir("run", 0755);
    FILE *file = fopen("run/status.json.tmp", "w");
    if (file == NULL) return;
    fprintf(file, "{\n  \"pid\": %ld,\n  \"mode\": \"%s\",\n  \"game\": \"%s\",\n"
                  "  \"width\": %u,\n  \"height\": %u,\n  \"refresh\": %u,\n"
                  "  \"viewport_width\": %d,\n  \"viewport_height\": %d,\n  \"profiling\": %s,\n  \"loading\": %s\n}\n",
            (long)getpid(), (const char *[]){"launcher","settings","input","setup","test","display","game","paused"}[current_screen(host)],
            host->active_game ? host->active_game->id : "",
            host->mode.hdisplay, host->mode.vdisplay, host->mode.vrefresh,
            host->api.screen_width, host->api.screen_height,
            host->frame_timing_enabled || host->trace.spans?"true":"false",host->pending_game?"true":"false");
    fclose(file);
    rename("run/status.json.tmp", "run/status.json");
}

static bool load_manifest(const char *directory, struct game_record *game)
{
    snprintf(game->config_path, sizeof(game->config_path), "games/%s/game.conf", directory);
    FILE *file = fopen(game->config_path, "r");
    if (file == NULL) return false;
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *entry = trim(line);
        if (*entry == '\0' || *entry == '#' || *entry == ';') continue;
        char *separator = strchr(entry, '=');
        if (separator == NULL) continue;
        *separator = '\0';
        char *key = trim(entry);
        char *value = trim(separator + 1);
        if (strcmp(key, "id") == 0) copy_text(game->id, sizeof(game->id), value);
        else if (strcmp(key, "name") == 0) copy_text(game->name, sizeof(game->name), value);
        else if (strcmp(key, "description") == 0)
            copy_text(game->description, sizeof(game->description), value);
        else if (strcmp(key, "module") == 0)
            copy_text(game->module_path, sizeof(game->module_path), value);
    }
    fclose(file);
    return game->id[0] && game->name[0] && game->module_path[0];
}

static int compare_games(const void *left, const void *right)
{
    const struct game_record *a=left,*b=right;
    int hardware_a=!strcmp(a->id,"hardware-test"),hardware_b=!strcmp(b->id,"hardware-test");
    if(hardware_a!=hardware_b)return hardware_a-hardware_b;
    return strcmp(a->name,b->name);
}

/* Discovery places the settings utility after all playable games. */
static int launcher_game_count(const struct host *host)
{
    int count=host->game_count;
    if(count && !strcmp(host->games[count-1].id,"hardware-test"))count--;
    return count;
}

static void load_launcher(struct host *host)
{
    host->launcher=(struct launcher_config){0};
    for(int i=0;i<launcher_game_count(host);i++)launcher_add(&host->launcher,host->games[i].id,host->games[i].name,i,false);
    launcher_add(&host->launcher,"settings","Settings",-1,false);
    launcher_add(&host->launcher,"power","Power Down",-2,false);
    launcher_add(&host->launcher,"input","Input Settings",-3,true);
    launcher_add(&host->launcher,"display","Display Area",-4,true);
    launcher_add(&host->launcher,"hardware","Hardware Test",-5,true);
    launcher_read(&host->launcher,"config/launcher.conf");
    host->selected_game=host->settings_option=0;
}
static void ensure_launcher(struct host *host) {
    if(!host->launcher.count)load_launcher(host);
    if(host->selected_game<0 || host->selected_game>=launcher_count(&host->launcher,false))host->selected_game=0;
    if(host->settings_option<0 || host->settings_option>launcher_count(&host->launcher,true))host->settings_option=0;
}

static void discover_games(struct host *host)
{
    DIR *games = opendir("games");
    if (games == NULL) return;
    struct dirent *entry;
    while (host->game_count < MAX_GAMES && (entry = readdir(games)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        struct game_record candidate = {0};
        if (load_manifest(entry->d_name, &candidate))
            host->games[host->game_count++] = candidate;
    }
    closedir(games);
    qsort(host->games,host->game_count,sizeof(host->games[0]),compare_games);
    printf("Discovered %d game(s).\n", host->game_count);
}

static bool load_game(struct host *host, int index);

static bool keyboard_binding_valid(struct controller_binding binding)
{
    return binding.kind==BINDING_NONE || (binding.kind==BINDING_KEY && binding.code<BTN_MISC &&
        binding.code!=KEY_F1 && binding.code!=KEY_F12);
}

static bool same_binding(struct controller_binding a, struct controller_binding b)
{
    return a.kind!=BINDING_NONE && a.kind==b.kind && a.code==b.code && a.direction==b.direction;
}

static void load_host_config(struct host *host)
{
    default_bindings(host->bindings);
    default_keyboard_bindings(host->keyboard_bindings);
    host->safe_x=16; host->safe_y=12;
    host->safe_offset_x=host->safe_offset_y=0;
    host->frame_timing_enabled=false;
    host->timing_start_held=host->timing_start_toggled=false;
    int input_version=0;
    struct controller_binding legacy[8]={0};
    bool supplied[2*CHIRKY_BUTTON_COUNT]={0}, old_supplied[8]={0};
    FILE *file=fopen(HOST_CONFIG_PATH,"r");
    if (!file) return;
    char line[512];
    while (fgets(line,sizeof(line),file)) {
        char *key=trim(line), *separator=strchr(key,'=');
        if (!separator || *key=='#' || *key==';') continue;
        *separator=0; key=trim(key); char *value=trim(separator+1);
        if (!strcmp(key,"boot_game")) copy_text(host->boot_game_id,sizeof(host->boot_game_id),value);
        else if (!strcmp(key,"safe_x")) { int n=atoi(value); if (n>=0 && n<=32) host->safe_x=n; }
        else if (!strcmp(key,"safe_y")) { int n=atoi(value); if (n>=0 && n<=24) host->safe_y=n; }
        else if (!strcmp(key,"safe_offset_x")) { int n=atoi(value); if(n>=-32 && n<=32)host->safe_offset_x=n; }
        else if (!strcmp(key,"safe_offset_y")) { int n=atoi(value); if(n>=-24 && n<=24)host->safe_offset_y=n; }
        else if (!strcmp(key,"input_version")) input_version=atoi(value);
        else {
            int button=binding_button_for_key(key), old=legacy_binding_for_key(key);
            struct controller_binding parsed;
            if (!parse_binding(value,&parsed)) continue;
            if (button>=0 && (button<CHIRKY_BUTTON_COUNT || keyboard_binding_valid(parsed))) {
                if (button<CHIRKY_BUTTON_COUNT) host->bindings[button]=parsed;
                else host->keyboard_bindings[button-CHIRKY_BUTTON_COUNT]=parsed;
                supplied[button]=true;
            } else if (old>=0 && (old<4 || keyboard_binding_valid(parsed))) {
                legacy[old]=parsed; old_supplied[old]=true;
            }
        }
    }
    fclose(file);
    if (input_version<3) {
        const int targets[]={CHIRKY_BUTTON_Y,CHIRKY_BUTTON_B,-1,CHIRKY_BUTTON_SELECT,
            CHIRKY_BUTTON_COUNT+CHIRKY_BUTTON_Y,CHIRKY_BUTTON_COUNT+CHIRKY_BUTTON_B,
            CHIRKY_BUTTON_COUNT+CHIRKY_BUTTON_START,CHIRKY_BUTTON_COUNT+CHIRKY_BUTTON_SELECT};
        for (int i=0;i<8;i++) {
            int target=targets[i];
            if (target<0 || !old_supplied[i] || supplied[target]) continue;
            if (target<CHIRKY_BUTTON_COUNT) host->bindings[target]=legacy[i];
            else host->keyboard_bindings[target-CHIRKY_BUTTON_COUNT]=legacy[i];
            supplied[target]=true;
        }
        /* A legacy Confirm has no separate game button. Dash owns B; if Dash
           was absent, retain custom Confirm as B except the obsolete Start default. */
        if (!supplied[CHIRKY_BUTTON_B] && old_supplied[2] &&
            !(input_version<2 && legacy[2].kind==BINDING_KEY && legacy[2].code==BTN_TR2)) {
            host->bindings[CHIRKY_BUTTON_B]=legacy[2]; supplied[CHIRKY_BUTTON_B]=true;
        }
        /* New defaults must not turn a retained custom input into two buttons. */
        for (int source=0;source<2;source++) {
            struct controller_binding *map=source?host->keyboard_bindings:host->bindings;
            bool *explicit=supplied+source*CHIRKY_BUTTON_COUNT;
            for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) if (!explicit[i])
                for (int j=0;j<CHIRKY_BUTTON_COUNT;j++)
                    if (explicit[j] && same_binding(map[i],map[j])) map[i]=(struct controller_binding){0};
        }
    }
    clamp_safe_position(host);
}

static bool load_boot_game(struct host *host)
{
    if (!host->boot_game_id[0] || strcmp(host->boot_game_id, "launcher") == 0)
        return false;
    for (int index = 0; index < host->game_count; ++index) {
        if (strcmp(host->games[index].id, host->boot_game_id) == 0) {
            host->selected_game = index;
            return load_game(host, index);
        }
    }
    fprintf(stderr, "Configured boot game was not found: %s\n", host->boot_game_id);
    return false;
}

static void unload_game(struct host *host)
{
    host->pending_game=NULL;
    audio_mixer_reset(host->audio);
    for(size_t i=0;i<host->sound_pin_count;i++)asset_store_release(host->assets,host->sound_pins[i]);
    host->sound_pin_count=0;
    host->paused=false;host->pause_option=0;
    if (host->game_api != NULL) host->game_api->shutdown();
    host->game_api = NULL;
    host->active_game = NULL;
    if (host->game_library != NULL) dlclose(host->game_library);
    host->game_library = NULL;
    if(host->assets) {
        splash_free(&host->launcher_art);
        image_cache_clear(&host->images,&host->renderer);
        asset_store_clear(host->assets);
        splash_load_file_api(&host->launcher_art,&host->api,"assets/launcher/splash.ppm");
    }
    write_status(host);
}

static bool activate_game(struct host *host,struct game_record *game)
{
    chirky_scope(&host->api,"assets.module",true);
    host->game_library = dlopen(game->module_path, RTLD_NOW | RTLD_LOCAL);
    chirky_scope(&host->api,"assets.module",false);
    if (host->game_library == NULL) {
        fprintf(stderr, "Cannot load %s: %s\n", game->module_path, dlerror());
        return false;
    }
    dlerror();
    void *symbol = dlsym(host->game_library, "chirky_game_entry");
    chirky_game_entry_fn entry = NULL;
    memcpy(&entry, &symbol, sizeof(entry));
    const char *error = dlerror();
    if (error != NULL || entry == NULL) {
        fprintf(stderr, "Invalid game module %s: %s\n", game->module_path,
                error != NULL ? error : "entry point missing");
        unload_game(host);
        return false;
    }
    host->game_api = entry();
    chirky_scope(&host->api,"assets.game_init",true);
    bool initialized=host->game_api && host->game_api->abi_version==CHIRKY_ABI_VERSION &&
        host->game_api->init(&host->api,game->config_path);
    chirky_scope(&host->api,"assets.game_init",false);
    if (!initialized) {
        fprintf(stderr, "Game initialization failed: %s\n", game->id);
        unload_game(host);
        return false;
    }
    host->active_game = game;
    if(host->assets) {
        char device[128]="plughw:0,0",line[1024];
        struct chirky_file config=chirky_file_open(&host->api,game->config_path);
        while(config.stream && fgets(line,sizeof(line),config.stream)) {
            char *key=trim(line),*equals=strchr(key,'=');
            if(equals){*equals=0;if(!strcmp(trim(key),"sound_device"))copy_text(device,sizeof(device),trim(equals+1));}
        }
        chirky_file_close(&config);
        if(!host->audio || strcmp(device,host->audio_device) ||
            audio_mixer_get_info(host->audio).state==AUDIO_FAILED) {
            chirky_scope(&host->api,"audio.startup",true);
            audio_mixer_stop(host->audio);host->audio=audio_mixer_start(device);
            chirky_scope(&host->api,"audio.startup",false);
            copy_text(host->audio_device,sizeof(host->audio_device),device);
            host->audio_reported=false;
            if(!host->audio)fprintf(stderr,"Audio unavailable on %s\n",device);
        }
    }
    block_transition_input(host);
    printf("Started game: %s\n", game->name);
    write_status(host);
    return true;
}

static bool load_game(struct host *host,int index)
{
    if(index<0 || index>=host->game_count)return false;
    if(host->display_settings)restore_display_area(host);
    host->settings_menu=host->controller_settings=host->display_settings=host->setup.active=host->input_test=false;
    block_transition_input(host);host->controller_menu_chord_frames=0;
    unload_game(host);
    if(!host->assets)return activate_game(host,&host->games[index]);
    char directory[1024];copy_text(directory,sizeof(directory),host->games[index].config_path);
    char *slash=strrchr(directory,'/');if(!slash)return false;*slash=0;
    host->loading_started=monotonic_us();
    if(!asset_store_prefetch(host->assets,directory))return false;
    host->pending_game=&host->games[index];write_status(host);return true;
}

static void finish_loading(struct host *host)
{
    if(!host->pending_game)return;
    enum chirky_asset_state state=asset_store_prefetch_state(host->assets);
    if(state==CHIRKY_ASSET_LOADING)return;
    struct game_record *game=(struct game_record *)host->pending_game;host->pending_game=NULL;
    if(state!=CHIRKY_ASSET_READY) {
        fprintf(stderr,"Asset preparation failed: %s\n",game->id);unload_game(host);return;
    }
    struct asset_store_metrics metrics=asset_store_get_metrics(host->assets);
    uint64_t began=monotonic_us();
    chirky_scope(&host->api,"assets.activate",true);
    bool ok=activate_game(host,game);
    chirky_scope(&host->api,"assets.activate",false);
    printf("Assets: game=%s ready=%d load_wall=%.3fms worker_cpu=%.3fms bytes=%llu resident=%zu activate=%.3fms total=%.3fms\n",
        game->id,ok,metrics.wall_ms,metrics.worker_cpu_ms,(unsigned long long)metrics.bytes_read,
        metrics.bytes_resident,(monotonic_us()-began)/1000.0,(monotonic_us()-host->loading_started)/1000.0);
    fflush(stdout);
}

static void poll_audio(struct host *host)
{
    if(!host->audio)return;
    struct audio_mixer_info info=audio_mixer_get_info(host->audio);
    if(info.state==AUDIO_STARTING || (host->audio_reported && host->audio_state==info.state))return;
    host->audio_state=info.state;host->audio_reported=true;
    printf("Audio: device=%s state=%s worker_setup_wall=%.3fms worker_setup_cpu=%.3fms\n",
        host->audio_device,info.state==AUDIO_READY?"ready":"failed",
        info.startup_wall_us/1000.0,info.startup_cpu_us/1000.0);
    fflush(stdout);
}

static void open_settings_screen(struct host *host, int option)
{
    if(host->display_settings)restore_display_area(host);
    host->settings_menu=host->controller_settings=host->display_settings=host->setup.active=host->input_test=false;
    unload_game(host);
    host->settings_menu=true;host->settings_option=option;
    ensure_launcher(host);
    host->selected_game=0;
    for(int i=0;i<launcher_count(&host->launcher,false);i++)if(launcher_at(&host->launcher,false,i)->action==-1)host->selected_game=i;
    for(int i=0;i<launcher_count(&host->launcher,true);i++)if(launcher_at(&host->launcher,true,i)->action==-3-option)host->settings_option=i;
    host->settings_message="";host->controller_menu_chord_frames=0;
    if(option==0) { host->controller_settings=true;host->selected_option=0; }
    else if(option==1) {
        host->display_settings=true;host->display_option=0;
        host->saved_safe_x=host->safe_x;host->saved_safe_y=host->safe_y;
        host->saved_safe_offset_x=host->safe_offset_x;host->saved_safe_offset_y=host->safe_offset_y;
    } else {
        int hardware=launcher_game_count(host);
        if(hardware<host->game_count)load_game(host,hardware);
        host->settings_menu=true;
    }
    block_transition_input(host);
    write_status(host);
}

static void process_control(struct host *host)
{
    char command[256];
    ssize_t length = read(host->control_fd, command, sizeof(command) - 1);
    if (length <= 0) return;
    command[length] = '\0';
    char *line = trim(command);
    char *newline = strchr(line, '\n');
    if (newline != NULL) *newline = '\0';

    if (strcmp(line, "menu") == 0) {
        if (host->display_settings) {
            restore_display_area(host);
        }
        host->settings_menu=host->controller_settings=host->display_settings=host->setup.active=host->input_test=false;
        block_transition_input(host);
        unload_game(host);
    } else if (!strcmp(line,"launcher-reload")) {
        load_launcher(host);block_transition_input(host);
    } else if (!strcmp(line,"settings input")) {
        open_settings_screen(host,0);
    } else if (!strcmp(line,"settings display")) {
        open_settings_screen(host,1);
    } else if (!strcmp(line,"settings hardware")) {
        open_settings_screen(host,2);
    } else if (strcmp(line, "poweroff") == 0) {
        power_down_pi();
    } else if (strcmp(line, "snapshot") == 0) {
        snapshot_requested = 1;
    } else if (!strcmp(line,"timing on") || !strcmp(line,"timing off")) {
        host->frame_timing_enabled=!strcmp(line,"timing on");
    } else if (!strncmp(line,"capture ",8)) {
        char *end;unsigned long frames=strtoul(line+8,&end,10);
        const char *id=*end==' '?end+1:end;
        bool valid_id=(!*end || *end==' ') && strlen(id)<=32 && strspn(id,"0123456789abcdef")==strlen(id);
        if(end!=line+8 && valid_id && frames>=1 && frames<=1800 &&
           trace_arm(&host->trace,(unsigned)frames,frame_budget_us(host))) {
            snprintf(host->trace.request_id,sizeof(host->trace.request_id),"%s",id);
            host->trace.gpu_mode=host->gpu_timing.supported?1:2;
            remove("run/profile.json");
            write_status(host);
            printf("Frame capture armed: %lu frames\n",frames);
        } else fprintf(stderr,"Capture rejected: use 1..1800 frames, one capture at a time\n");
    } else if (strcmp(line, "quit") == 0) {
        host->running = false;
    } else if (strcmp(line, "reload") == 0 && host->active_game != NULL) {
        int active = (int)(host->active_game - host->games);
        load_game(host, active);
    } else if (strncmp(line, "launch ", 7) == 0) {
        const char *id = trim(line + 7);
        for (int index = 0; index < host->game_count; ++index) {
            if (strcmp(host->games[index].id, id) == 0) {
                host->selected_game = index;
                load_game(host, index);
                break;
            }
        }
    }
}

#include "pixel_font.h"

static void draw_text(void *context, int x, int y, const char *text,
                      int scale, unsigned char red, unsigned char green,
                      unsigned char blue)
{
    struct host *host = context;
    int cursor = x;
    for (const char *character = text; *character; ++character) {
        const uint8_t *rows = glyph(*character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1u << (4 - column))) != 0)
                    fill_rect(host, cursor + column * scale, y - row * scale,
                              scale, scale, red, green, blue);
            }
        }
        cursor += 6 * scale;
    }
}

static void clear_screen(void)
{
    glDisable(GL_SCISSOR_TEST); glClearColor(.012f,.019f,.032f,1); glClear(GL_COLOR_BUFFER_BIT);
}

static void menu_text(struct host *host, int x, int y, const char *value, int scale,
                      unsigned char r, unsigned char g, unsigned char b)
{
    char clipped[128];
    int columns=(host->api.screen_width-x-8)/(6*scale);
    if (columns<0) columns=0;
    if (columns>127) columns=127;
    snprintf(clipped,sizeof(clipped),"%.*s",columns,value);
    draw_text(host,x,y,clipped,scale,r,g,b);
}

static void menu_row(struct host *host, int y, const char *label, bool selected)
{
    fill_rect(host,8,y-13,host->api.screen_width-16,20,selected?28:14,selected?74:30,selected?84:40);
    fill_rect(host,12,y-9,3,10,selected?244:70,selected?194:110,70);
    menu_text(host,22,y,label,1,selected?250:170,selected?248:185,selected?236:190);
}

static void menu_footer(struct host *host, bool can_go_back)
{
    char confirm[32],back[32],line[80]; button_label(host,CHIRKY_BUTTON_B,confirm,sizeof(confirm));
    button_label(host,CHIRKY_BUTTON_A,back,sizeof(back));
    if (can_go_back) snprintf(line,sizeof(line),"%s SELECT - %s BACK",confirm,back);
    else snprintf(line,sizeof(line),"%s SELECT - UP DOWN MOVE",confirm);
    menu_text(host,10,14,line,1,112,160,170);
}

static void draw_launcher(struct host *host)
{
    clear_screen();
    splash_draw(&host->launcher_art,&host->api);
    int height=host->api.screen_height;
    struct chirky_host_api title_api=host->api;
    title_api.context=host;
    title_api.fill_rect=fill_rect;
    launcher_wordmark(&title_api);
    ensure_launcher(host);
    int count=launcher_count(&host->launcher,false), first=host->selected_game<4?0:host->selected_game-3;
    for (int row=0;row<4 && first+row<count;row++) {
        int index=first+row;
        const char *label=launcher_at(&host->launcher,false,index)->label;
        int y=height-83-row*24;
        bool selected=index==host->selected_game;
        int width=host->api.screen_width*2/3;
        fill_rect(host,8,y-13,width,20,selected?28:5,selected?74:17,selected?84:23);
        fill_rect(host,12,y-9,3,10,selected?244:40,selected?194:85,selected?70:91);
        char visible[64];
        snprintf(visible,sizeof(visible),"%.*s",(width-20)/6,label);
        menu_text(host,22,y,visible,1,selected?250:170,selected?248:185,selected?236:190);
    }
    if (first+4<count) menu_text(host,10,30,"MORE BELOW",1,112,160,170);
    else if (first>0) menu_text(host,10,30,"MORE ABOVE",1,112,160,170);
    fill_rect(host,8,5,host->api.screen_width-16,18,5,17,23);
    menu_footer(host,false);
}

static void draw_settings_menu(struct host *host)
{
    clear_screen();
    int height=host->api.screen_height;
    fill_rect(host,8,height-7,host->api.screen_width-16,3,40,175,212);
    menu_text(host,10,height-23,"SETTINGS",3,238,240,232);
    ensure_launcher(host);
    for(int i=0;i<launcher_count(&host->launcher,true);i++)menu_row(host,height-83-i*24,launcher_at(&host->launcher,true,i)->label,i==host->settings_option);
    int back_index=launcher_count(&host->launcher,true);
    menu_row(host,height-83-back_index*24,"BACK",host->settings_option==back_index);
    menu_footer(host,true);
}

static bool binding_down(const struct input_set *inputs, const struct controller_binding *binding, bool keyboard);
static int axis_direction(const struct input_device *device, unsigned int code, int value);
static bool capture_axis(unsigned int code);

static void append_input_name(char *line, size_t capacity, const char *name)
{
    size_t used=strlen(line);
    if (strstr(line," MORE")) return;
    if (used+strlen(name)+6>=capacity) {
        if (used+6<capacity) snprintf(line+used,capacity-used," MORE");
        return;
    }
    snprintf(line+used,capacity-used," %s",name);
}

/* Poll the device states, including unmapped inputs, without consuming events. */
static void held_input_names(const struct host *host, bool keyboard, char *line, size_t capacity)
{
    copy_text(line,capacity,keyboard?"KEY":"PAD");
    bool any=false;
    for (unsigned int code=0;code<=KEY_MAX;code++) {
        bool held=false;
        for (int i=0;i<host->inputs.count;i++) {
            const struct input_device *d=&host->inputs.devices[i];
            if (d->controller!=keyboard && d->keys[code]) held=true;
        }
        if (!held) continue;
        char name[32]; any=true;
        if (keyboard) keyboard_name(&(struct controller_binding){BINDING_KEY,code,0},name,sizeof(name));
        else snprintf(name,sizeof(name),"%u",code);
        append_input_name(line,capacity,name);
    }
    if (!keyboard) for (unsigned int code=0;code<=ABS_MAX;code++) {
        if (!capture_axis(code)) continue;
        for (int direction=-1;direction<=1;direction+=2) {
            bool held=false;
            for (int i=0;i<host->inputs.count;i++) {
                const struct input_device *d=&host->inputs.devices[i];
                if (d->controller && axis_direction(d,code,d->abs_values[code])==direction) held=true;
            }
            if (held) {
                char name[24]; snprintf(name,sizeof(name),"AX%u%s",code,direction<0?"NEG":"POS");
                append_input_name(line,capacity,name); any=true;
            }
        }
    }
    if (!any) append_input_name(line,capacity,"NONE");
}

static void draw_live_inputs(struct host *host)
{
    int cell=(host->api.screen_width-20)/6;
    menu_text(host,10,87,"PAD GREEN / KEY GOLD",1,155,175,180);
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        bool pad=binding_down(&host->inputs,&host->bindings[i],false);
        bool key=binding_down(&host->inputs,&host->keyboard_bindings[i],true);
        int x=10+(i%6)*cell, y=61-(i/6)*21;
        fill_rect(host,x,y,cell-2,18,pad||key?45:22,pad||key?65:32,pad||key?58:38);
        menu_text(host,x+2,y+14,button_names[i],1,pad||key?250:130,pad||key?245:150,pad||key?220:157);
        fill_rect(host,x+2,y+2,(cell-6)/2,3,pad?93:40,pad?220:60,pad?153:60);
        fill_rect(host,x+cell/2,y+2,(cell-6)/2,3,key?250:60,key?196:55,key?75:40);
    }
    char line[96]; size_t capacity=(size_t)(host->api.screen_width-20)/6+1;
    if (capacity>sizeof(line)) capacity=sizeof(line);
    held_input_names(host,false,line,capacity); menu_text(host,10,38,line,1,93,220,153);
    held_input_names(host,true,line,capacity); menu_text(host,10,26,line,1,250,196,75);
}

static void draw_controller_settings(struct host *host)
{
    clear_screen();
    int height=host->api.screen_height;
    if (host->setup.active) {
        struct binding_setup *setup=&host->setup;
        menu_text(host,10,height-18,setup->keyboard?"MAP KEYBOARD":"MAP CONTROLLER",2,238,240,232);
        int step=setup->step<CHIRKY_BUTTON_COUNT?setup->step:CHIRKY_BUTTON_COUNT-1;
        menu_text(host,10,height-47,button_names[step],3,244,194,70);
        char line[80];
        snprintf(line,sizeof(line),"%d OF %d - %s",step+1,CHIRKY_BUTTON_COUNT,
            setup->wait_release?"RELEASE ALL INPUTS":"PRESS NOW");
        menu_text(host,10,height-74,line,1,112,180,190);
        menu_text(host,10,99,setup->message,1,244,160,70);
        menu_text(host,10,12,setup->keyboard?"F1 CANCEL - SAVES AFTER ALL 12":"HOLD TWO BUTTONS TO CANCEL",1,112,160,170);
    } else if (host->input_test) {
        menu_text(host,10,height-22,"TEST BUTTONS",2,238,240,232);
        menu_text(host,10,height-49,"PRESS ANY KEYS OR BUTTONS",1,112,180,190);
        menu_text(host,10,height-64,"BOTH SOURCES LIGHT UP BELOW",1,112,180,190);
        menu_text(host,10,12,"HOLD A 1 SECOND TO RETURN",1,112,160,170);
    } else {
        menu_text(host,10,height-19,"INPUT SETTINGS",2,238,240,232);
        menu_text(host,10,height-35,host->settings_message?host->settings_message:"",1,244,194,70);
        const char *labels[]={"MAP SNES CONTROLLER","MAP KEYBOARD TO SNES","TEST BUTTONS","BACK"};
        for (int i=0;i<4;i++) menu_row(host,height-48-i*16,labels[i],host->selected_option==i);
        menu_text(host,10,12,"B SELECT - A BACK",1,112,160,170);
    }
    draw_live_inputs(host);
}

static void draw_display_settings(struct host *host)
{
    clear_screen();
    int width=host->api.screen_width,height=host->api.screen_height;
    fill_rect(host,0,0,width,1,40,175,212); fill_rect(host,0,height-1,width,1,40,175,212);
    fill_rect(host,0,0,1,height,40,175,212); fill_rect(host,width-1,0,1,height,40,175,212);
    menu_text(host,10,height-20,"DISPLAY AREA",2,238,240,232);
    menu_text(host,10,height-47,"KEEP ALL FOUR EDGES VISIBLE",1,112,160,170);
    char line[64];
    snprintf(line,sizeof(line),"SIDE MARGIN - %d",host->safe_x); menu_row(host,height-70,line,host->display_option==0);
    snprintf(line,sizeof(line),"TOP BOTTOM MARGIN - %d",host->safe_y); menu_row(host,height-86,line,host->display_option==1);
    snprintf(line,sizeof(line),"HORIZONTAL - %d",host->safe_offset_x); menu_row(host,height-102,line,host->display_option==2);
    snprintf(line,sizeof(line),"VERTICAL - %d",host->safe_offset_y); menu_row(host,height-118,line,host->display_option==3);
    menu_row(host,height-134,"SAVE",host->display_option==4);
    menu_row(host,height-150,"BACK",host->display_option==5);
    menu_text(host,10,25,host->settings_message && *host->settings_message?host->settings_message:"LEFT RIGHT ADJUST - UP DOWN MOVE",1,112,160,170);
    menu_footer(host,true);
}

static void save_snapshot(struct host *host)
{
    int width = host->mode.hdisplay;
    int height = host->mode.vdisplay;
    size_t row_bytes = (size_t)width * 3u;
    GLubyte *pixels = malloc(row_bytes * (size_t)height);
    if (pixels == NULL) return;
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    if (mkdir("snapshots", 0755) != 0 && errno != EEXIST) { free(pixels); return; }
    time_t now = time(NULL);
    struct tm timestamp;
    localtime_r(&now, &timestamp);
    char path[512];
    snprintf(path, sizeof(path), "snapshots/chirky-%04d%02d%02d-%02d%02d%02d-%03u.ppm",
             timestamp.tm_year + 1900, timestamp.tm_mon + 1, timestamp.tm_mday,
             timestamp.tm_hour, timestamp.tm_min, timestamp.tm_sec,
             host->snapshot_sequence++);
    FILE *file = fopen(path, "wb");
    if (file != NULL) {
        fprintf(file, "P6\n%d %d\n255\n", width, height);
        for (int row = height - 1; row >= 0; --row)
            fwrite(pixels + (size_t)row * row_bytes, 1, row_bytes, file);
        fclose(file);
        printf("Snapshot: %s\n", path);
    }
    free(pixels);
}

static void destroy_framebuffer(struct gbm_bo *bo, void *data)
{
    (void)bo;
    struct framebuffer *framebuffer = data;
    if (framebuffer != NULL) {
        if (framebuffer->fb_id) drmModeRmFB(framebuffer->drm_fd, framebuffer->fb_id);
        free(framebuffer);
    }
}

static uint32_t framebuffer_for_bo(struct host *host, struct gbm_bo *bo)
{
    struct framebuffer *framebuffer = gbm_bo_get_user_data(bo);
    if (framebuffer != NULL) return framebuffer->fb_id;
    framebuffer = calloc(1, sizeof(*framebuffer));
    if (framebuffer == NULL) return 0;
    framebuffer->drm_fd = host->drm_fd;
    uint32_t handles[4] = {gbm_bo_get_handle(bo).u32, 0, 0, 0};
    uint32_t pitches[4] = {gbm_bo_get_stride(bo), 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    if (drmModeAddFB2(host->drm_fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                      DRM_FORMAT_XRGB8888, handles, pitches, offsets,
                      &framebuffer->fb_id, 0) != 0) {
        free(framebuffer);
        return 0;
    }
    gbm_bo_set_user_data(bo, framebuffer, destroy_framebuffer);
    return framebuffer->fb_id;
}

static bool choose_display(struct host *host)
{
    drmModeRes *resources = drmModeGetResources(host->drm_fd);
    if (resources == NULL) return false;
    drmModeConnector *connector = NULL;
    for (int index = 0; index < resources->count_connectors; ++index) {
        drmModeConnector *candidate = drmModeGetConnector(host->drm_fd,
                                                           resources->connectors[index]);
        if (candidate != NULL && candidate->connection == DRM_MODE_CONNECTED &&
            candidate->count_modes > 0) { connector = candidate; break; }
        drmModeFreeConnector(candidate);
    }
    if (connector == NULL) { drmModeFreeResources(resources); return false; }
    drmModeEncoder *encoder = connector->encoder_id ?
        drmModeGetEncoder(host->drm_fd, connector->encoder_id) : NULL;
    if (encoder == NULL) {
        for (int index = 0; index < connector->count_encoders; ++index) {
            encoder = drmModeGetEncoder(host->drm_fd, connector->encoders[index]);
            if (encoder != NULL) break;
        }
    }
    uint32_t crtc_id = encoder != NULL ? encoder->crtc_id : 0;
    if (!crtc_id && encoder != NULL) {
        for (int index = 0; index < resources->count_crtcs; ++index) {
            if (encoder->possible_crtcs & (1u << index)) {
                crtc_id = resources->crtcs[index]; break;
            }
        }
    }
    if (!crtc_id) {
        drmModeFreeEncoder(encoder); drmModeFreeConnector(connector);
        drmModeFreeResources(resources); return false;
    }
    host->connector_id = connector->connector_id;
    host->crtc_id = crtc_id;
    host->mode = connector->modes[0];
    host->saved_crtc = drmModeGetCrtc(host->drm_fd, crtc_id);
    printf("DRM mode: %s %ux%u @ %u Hz\n", host->mode.name,
           host->mode.hdisplay, host->mode.vdisplay, host->mode.vrefresh);
    drmModeFreeEncoder(encoder); drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    return true;
}

static bool init_graphics(struct host *host)
{
    host->gbm = gbm_create_device(host->drm_fd);
    if (host->gbm == NULL) return false;
    host->gbm_surface = gbm_surface_create(host->gbm, host->mode.hdisplay,
        host->mode.vdisplay, DRM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (host->gbm_surface == NULL) return false;
    host->egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, host->gbm, NULL);
    if (host->egl_display == EGL_NO_DISPLAY)
        host->egl_display = eglGetDisplay((EGLNativeDisplayType)host->gbm);
    EGLint major = 0, minor = 0;
    if (host->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(host->egl_display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API)) return false;
    const EGLint attributes[] = {EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,
        EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,0,EGL_NONE};
    EGLConfig config = NULL;
    EGLint count = 0;
    if (!eglChooseConfig(host->egl_display, attributes, &config, 1, &count) || count != 1)
        return false;
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    host->egl_context = eglCreateContext(host->egl_display, config, EGL_NO_CONTEXT,
                                          context_attributes);
    host->egl_surface = eglCreateWindowSurface(host->egl_display, config,
        (EGLNativeWindowType)host->gbm_surface, NULL);
    if (host->egl_context == EGL_NO_CONTEXT || host->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(host->egl_display, host->egl_surface, host->egl_surface,
                        host->egl_context)) return false;
    eglSwapInterval(host->egl_display, 0);
    glViewport(0, 0, host->mode.hdisplay, host->mode.vdisplay);
    printf("EGL version: %d.%d\n", major, minor);
    gpu_timing_init(&host->gpu_timing,(const char *)glGetString(0x1f03),eglGetProcAddress);
    printf("GPU profiling: %s\n",host->gpu_timing.supported?"elapsed timer queries":"kernel trace collector");
    return rect_renderer_init(&host->renderer,host->mode.hdisplay,host->mode.vdisplay);
}

static bool input_bit(const unsigned char *bits, unsigned int code)
{
    return (bits[code / 8u] & (1u << (code % 8u))) != 0;
}

static bool controller_button_code(unsigned int code)
{
    return (code >= BTN_MISC && code <= BTN_9) ||
           (code >= BTN_JOYSTICK && code < BTN_DIGI) ||
           (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT) ||
           (code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY40);
}

static bool controller_direction_code(unsigned int code)
{
    return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT ||
           code == KEY_RIGHT ||
           (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT);
}

static int axis_direction(const struct input_device *device, unsigned int code,
                          int value)
{
    int minimum = device->abs_minimums[code];
    int maximum = device->abs_maximums[code];
    int centre = minimum + (maximum - minimum) / 2;
    int threshold = (maximum - minimum) / 4;
    if (device->abs_flats[code] > threshold) threshold = device->abs_flats[code];
    if (value < centre - threshold) return -1;
    if (value > centre + threshold) return 1;
    return 0;
}

static void open_inputs(struct input_set *inputs)
{
    memset(inputs, 0, sizeof(*inputs));
    for (int index = 0; index < MAX_INPUTS; ++index) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128] = "unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        unsigned char key_bits[(KEY_MAX + 8) / 8] = {0};
        unsigned char abs_bits[(ABS_MAX + 8) / 8] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

        struct input_device *device = &inputs->devices[inputs->count];
        device->fd = fd;
        copy_text(device->name, sizeof(device->name), name);
        for (unsigned int code = 0; code <= KEY_MAX; ++code) {
            if (input_bit(key_bits, code) && controller_button_code(code))
                device->controller = true;
        }
        for (unsigned int code = 0; code <= ABS_MAX; ++code) {
            if (!input_bit(abs_bits, code)) continue;
            struct input_absinfo info;
            if (ioctl(fd, EVIOCGABS(code), &info) == 0) {
                device->abs_values[code] = info.value;
                device->abs_minimums[code] = info.minimum;
                device->abs_maximums[code] = info.maximum;
                device->abs_flats[code] = info.flat;
            }
            if (code >= ABS_HAT0X && code <= ABS_HAT3Y)
                device->controller = true;
        }
        if (strcasestr(name, "gamepad") != NULL ||
            strcasestr(name, "controller") != NULL ||
            strcasestr(name, "joystick") != NULL ||
            strcasestr(name, "stick") != NULL ||
            strcasestr(name, "gp2040") != NULL)
            device->controller = true;
        ++inputs->count;
        printf("Input: %s (%s)%s\n", path, name,
               device->controller ? " [controller]" : "");
    }
}

static bool capture_axis(unsigned int code)
{
    return code==ABS_X || code==ABS_Y || code==ABS_RX || code==ABS_RY ||
        (code>=ABS_HAT0X && code<=ABS_HAT3Y);
}

static bool buttons_released(const struct input_set *inputs, bool keyboard)
{
    for (int i=0;i<inputs->count;i++) {
        const struct input_device *device=&inputs->devices[i];
        if (device->controller==keyboard) continue;
        for (unsigned int code=0;code<=KEY_MAX;code++) if (device->keys[code]) return false;
        if (!keyboard) for (unsigned int code=0;code<=ABS_MAX;code++)
            if (capture_axis(code) && axis_direction(device,code,device->abs_values[code])) return false;
    }
    return true;
}

static int controller_buttons_down(const struct input_set *inputs)
{
    int count=0;
    for (int i=0;i<inputs->count;i++) {
        const struct input_device *device=&inputs->devices[i];
        if (!device->controller) continue;
        for (unsigned int code=0;code<=KEY_MAX;code++)
            if (device->keys[code] && !controller_direction_code(code)) count++;
    }
    return count;
}

static bool binding_down(const struct input_set *inputs, const struct controller_binding *binding, bool keyboard)
{
    for (int i=0;i<inputs->count;i++) {
        const struct input_device *device=&inputs->devices[i];
        if (device->controller==keyboard) continue;
        if (binding->kind==BINDING_KEY && binding->code<=KEY_MAX && device->keys[binding->code]) return true;
        if (!keyboard && binding->kind==BINDING_ABS && binding->code<=ABS_MAX &&
            axis_direction(device,binding->code,device->abs_values[binding->code])==binding->direction) return true;
    }
    return false;
}

static bool button_down(const struct host *host, int action)
{
    return binding_down(&host->inputs,&host->bindings[action],false) ||
        binding_down(&host->inputs,&host->keyboard_bindings[action],true);
}

static void update_controller_buttons(struct host *host)
{
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        bool down=button_down(host,i);
        host->inputs.state.buttons[i]=down;
        host->inputs.state.button_pressed[i]=host->inputs.pending_buttons[i] || (down && !host->inputs.previous_buttons[i]);
        host->inputs.previous_buttons[i]=down;
        host->inputs.pending_buttons[i]=false;
    }
}

static void process_input_event(struct host *host, struct input_device *device, const struct input_event *event)
{
    /* Linux autorepeat is not a physical press or release. */
    if(event->type==EV_KEY && event->value==2)return;
    bool before[CHIRKY_BUTTON_COUNT];
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++) before[i]=button_down(host,i);
    if (event->type==EV_KEY && event->code<=KEY_MAX) {
        bool pressed=event->value==1 && !device->keys[event->code];
        device->keys[event->code]=event->value!=0;
        if (!device->controller) {
            if (pressed) host->inputs.state.pressed[event->code]=true;
            bool down=false;
            for (int i=0;i<host->inputs.count;i++) if (!host->inputs.devices[i].controller)
                down |= host->inputs.devices[i].keys[event->code];
            host->inputs.state.keys[event->code]=down;
        }
        if (pressed) {
            host->last_keyboard=!device->controller;
            if (device->controller) {
                if (event->code==KEY_UP || event->code==BTN_DPAD_UP) host->inputs.controller_up_pressed=true;
                if (event->code==KEY_DOWN || event->code==BTN_DPAD_DOWN) host->inputs.controller_down_pressed=true;
                if (!controller_direction_code(event->code)) host->inputs.state.controller_pressed=true;
            }
            if (!host->ui_wait_release && host->setup.keyboard!=device->controller)
                setup_offer(&host->setup,(struct controller_binding){BINDING_KEY,event->code,0});
        }
    } else if (event->type==EV_ABS && event->code<=ABS_MAX) {
        int old=axis_direction(device,event->code,device->abs_values[event->code]);
        device->abs_values[event->code]=event->value;
        int direction=axis_direction(device,event->code,event->value);
        if (device->controller && old==0 && direction!=0) {
            host->last_keyboard=false;
            if (event->code==ABS_Y || event->code==ABS_HAT0Y) {
                if (direction<0) host->inputs.controller_up_pressed=true;
                else host->inputs.controller_down_pressed=true;
            }
            if (!host->ui_wait_release && !host->setup.keyboard && capture_axis(event->code))
                setup_offer(&host->setup,(struct controller_binding){BINDING_ABS,event->code,direction});
        }
    }
    for (int i=0;i<CHIRKY_BUTTON_COUNT;i++)
        if (!before[i] && button_down(host,i)) host->inputs.pending_buttons[i]=true;
    /* A release followed by a new press may both arrive between frames. */
    if(before[CHIRKY_BUTTON_START] && !button_down(host,CHIRKY_BUTTON_START))
        host->timing_start_held=host->timing_start_toggled=false;
}

static void process_input(struct host *host, int fd)
{
    struct input_device *device=NULL;
    for (int i=0;i<host->inputs.count;i++) if (host->inputs.devices[i].fd==fd) device=&host->inputs.devices[i];
    if (!device) return;
    struct input_event events[32]; ssize_t bytes;
    while ((bytes=read(fd,events,sizeof(events)))>0)
        for (size_t i=0;i<(size_t)bytes/sizeof(events[0]);i++) process_input_event(host,device,&events[i]);
}

static bool menu_confirmed(const struct chirky_input *input)
{
    return input->button_pressed[CHIRKY_BUTTON_B];
}

static int menu_direction(const struct host *host)
{
    const struct chirky_input *input=&host->inputs.state;
    bool up=input->button_pressed[CHIRKY_BUTTON_UP];
    bool down=input->button_pressed[CHIRKY_BUTTON_DOWN];
    return (int)down-(int)up;
}

static bool recovery_chord(const struct input_set *inputs)
{
    for (int i=0;i<inputs->count;i++) {
        const struct input_device *d=&inputs->devices[i];
        if (d->controller && ((d->keys[BTN_START] && d->keys[BTN_SELECT]) || (d->keys[BTN_TR2] && d->keys[BTN_TL2]))) return true;
    }
    return false;
}

static void update_setup(struct host *host)
{
    if (controller_buttons_down(&host->inputs)>=2) host->controller_menu_chord_frames++;
    else host->controller_menu_chord_frames=0;
    if (host->inputs.state.pressed[KEY_F1] || host->controller_menu_chord_frames>=60) {
        host->setup.active=false; host->ui_wait_release=true;
        host->settings_message="CANCELLED - NOTHING CHANGED";
        host->controller_menu_chord_frames=0; return;
    }
    setup_release(&host->setup,buttons_released(&host->inputs,host->setup.keyboard));
    if (host->setup.complete) {
        struct controller_binding *target=host->setup.keyboard?host->keyboard_bindings:host->bindings;
        struct controller_binding original[CHIRKY_BUTTON_COUNT];
        memcpy(original,target,sizeof(original)); memcpy(target,host->setup.pending,sizeof(original));
        if (save_bindings(host)) host->settings_message="BUTTONS SAVED";
        else { memcpy(target,original,sizeof(original)); host->settings_message="SAVE FAILED - NOTHING CHANGED"; }
        host->setup.active=false; host->ui_wait_release=true;
    }
}

/* A host-wide gesture, independent of screen transitions and gameplay. Use
   elapsed time so a slow frame cannot turn two seconds into a longer hold. */
static void update_timing_toggle(struct host *host,uint64_t now_us)
{
    if(!host->inputs.state.buttons[CHIRKY_BUTTON_START]) {
        host->timing_start_held=host->timing_start_toggled=false;
        return;
    }
    if(!host->timing_start_held) {
        host->timing_start_held=true;host->timing_start_us=now_us;
    }
    if(!host->timing_start_toggled && now_us-host->timing_start_us>=2000000u) {
        host->frame_timing_enabled=!host->frame_timing_enabled;
        host->timing_start_toggled=true;
    }
}

static void update_host(struct host *host)
{
    finish_loading(host);
    poll_audio(host);
    if(host->pending_game) {
        if(host->inputs.state.pressed[KEY_F1])unload_game(host);
        memset(host->inputs.state.button_pressed,0,sizeof(host->inputs.state.button_pressed));
        memset(host->inputs.state.pressed,0,sizeof(host->inputs.state.pressed));
        return;
    }
    ensure_launcher(host);
    enum host_screen previous_screen=current_screen(host);
    struct chirky_input *input=&host->inputs.state;
    update_controller_buttons(host);
    update_timing_toggle(host,monotonic_us());
    if (!host->setup.active && input->pressed[KEY_F12]) snapshot_requested=1;
    int direction=menu_direction(host);
    bool confirm=menu_confirmed(input);
    bool back=input->pressed[KEY_F1] || input->button_pressed[CHIRKY_BUTTON_A];
    if (host->ui_wait_release && !input->pressed[KEY_F1]) {
        bool neutral=buttons_released(&host->inputs,false) && buttons_released(&host->inputs,true);
        for(int i=0;i<CHIRKY_BUTTON_COUNT;i++)neutral &= !input->button_pressed[i];
        chirky_gate_accept(&host->transition_gate,neutral);
        host->ui_wait_release=host->transition_gate.blocked;
    } else if (host->setup.active) update_setup(host);
    else if (host->active_game) {
        if (recovery_chord(&host->inputs)) host->controller_menu_chord_frames++;
        else host->controller_menu_chord_frames=0;
        bool hardware=!strcmp(host->active_game->id,"hardware-test");
        if (input->pressed[KEY_F1] || host->controller_menu_chord_frames>=60 || (hardware && back)) {
            unload_game(host); host->controller_settings=false;
            host->controller_menu_chord_frames=0; host->ui_wait_release=true;
        } else if(host->paused) {
            if(direction)host->pause_option=(host->pause_option+direction+2)%2;
            else if(back || input->button_pressed[CHIRKY_BUTTON_SELECT])host->paused=false;
            else if(confirm) {
                if(host->pause_option==0)host->paused=false;
                else {host->settings_menu=false;unload_game(host);}
            }
        } else if(!hardware && input->button_pressed[CHIRKY_BUTTON_SELECT]) {
            host->paused=true;host->pause_option=0;
        } else host->game_api->update(input);
    } else if (host->display_settings) {
        if (direction) host->display_option=(host->display_option+direction+6)%6;
        int delta=(int)input->button_pressed[CHIRKY_BUTTON_RIGHT]-
                  (int)input->button_pressed[CHIRKY_BUTTON_LEFT];
        int *values[]={&host->safe_x,&host->safe_y,&host->safe_offset_x,&host->safe_offset_y};
        int maximums[]={32,24,host->safe_x,host->safe_y};
        if (host->display_option<4 && delta) {
            int *value=values[host->display_option],maximum=maximums[host->display_option];
            int minimum=host->display_option<2?0:-maximum;
            *value+=delta;if(*value<minimum)*value=minimum;if(*value>maximum)*value=maximum;
            update_safe_area(host);
        }
        if (back || (!direction && confirm && host->display_option==5)) {
            restore_display_area(host);host->display_settings=false;
            write_status(host);
        } else if (!direction && confirm && host->display_option==4) {
            if (save_bindings(host)) { host->display_settings=false; write_status(host); }
            else host->settings_message="SAVE FAILED - TRY AGAIN";
        }
    } else if (host->controller_settings && host->input_test) {
        if (input->buttons[CHIRKY_BUTTON_A]) host->controller_menu_chord_frames++;
        else host->controller_menu_chord_frames=0;
        if (input->pressed[KEY_F1] || host->controller_menu_chord_frames>=60) {
            host->input_test=false; host->controller_menu_chord_frames=0; host->ui_wait_release=true;
        }
    } else if (host->controller_settings) {
        if (direction) host->selected_option=(host->selected_option+direction+4)%4;
        else if (back) host->controller_settings=false;
        else if (confirm) {
            if (host->selected_option==3) host->controller_settings=false;
            else if (host->selected_option==2) { host->input_test=true; host->controller_menu_chord_frames=0; }
            else { setup_begin(&host->setup,host->selected_option==1); host->settings_message=""; }
        }
    } else if (host->settings_menu) {
        int count=launcher_count(&host->launcher,true)+1;
        if(direction)host->settings_option=(host->settings_option+direction+count)%count;
        else if(back || (confirm && host->settings_option==count-1))host->settings_menu=false;
        else if(confirm)open_settings_screen(host,-3-launcher_at(&host->launcher,true,host->settings_option)->action);
    } else {
        int count=launcher_count(&host->launcher,false);
        if(direction)host->selected_game=(host->selected_game+direction+count)%count;
        else if(confirm) {
            int action=launcher_at(&host->launcher,false,host->selected_game)->action;
            if(action>=0)load_game(host,action);
            else if(action==-1){host->settings_menu=true;host->settings_option=0;}
            else power_down_pi();
        }
    }
    if(current_screen(host)!=previous_screen) { block_transition_input(host);write_status(host); }
    memset(input->button_pressed,0,sizeof(input->button_pressed));
    memset(input->pressed,0,sizeof(input->pressed));
    input->controller_pressed=false; host->inputs.controller_up_pressed=false; host->inputs.controller_down_pressed=false;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_nsec/1000u;
}

static unsigned int frame_budget_us(const struct host *host)
{
    if (host->mode.clock && host->mode.htotal && host->mode.vtotal)
        return (unsigned int)((uint64_t)host->mode.htotal*host->mode.vtotal*1000u/host->mode.clock);
    return 1000000u/(host->mode.vrefresh?host->mode.vrefresh:60);
}

/* Two twelve-pixel rows: text above a two-pixel timing bar. */
static void timing_bar(struct host *host,int y,const char *name,struct profile_stats stats,bool gpu)
{
    unsigned budget=frame_budget_us(host),scale=budget*2;
    int width=host->api.screen_width-6;
    char line[64];
    uint32_t average=stats.count?(uint32_t)(stats.sum/stats.count):0;
    if(stats.count)snprintf(line,sizeof(line),"%s %u.%u A%u.%u M%u.%u",name,
        stats.latest/1000,(stats.latest%1000)/100,average/1000,(average%1000)/100,
        stats.maximum/1000,(stats.maximum%1000)/100);
    else snprintf(line,sizeof(line),"%s -- A-- M--",name);
    menu_text(host,3,y+10,line,1,225,231,219);
    fill_rect(host,3,y,width,2,29,42,52);
    if(stats.count) {
        unsigned value=stats.latest>scale?scale:stats.latest;
        bool over=stats.latest>budget;
        int length=(int)((uint64_t)value*width/scale);
        if(value && !length)length=1;
        fill_rect(host,3,y,length,2,over?241:gpu?80:91,
                  over?82:gpu?174:212,over?92:gpu?235:95);
        unsigned top=stats.maximum>scale?scale:stats.maximum;
        fill_rect(host,3+(int)((uint64_t)top*(width-1)/scale),y,1,3,244,194,75);
    }
    fill_rect(host,3+width/2,y,1,3,244,236,209);
}

static void draw_frame_timing(struct host *host)
{
    const struct profile *p=&host->profile;
    uint64_t now=monotonic_us();
    struct profile_stats cpu=profile_window(p,now,false),gpu=profile_window(p,now,true);
    if(host->gpu_timing.supported) {
        const struct gpu_timing *g=&host->gpu_timing;
        gpu=(struct profile_stats){0};
        for(unsigned age=0;age<g->count;age++) {
            const struct gpu_sample *sample=&g->history[(g->head+GPU_HISTORY-1-age)%GPU_HISTORY];
            if(sample->valid)profile_stats_add(&gpu,sample->value,sample->stamp_us,now);
        }
    }
    int y=host->api.screen_height-24,width=host->api.screen_width;
    fill_rect(host,0,y,width,24,10,19,28);
    timing_bar(host,y+12,"CPU",cpu,false);
    timing_bar(host,y,"GPU",gpu,true);
    bool dropped=now<p->flash_until;
    uint64_t drops=host->timing.missed_total-p->drop_base;
    char line[32];
    if(drops>99999)snprintf(line,sizeof(line),"D99999+");
    else snprintf(line,sizeof(line),"D%llu",(unsigned long long)drops);
    int badge_width=(int)strlen(line)*6+4,x=width-badge_width-2;
    fill_rect(host,x,y+15,badge_width,9,dropped?155:10,dropped?30:19,dropped?35:28);
    draw_text(host,x+2,y+22,line,1,dropped?255:156,dropped?235:218,dropped?210:166);
    unsigned budget=frame_budget_us(host);
    snprintf(line,sizeof(line),"%u.%uMS",(budget+50)/1000,((budget+50)%1000)/100);
    draw_text(host,width-3-(int)strlen(line)*6,y+10,line,1,156,174,184);
}

static void draw_pause_menu(struct host *host)
{
    const int width=216,height=112;
    int x=(host->api.screen_width-width)/2,y=(host->api.screen_height-height)/2;
    fill_rect(host,x+3,y-3,width,height,4,8,11);
    fill_rect(host,x,y,width,height,40,175,212);
    fill_rect(host,x+1,y+1,width-2,height-2,12,22,28);
    menu_text(host,x+12,y+89,"PAUSED",2,238,240,232);
    fill_rect(host,x+12,y+77,width-24,1,40,75,85);
    const char *labels[]={"CONTINUE GAME","RETURN TO LAUNCHER"};
    for(int i=0;i<2;i++) {
        bool selected=host->pause_option==i;
        int row=y+61-i*24;
        fill_rect(host,x+8,row-13,width-16,20,selected?28:14,selected?74:30,selected?84:40);
        fill_rect(host,x+12,row-9,3,10,selected?244:70,selected?194:110,70);
        menu_text(host,x+22,row,labels[i],1,selected?250:170,selected?248:185,selected?236:190);
    }
    menu_text(host,x+12,y+10,"B SELECT - A BACK",1,112,160,170);
}

static void capture_gpu_resolve(struct host *host,const struct profile_shared *shared)
{
    struct trace_capture *t=&host->trace;
    while(t->gpu_cursor<t->count) {
        struct trace_span *s=&t->spans[t->gpu_cursor];
        if(s->parent!=UINT32_MAX){t->gpu_cursor++;continue;}
        if(!s->end)return;
        if(t->gpu_mode==2) {
            if(!shared || s->end>shared->watermark)return;
            s->gpu_valid=profile_gpu(shared,s->start,s->end,(uint64_t)getpid(),&s->gpu_us);
        } else if(t->gpu_mode==1 && s->gpu_serial) {
            const struct gpu_sample *g=&host->gpu_timing.history[(s->gpu_serial-1)%GPU_HISTORY];
            if(g->serial==s->gpu_serial && g->valid){s->gpu_us=g->value;s->gpu_valid=true;}
            else if(host->gpu_timing.serial-s->gpu_serial<GPU_HISTORY)return;
        }
        t->gpu_cursor++;
    }
}

static void capture_gpu_poll(struct host *host)
{
    struct trace_capture *t=&host->trace;
    if(!t->spans)return;
    uint64_t now=monotonic_us();
    if(now<t->gpu_poll_after)return;
    t->gpu_poll_after=now+100000;
    struct profile_shared shared;
    if(t->gpu_mode==2) {
        int fd=open("/run/chirky-gpu/samples",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)return;
        struct stat st;
        bool ok=!fstat(fd,&st) && st.st_size==sizeof(shared) && read(fd,&shared,sizeof(shared))==sizeof(shared);
        close(fd);
        if(!ok || shared.magic!=PROFILE_MAGIC || now>shared.watermark+2000000)return;
    }
    capture_gpu_resolve(host,t->gpu_mode==2?&shared:NULL);
}

static void draw_host(struct host *host)
{
    if(host->profile.enabled!=host->frame_timing_enabled) {
        profile_reset(&host->profile,host->frame_timing_enabled,host->timing.missed_total);
        write_status(host);
        memset(host->gpu_timing.history,0,sizeof(host->gpu_timing.history));
        host->gpu_timing.count=0;
    }
    if(host->frame_timing_enabled)profile_poll(&host->profile,monotonic_us());
    gpu_timing_poll(&host->gpu_timing);
    chirky_scope(&host->api,"profiler.gpu_poll",true);
    capture_gpu_poll(host);
    chirky_scope(&host->api,"profiler.gpu_poll",false);
    if(host->frame_timing_enabled || host->trace.recording)gpu_timing_begin(&host->gpu_timing);
    else host->gpu_timing.count=0;
    host->submitted_rectangles=0;
    rect_renderer_begin(&host->renderer);
    clear_screen();
    if (host->active_game != NULL) {
        chirky_scope(&host->api,host->active_game->id,true);
        host->game_api->render();
        chirky_scope(&host->api,host->active_game->id,false);
        if(host->paused)draw_pause_menu(host);
    }
    else if (host->display_settings) draw_display_settings(host);
    else if (host->controller_settings) draw_controller_settings(host);
    else if (host->settings_menu) draw_settings_menu(host);
    else { chirky_scope(&host->api,"launcher",true);draw_launcher(host);chirky_scope(&host->api,"launcher",false); }
    if(host->pending_game)menu_text(host,12,host->api.screen_height/2,"LOADING",2,250,248,236);
    chirky_scope(&host->api,"overlay",true);
    if (host->frame_timing_enabled) draw_frame_timing(host);
    chirky_scope(&host->api,"overlay",false);
    chirky_scope(&host->api,"renderer.flush",true);
    rect_renderer_flush(&host->renderer);
    chirky_scope(&host->api,"renderer.flush",false);
    gpu_timing_end(&host->gpu_timing);
    glDisable(GL_SCISSOR_TEST);
    if (snapshot_requested) { snapshot_requested = 0; save_snapshot(host); }
}

static void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                         unsigned int tv_usec, void *user_data)
{
    (void)fd;
    struct host *host=user_data;
    uint64_t previous=host->timing.missed_total;
    frame_timing_present(&host->timing,sequence,(uint64_t)tv_sec*1000000u+tv_usec);
    if(host->timing.missed_total>previous)host->profile.flash_until=monotonic_us()+1000000;
    host->flip_pending = false;
}

static void wait_for_events(struct host *host)
{
    struct pollfd fds[MAX_INPUTS + 2];
    fds[0].fd = host->drm_fd; fds[0].events = POLLIN;
    fds[1].fd = host->control_fd; fds[1].events = POLLIN;
    for (int index = 0; index < host->inputs.count; ++index) {
        fds[index + 2].fd = host->inputs.devices[index].fd; fds[index + 2].events = POLLIN;
    }
    drmEventContext context = {.version=DRM_EVENT_CONTEXT_VERSION,
        .vblank_handler=NULL,.page_flip_handler=flip_handler};
    while (host->flip_pending && host->running && !stop_requested) {
        int result = poll(fds, (nfds_t)(host->inputs.count + 2), -1);
        if (result < 0) { if (errno == EINTR) continue; host->running = false; break; }
        if (fds[0].revents & POLLIN) drmHandleEvent(host->drm_fd, &context);
        if (fds[1].revents & POLLIN) process_control(host);
        for (int index = 0; index < host->inputs.count; ++index)
            if (fds[index + 2].revents & POLLIN) process_input(host, fds[index + 2].fd);
    }
}

static bool first_frame(struct host *host)
{
    draw_host(host);
    if (!eglSwapBuffers(host->egl_display, host->egl_surface)) return false;
    host->front_bo = gbm_surface_lock_front_buffer(host->gbm_surface);
    if (host->front_bo == NULL) return false;
    uint32_t fb_id = framebuffer_for_bo(host, host->front_bo);
    return fb_id && drmModeSetCrtc(host->drm_fd, host->crtc_id, fb_id, 0, 0,
        &host->connector_id, 1, &host->mode) == 0;
}

static bool next_frame(struct host *host)
{
    struct trace_capture *capture=&host->trace;
    unsigned frame_span=capture->count;
    uint64_t missed_before=host->timing.missed_total;
    uint64_t presented_before=host->timing.stamp_us;
    if(capture->spans && capture->frames<capture->target && !capture->invalid) {
        capture->recording=true;host->api.profile_scope=capture_scope;
        host->submitted_rectangles=0;
    }
    chirky_scope(&host->api,"frame",true);
    uint64_t began=monotonic_us();
    struct timespec cpu_start,cpu_end;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_start);
    chirky_scope(&host->api,"update",true);
    update_host(host);
    chirky_scope(&host->api,"update",false);
    chirky_scope(&host->api,"render",true);
    draw_host(host);
    chirky_scope(&host->api,"render",false);
    if(capture->recording && frame_span<capture->count)
        capture->spans[frame_span].gpu_serial=host->gpu_timing.serial;
    host->frame_number++;
    chirky_scope(&host->api,"submit",true);
    chirky_scope(&host->api,"egl.swap",true);
    if (!eglSwapBuffers(host->egl_display, host->egl_surface)) return false;
    chirky_scope(&host->api,"egl.swap",false);
    chirky_scope(&host->api,"gbm.lock",true);
    struct gbm_bo *next = gbm_surface_lock_front_buffer(host->gbm_surface);
    chirky_scope(&host->api,"gbm.lock",false);
    if (next == NULL) return false;
    uint32_t fb_id = framebuffer_for_bo(host, next);
    if (!fb_id) { gbm_surface_release_buffer(host->gbm_surface, next); return false; }
    host->flip_pending = true;
    if (drmModePageFlip(host->drm_fd, host->crtc_id, fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, host) != 0) {
        host->flip_pending = false;
        gbm_surface_release_buffer(host->gbm_surface, next);
        return false;
    }
    host->timing.pending_work_us=(uint32_t)(monotonic_us()-began);
    chirky_scope(&host->api,"submit",false);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_end);
    uint64_t cpu_ns=(uint64_t)(cpu_end.tv_sec-cpu_start.tv_sec)*1000000000u+cpu_end.tv_nsec-cpu_start.tv_nsec;
    chirky_scope(&host->api,"present.wait",true);
    wait_for_events(host);
    chirky_scope(&host->api,"present.wait",false);
    if(host->frame_timing_enabled && !host->flip_pending)
        profile_push(&host->profile,began,monotonic_us(),(uint32_t)(cpu_ns/1000));
    if (host->timing.count && host->frame_number%300==0) {
        const struct frame_sample *s=&host->timing.history[(host->timing.head+FRAME_HISTORY-1)%FRAME_HISTORY];
        printf("Frame timing: work=%uus interval=%uus missed=%llu rects=%u batches=%u\n",
            s->work_us,s->interval_us,(unsigned long long)host->timing.missed_total,
            host->renderer.rectangles,host->renderer.batches);
        fflush(stdout);
    }
    if (!host->flip_pending) {
        gbm_surface_release_buffer(host->gbm_surface, host->front_bo);
        host->front_bo = next;
    } else gbm_surface_release_buffer(host->gbm_surface, next);
    chirky_scope(&host->api,"frame",false);
    if(capture->recording) {
        if(host->flip_pending)capture->invalid=true;
        if(frame_span<capture->count) {
            struct trace_span *frame=&capture->spans[frame_span];
            frame->missed=(unsigned)(host->timing.missed_total-missed_before);
            frame->interval=presented_before?(unsigned)(host->timing.stamp_us-presented_before):0;
            capture->missed+=frame->missed;
        }
        capture->rectangles+=host->renderer.rectangles;capture->batches+=host->renderer.batches;
        capture->sprites+=host->renderer.sprites;
        capture->frames++;capture->recording=false;host->api.profile_scope=NULL;
        if(capture->frames==capture->target || capture->invalid)capture->finish_after=monotonic_us()+300000;
    }
    if(capture->spans && capture->finish_after && monotonic_us()>=capture->finish_after) {
        bool ok=trace_write(capture,"run/profile.json.tmp") &&
            !rename("run/profile.json.tmp","run/profile.json");
        printf("Frame capture %s: run/profile.json (%u frames)\n",ok?"saved":"write failed",capture->frames);
        free(capture->spans);*capture=(struct trace_capture){0};
        write_status(host);
    }
    return true;
}

static void cleanup(struct host *host)
{
    free(host->trace.spans);host->trace.spans=NULL;
    splash_free(&host->launcher_art);
    unload_game(host);
    audio_mixer_stop(host->audio);host->audio=NULL;
    splash_free(&host->launcher_art);
    image_cache_clear(&host->images,&host->renderer);
    asset_store_destroy(host->assets);host->assets=NULL;
    for (int index = 0; index < host->inputs.count; ++index) close(host->inputs.devices[index].fd);
    if (host->saved_crtc != NULL && host->drm_fd >= 0)
        drmModeSetCrtc(host->drm_fd, host->saved_crtc->crtc_id,
            host->saved_crtc->buffer_id, host->saved_crtc->x, host->saved_crtc->y,
            &host->connector_id, 1,
            host->saved_crtc->mode_valid ? &host->saved_crtc->mode : NULL);
    if (host->front_bo != NULL && host->gbm_surface != NULL)
        gbm_surface_release_buffer(host->gbm_surface, host->front_bo);
    if (host->egl_display != EGL_NO_DISPLAY) {
        if (host->egl_context != EGL_NO_CONTEXT) {
            gpu_timing_destroy(&host->gpu_timing);
            rect_renderer_destroy(&host->renderer);
        }
        eglMakeCurrent(host->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (host->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(host->egl_display, host->egl_surface);
        if (host->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(host->egl_display, host->egl_context);
        eglTerminate(host->egl_display);
    }
    if (host->gbm_surface != NULL) gbm_surface_destroy(host->gbm_surface);
    if (host->gbm != NULL) gbm_device_destroy(host->gbm);
    drmModeFreeCrtc(host->saved_crtc);
    if (host->drm_fd >= 0) { drmDropMaster(host->drm_fd); close(host->drm_fd); }
    if (host->control_fd >= 0) close(host->control_fd);
    unlink("run/control.fifo");
    remove("run/status.json");
}

int main(void)
{
    struct host host;
    memset(&host, 0, sizeof(host));
    host.drm_fd = -1;
    host.control_fd = -1;
    host.egl_display = EGL_NO_DISPLAY;
    host.egl_context = EGL_NO_CONTEXT;
    host.egl_surface = EGL_NO_SURFACE;
    host.running = true;
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    signal(SIGUSR1, on_snapshot);

    discover_games(&host);
    load_launcher(&host);
    load_host_config(&host);
    host.drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (host.drm_fd < 0 || (drmSetMaster(host.drm_fd) != 0 && errno != EINVAL) ||
        !choose_display(&host) || !init_graphics(&host)) {
        fprintf(stderr, "Chirky host initialization failed: %s\n", strerror(errno));
        cleanup(&host);
        return EXIT_FAILURE;
    }
    host.api = (struct chirky_host_api){.abi_version=CHIRKY_ABI_VERSION,
        .screen_width=host.mode.hdisplay,.screen_height=host.mode.vdisplay,
        .context=&host,.fill_rect=fill_rect,.play_sound=play_sound,
        .draw_text=draw_text,.button_label=button_label,.asset_request=request_asset,
        .asset_status=status_asset,.asset_data=data_asset,.asset_release=release_asset,
        .draw_sprite=draw_sprite,.sound_play=sound_play};
    host.assets=asset_store_create();
    if(!host.assets){fprintf(stderr,"Could not start asset loader\n");cleanup(&host);return EXIT_FAILURE;}
    update_safe_area(&host);
    splash_load_file_api(&host.launcher_art,&host.api,"assets/launcher/splash.ppm");
    open_inputs(&host.inputs);
    mkdir("run", 0755);
    if (mkfifo("run/control.fifo", 0600) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create control channel: %s\n", strerror(errno));
        cleanup(&host);
        return EXIT_FAILURE;
    }
    host.control_fd = open("run/control.fifo", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (host.control_fd < 0) {
        fprintf(stderr, "Cannot open control channel: %s\n", strerror(errno));
        cleanup(&host);
        return EXIT_FAILURE;
    }
    load_boot_game(&host);
    write_status(&host);
    if (!first_frame(&host)) {
        cleanup(&host);
        return EXIT_FAILURE;
    }
    puts("Chirky host running. Menus: B selects, A goes back. Games: Select pauses, F1 recovers, F12 snapshots.");
    while (host.running && !stop_requested) {
        if (!next_frame(&host)) host.running = false;
    }
    cleanup(&host);
    puts("Console framebuffer restored.");
    return EXIT_SUCCESS;
}
