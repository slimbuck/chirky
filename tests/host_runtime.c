/* Exercise host input and layout without DRM, a controller, or the live Pi. */
#define main unused_host_main
#include "../src/platform/linux/host.c"
#undef main
#include <assert.h>

/* Fixture-only views and deterministic clock adapter. Production renders through the console core. */
static int launcher_game_count(const struct host *host)
{
    int count=0;
    while(count<host->game_count && !host->games[count].diagnostic)count++;
    return count;
}

static void draw_launcher(struct host *host)
{ struct chirky_host_api ui=console_api(host); clear_screen();ensure_launcher(host);console_draw_launcher(&ui,&host->launcher_art,&host->console.launcher,host->console.selected_game); }

static void draw_settings_menu(struct host *host)
{ struct chirky_host_api ui=console_api(host); clear_screen();ensure_launcher(host);console_draw_settings_menu(&ui,&host->console.launcher,host->console.settings_option); }

static void draw_live_inputs(struct host *host)
{ struct chirky_host_api ui=console_api(host);
    unsigned pad=0,key=0;char pad_line[96],key_line[96];
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        if(binding_down(&host->inputs,&host->bindings[i],false))pad|=1u<<i;
        if(binding_down(&host->inputs,&host->keyboard_bindings[i],true))key|=1u<<i;
    }
    held_input_names(host,false,pad_line,sizeof(pad_line));held_input_names(host,true,key_line,sizeof(key_line));
    console_draw_live_inputs(&ui,pad,key,pad_line,key_line);
}

static void draw_controller_settings(struct host *host)
{ struct chirky_host_api ui=console_api(host);
    clear_screen();console_draw_controller_settings(&ui,&host->console.setup,host->console.input_test,host->console.selected_option,host->console.settings_message);
    draw_live_inputs(host);
}

static void draw_display_settings(struct host *host)
{ struct chirky_host_api ui=console_api(host);clear_screen();chirky_console_draw_display(&host->console,&ui); }

static void update_timing_toggle(struct host *host,uint64_t now_us)
{
    ensure_launcher(host);
    chirky_console_timing(&host->console,host->inputs.state.buttons[CHIRKY_BUTTON_START],now_us);
}

static unsigned char framebuffer[240][320][3], clear_colour[3];
static void check_capture_gpu(void)
{
    struct host h={0};
    assert(trace_arm(&h.trace,2,16667));h.trace.recording=true;h.trace.gpu_mode=2;
    trace_scope_at(&h.trace,"frame",true,0,100,10);
    trace_scope_at(&h.trace,"frame",false,0,200,20);
    trace_scope_at(&h.trace,"frame",true,0,200,20);
    trace_scope_at(&h.trace,"frame",false,0,300,30);
    struct profile_shared shared={.watermark=150,.total=2};
    shared.jobs[0]=(struct profile_job){1,120,170,(uint64_t)getpid()};
    shared.jobs[1]=(struct profile_job){2,190,230,(uint64_t)getpid()};
    capture_gpu_resolve(&h,&shared);assert(!h.trace.gpu_cursor);
    shared.watermark=300;capture_gpu_resolve(&h,&shared);
    assert(h.trace.gpu_cursor==2 && !h.trace.spans[0].gpu_valid && !h.trace.spans[1].gpu_valid);
    /* Cross-frame jobs cannot be assigned to either frame. */
    h.trace.gpu_cursor=0;shared.total=1;
    capture_gpu_resolve(&h,&shared);
    assert(h.trace.spans[0].gpu_valid && h.trace.spans[0].gpu_us==50);
    assert(!h.trace.spans[1].gpu_valid);
    h.trace.gpu_cursor=0;h.trace.gpu_mode=1;h.trace.spans[0].gpu_valid=false;
    h.trace.spans[0].gpu_serial=1;h.gpu_timing.serial=1;
    h.gpu_timing.history[0]=(struct gpu_sample){.serial=1,.value=99};
    capture_gpu_resolve(&h,NULL);assert(!h.trace.gpu_cursor && !h.trace.spans[0].gpu_valid);
    h.gpu_timing.history[0].valid=true;capture_gpu_resolve(&h,NULL);
    assert(h.trace.spans[0].gpu_valid && h.trace.spans[0].gpu_us==99);
    free(h.trace.spans);
}
static int scissor_x,scissor_y,scissor_w,scissor_h;
static bool scissor;
void glDisable(GLenum cap) { (void)cap; scissor=false; }
void glEnable(GLenum cap) { (void)cap; scissor=true; }
void glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a)
{ (void)a;clear_colour[0]=(unsigned char)(r*255);clear_colour[1]=(unsigned char)(g*255);clear_colour[2]=(unsigned char)(b*255); }
void glScissor(GLint x,GLint y,GLsizei w,GLsizei h)
{ scissor_x=x;scissor_y=y;scissor_w=w;scissor_h=h; }
void glClear(GLbitfield mask)
{
    (void)mask;
    int left=scissor?scissor_x:0,bottom=scissor?scissor_y:0;
    int right=scissor?left+scissor_w:320,top=scissor?bottom+scissor_h:240;
    assert(left>=0 && bottom>=0 && right<=320 && top<=240);
    for(int y=bottom;y<top;y++) for(int x=left;x<right;x++) memcpy(framebuffer[y][x],clear_colour,3);
}
void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void *pixels)
{ (void)x;(void)y;(void)w;(void)h;(void)format;(void)type;(void)pixels; }

static void event(struct host *host,int device,int type,int code,int value)
{
    struct input_event input={.type=(unsigned short)type,.code=(unsigned short)code,.value=value};
    process_input_event(host,&host->inputs.devices[device],&input);
}
static void tap(struct host *host,int device,int code)
{
    /* Ordinary test taps are distinct gestures. Boundary/bounce tests below
       drive raw events explicitly, without this neutral settling period. */
    if(host->console.ui_wait_release)for(int i=0;i<3;i++)update_host(host);
    event(host,device,EV_KEY,code,1);event(host,device,EV_KEY,code,0);update_host(host);
    if(host->console.ui_wait_release)for(int i=0;i<3;i++)update_host(host);
}
static void write_preview(const char *path)
{
    FILE *file=fopen(path,"wb");assert(file);fputs("P6\n320 240\n255\n",file);
    for(int y=239;y>=0;y--) fwrite(framebuffer[y],1,sizeof(framebuffer[y]),file);
    fclose(file);
}
static unsigned int game_updates;
static void fake_update(const struct chirky_input *input) { (void)input;game_updates++; }
static void fake_shutdown(void) {}

static void check_timing_toggle(void)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    assert(!h.frame_timing_enabled);
    event(&h,0,EV_KEY,BTN_TR2,1);update_controller_buttons(&h);
    update_timing_toggle(&h,0);update_timing_toggle(&h,1999999);assert(!h.frame_timing_enabled);
    update_timing_toggle(&h,2000000);assert(h.frame_timing_enabled);
    update_timing_toggle(&h,9000000);assert(h.frame_timing_enabled);
    event(&h,0,EV_KEY,BTN_TR2,0);update_controller_buttons(&h);update_timing_toggle(&h,9000001);
    /* Keyboard Start works inside setup and while transition input is blocked. */
    h.console.controller_settings=true;h.console.setup.active=true;block_transition_input(&h);
    event(&h,1,EV_KEY,KEY_ENTER,1);update_controller_buttons(&h);
    update_timing_toggle(&h,10000000);update_timing_toggle(&h,11999999);assert(h.frame_timing_enabled);
    update_timing_toggle(&h,12000000);assert(!h.frame_timing_enabled);
    event(&h,1,EV_KEY,KEY_ENTER,0);update_controller_buttons(&h);update_timing_toggle(&h,12000001);
    event(&h,1,EV_KEY,KEY_ENTER,1);update_controller_buttons(&h);update_timing_toggle(&h,13000000);
    event(&h,1,EV_KEY,KEY_ENTER,0);update_controller_buttons(&h);update_timing_toggle(&h,14000000);
    event(&h,1,EV_KEY,KEY_ENTER,1);update_controller_buttons(&h);update_timing_toggle(&h,14500000);
    update_timing_toggle(&h,16000000);assert(!h.frame_timing_enabled);
    update_timing_toggle(&h,16500000);assert(h.frame_timing_enabled);
    event(&h,1,EV_KEY,KEY_ENTER,0);event(&h,1,EV_KEY,KEY_ENTER,1);update_controller_buttons(&h);
    update_timing_toggle(&h,17000000);update_timing_toggle(&h,18999999);assert(h.frame_timing_enabled);
    update_timing_toggle(&h,19000000);assert(!h.frame_timing_enabled);
}

static struct host *pause_render_host;
static unsigned int pause_renders;
static void fake_pause_render(void)
{
    pause_renders++;
    fill_rect(pause_render_host,0,0,pause_render_host->api.screen_width,pause_render_host->api.screen_height,21,42,63);
}

static void check_pause_menu(const char *output_dir)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    h.mode.hdisplay=320;h.mode.vdisplay=240;h.safe_x=32;h.safe_y=24;update_safe_area(&h);
    strcpy(h.games[0].id,"phosphor-run");strcpy(h.games[0].name,"Phosphor Run");h.game_count=1;
    const struct chirky_game_api fake={.update=fake_update,.shutdown=fake_shutdown,.render=fake_pause_render};
    pause_render_host=&h;pause_renders=0;
    h.console.game_active=true;h.active_game=&h.games[0];h.runtime=(struct chirky_runtime){.game=&fake,.active=true};
    unsigned int saved_updates=game_updates;
    event(&h,1,EV_KEY,KEY_ESC,1);update_host(&h);
    assert(h.console.paused && h.active_game && current_screen(&h)==SCREEN_PAUSE);
    for(int i=0;i<600;i++)update_host(&h);
    assert(h.console.paused && game_updates==saved_updates);
    event(&h,1,EV_KEY,KEY_ESC,0);for(int i=0;i<3;i++)update_host(&h);
    draw_host(&h);
    assert(pause_renders==1 && game_updates==saved_updates);
    /* The frozen game remains visible outside the centered pause panel. */
    assert(framebuffer[h.safe_y+4][h.safe_x+4][0]==21);
    assert(framebuffer[h.safe_y+4][h.safe_x+4][1]==42);
    assert(framebuffer[h.safe_y+4][h.safe_x+4][2]==63);
    int panel_x=h.safe_x+(h.api.screen_width-216)/2;
    int panel_y=h.safe_y+(h.api.screen_height-112)/2;
    assert(framebuffer[panel_y][panel_x][1]==175);
    draw_host(&h);assert(pause_renders==2 && game_updates==saved_updates);
    char path[512];snprintf(path,sizeof(path),"%s/pause-menu.ppm",output_dir);write_preview(path);
    event(&h,1,EV_KEY,KEY_X,1);update_host(&h);assert(!h.console.paused && h.active_game);
    for(int i=0;i<30;i++)update_host(&h);
    assert(game_updates==saved_updates);
    event(&h,1,EV_KEY,KEY_X,0);for(int i=0;i<3;i++)update_host(&h);
    assert(game_updates==saved_updates+1);
    tap(&h,1,KEY_ESC);assert(h.console.paused);
    tap(&h,1,KEY_Z);assert(!h.console.paused && h.active_game);
    tap(&h,1,KEY_ESC);tap(&h,1,KEY_DOWN);assert(h.console.paused && h.console.pause_option==1);
    tap(&h,1,KEY_X);assert(!h.active_game && !h.console.paused && current_screen(&h)==SCREEN_LAUNCHER);
    game_updates=saved_updates;
}

static void check_settings_shortcuts(void)
{
    struct host h={0};h.mode.hdisplay=320;h.mode.vdisplay=240;h.safe_x=16;h.safe_y=12;
    open_settings_screen(&h,1);
    assert(current_screen(&h)==SCREEN_DISPLAY && h.console.settings_menu && h.console.ui_wait_release);
    h.safe_x=20;h.safe_offset_x=2;
    open_settings_screen(&h,0);
    assert(current_screen(&h)==SCREEN_INPUT && h.safe_x==16 && h.safe_offset_x==0);
    assert(h.console.selected_option==0 && !h.console.setup.active);
    h.console.setup.active=true;
    open_settings_screen(&h,1);
    assert(!h.console.setup.active && current_screen(&h)==SCREEN_DISPLAY && h.console.saved_display.x==16);
}

static void check_launcher_menu(void)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    h.game_count=3;
    strcpy(h.games[0].id,"hardware-test");strcpy(h.games[0].name,"Hardware Test");h.games[0].diagnostic=true;
    strcpy(h.games[1].id,"rosey-chop");strcpy(h.games[1].name,"Rosey Chop");
    strcpy(h.games[2].id,"phosphor-run");strcpy(h.games[2].name,"Phosphor Run");
    qsort(h.games,h.game_count,sizeof(h.games[0]),compare_games);
    assert(launcher_game_count(&h)==2);
    assert(!strcmp(h.games[0].id,"phosphor-run") && !strcmp(h.games[1].id,"rosey-chop"));
    tap(&h,1,KEY_DOWN);assert(h.console.selected_game==1);
    tap(&h,1,KEY_DOWN);assert(h.console.selected_game==2);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_DOWN);tap(&h,1,KEY_DOWN);assert(h.console.settings_option==2);
    const struct chirky_game_api fake={.update=fake_update,.shutdown=fake_shutdown};
    h.console.game_active=true;h.console.diagnostic=true;h.active_game=&h.games[2];h.runtime=(struct chirky_runtime){.game=&fake,.active=true};
    tap(&h,1,KEY_ESC);assert(current_screen(&h)==SCREEN_SETTINGS && h.console.settings_option==2);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_LAUNCHER && h.console.selected_game==2);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_UP);assert(h.console.settings_option==3);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_LAUNCHER);
    tap(&h,1,KEY_X);tap(&h,1,KEY_ESC);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_INPUT);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_LAUNCHER);
    tap(&h,1,KEY_DOWN);assert(h.console.selected_game==3);
    tap(&h,1,KEY_DOWN);assert(h.console.selected_game==0);
}

static void check_transition_gates(void)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    h.inputs.devices[0].abs_minimums[ABS_HAT0X]=-1;h.inputs.devices[0].abs_maximums[ABS_HAT0X]=1;
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);
    assert(h.console.settings_menu && !h.console.controller_settings && h.console.ui_wait_release);
    for(int i=0;i<30;i++){event(&h,0,EV_KEY,BTN_EAST,2);update_host(&h);}
    assert(!h.console.setup.active);
    /* A brief release followed by bounce, or a second input source, must
       not activate the first item on the new screen. */
    event(&h,0,EV_KEY,BTN_EAST,0);update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,1);event(&h,1,EV_KEY,KEY_X,1);update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<4;i++)update_host(&h);
    assert(h.console.ui_wait_release && !h.console.setup.active);
    event(&h,1,EV_KEY,KEY_X,0);update_host(&h);assert(h.console.ui_wait_release);
    update_host(&h);assert(!h.console.ui_wait_release && !h.console.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,2);update_host(&h);assert(!h.console.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);assert(h.console.controller_settings && !h.console.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);assert(h.console.setup.active && h.console.setup.step==0);
    for(int i=0;i<5;i++)update_host(&h);
    assert(!h.console.setup.ready);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_ABS,ABS_HAT0X,-1);update_host(&h);assert(h.console.setup.ready && h.console.setup.step==0);
    event(&h,0,EV_ABS,ABS_HAT0X,0);update_host(&h);assert(h.console.setup.step==1);
    event(&h,1,EV_KEY,KEY_F1,1);update_host(&h);assert(!h.console.setup.active && h.console.controller_settings);
    event(&h,1,EV_KEY,KEY_F1,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_KEY,BTN_SOUTH,1);update_host(&h);assert(!h.console.controller_settings);
    for(int i=0;i<4;i++)update_host(&h);
    assert(!h.console.controller_settings);
    /* The shared game filter suppresses both held and edge-triggered buttons. */
    struct chirky_input_gate gate={0};struct chirky_input in={0},out;
    chirky_gate_begin(&gate);in.buttons[CHIRKY_BUTTON_PRIMARY]=true;
    in.button_pressed[CHIRKY_BUTTON_SECONDARY]=true;
    chirky_gate_filter(&gate,&in,&out);
    assert(!out.buttons[CHIRKY_BUTTON_PRIMARY] && !out.button_pressed[CHIRKY_BUTTON_SECONDARY]);
    memset(&in,0,sizeof(in));chirky_gate_filter(&gate,&in,&out);assert(gate.blocked);
    chirky_gate_filter(&gate,&in,&out);assert(!gate.blocked);
    in.button_pressed[CHIRKY_BUTTON_PRIMARY]=true;chirky_gate_filter(&gate,&in,&out);assert(out.button_pressed[CHIRKY_BUTTON_PRIMARY]);
}

static void check_frame_timing(struct host *host,const char *output_dir)
{
    struct frame_timing t={0};t.pending_work_us=2100;
    frame_timing_present(&t,100,1000000);assert(t.count==0);
    frame_timing_present(&t,101,1016667);assert(t.count==1 && !t.missed_total);
    assert(t.history[0].work_us==2100 && t.history[0].interval_us==16667);
    t.pending_work_us=22000;frame_timing_present(&t,103,1050000);
    assert(t.missed_total==1 && t.history[1].missed==1 && t.history[1].interval_us==33333);
    /* Delayed callback execution alone is not a miss; only display sequence gaps count. */
    t.pending_work_us=1000;frame_timing_present(&t,104,1066667);assert(t.missed_total==1);
    frame_timing_present(&t,104,1066667);assert(t.count==3);
    t.sequence=UINT32_MAX;t.stamp_us=2000000;
    frame_timing_present(&t,0,2016667);assert(t.count==4 && t.missed_total==1);
    for(int i=0;i<120;i++)frame_timing_present(&t,(uint32_t)i+1,2016667+(uint64_t)(i+1)*16667);
    assert(t.count==FRAME_HISTORY && t.head<FRAME_HISTORY);
    /* Restarted DRM counters rebase rather than creating billions of misses. */
    frame_timing_present(&t,1,5000000);assert(t.missed_total==1);
    host->timing=t;host->mode.vrefresh=60;assert(frame_budget_us(host)==16666);
    host->timing.pending_work_us=23000;
    frame_timing_present(&host->timing,3,5033333);
    draw_launcher(host);draw_frame_timing(host);
    char output[512];snprintf(output,sizeof(output),"%s/frame-timing.ppm",output_dir);write_preview(output);
    profile_reset(&host->profile,true,0);
    uint64_t now=monotonic_us();
    for(unsigned i=0;i<PROFILE_HISTORY;i++) {
        profile_push(&host->profile,now-20000,now-1000,2000+i*10);
        struct profile_frame *f=&host->profile.frames[i];f->resolved=f->valid=true;f->gpu=500+i*2;
    }
    host->profile.flash_until=now+1000000;
    draw_launcher(host);draw_frame_timing(host);
    snprintf(output,sizeof(output),"%s/frame-timing-gpu.ppm",output_dir);write_preview(output);
    profile_reset(&host->profile,false,0);
    /* Batched geometry retains clipping, native pixel positions and painter order. */
    struct rect_vertex vertices[18];
    host->renderer=(struct rect_renderer){.program=1,.vertices=vertices,.width=320,.height=240};
    fill_rect(host,-3,-4,8,9,255,40,80);fill_rect(host,1,1,2,2,10,200,30);
    assert(host->renderer.count==12 && host->renderer.rectangles==2);
    assert(vertices[0].x==2.f*host->safe_x/320-1.f);
    assert(vertices[0].y==2.f*host->safe_y/240-1.f);
    assert(vertices[2].x==2.f*(host->safe_x+5)/320-1.f);
    assert(vertices[2].y==2.f*(host->safe_y+5)/240-1.f);
    assert(vertices[0].r==255 && vertices[6].g==200 && vertices[6].a==255);
    memset(&host->renderer,0,sizeof(host->renderer));
}

static void check_migration(void)
{
    assert(rename(HOST_CONFIG_PATH,"config/before-migration.conf")==0);
    FILE *f=fopen(HOST_CONFIG_PATH,"w");assert(f);
    fputs("input_version=2\nbind_jump=key:307\nbind_dash=key:305\nbind_confirm=key:313\nbind_menu=key:312\nkey_jump=key:19\nkey_confirm=key:28\n",f);fclose(f);
    struct host migrated={0};load_host_config(&migrated);
    assert(migrated.bindings[CHIRKY_BUTTON_SECONDARY].code==BTN_NORTH);
    assert(migrated.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_EAST);
    assert(migrated.bindings[CHIRKY_BUTTON_SECONDARY].code==BTN_NORTH);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_R);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_START].code==KEY_ENTER);
    assert(save_bindings(&migrated));
    char saved[4096]={0};f=fopen(HOST_CONFIG_PATH,"r");assert(f);assert(fread(saved,1,sizeof(saved)-1,f)>0);fclose(f);
    assert(strstr(saved,"input_version=4") && strstr(saved,"bind_secondary=key:307") && !strstr(saved,"bind_a="));
    assert(!strstr(saved,"bind_jump=") && !strstr(saved,"bind_confirm=") && !strstr(saved,"key_confirm="));
    struct host reloaded={0};load_host_config(&reloaded);
    assert(reloaded.bindings[CHIRKY_BUTTON_SECONDARY].code==BTN_NORTH);
    f=fopen(HOST_CONFIG_PATH,"w");assert(f);
    fputs("input_version=2\nbind_y=key:304\nbind_jump=key:307\nkey_y=key:44\nkey_jump=key:19\n",f);fclose(f);
    load_host_config(&migrated);
    assert(migrated.bindings[CHIRKY_BUTTON_SECONDARY].code==BTN_SOUTH);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_Z);
    f=fopen(HOST_CONFIG_PATH,"w");assert(f);
    fputs("input_version=3\nbind_b=key:310\nbind_y=key:311\nbind_select=key:312\nkey_b=key:44\nkey_y=key:45\nkey_select=key:1\nbind_a=key:307\n",f);fclose(f);
    load_host_config(&migrated);
    assert(migrated.bindings[CHIRKY_BUTTON_PRIMARY].code==310);
    assert(migrated.bindings[CHIRKY_BUTTON_SECONDARY].code==311);
    assert(migrated.bindings[CHIRKY_BUTTON_MENU].code==312);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_PRIMARY].code==KEY_Z);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_X);
    assert(save_bindings(&migrated));
    load_host_config(&reloaded);
    assert(reloaded.bindings[CHIRKY_BUTTON_PRIMARY].code==310);
    f=fopen(HOST_CONFIG_PATH,"w");assert(f);
    fputs("input_version=3\nkey_primary=key:19\nkey_b=key:44\nbind_b=key:304\n",f);fclose(f);
    load_host_config(&migrated);
    assert(migrated.keyboard_bindings[CHIRKY_BUTTON_PRIMARY].code==KEY_R);
    assert(migrated.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_SOUTH);
    assert(migrated.bindings[CHIRKY_BUTTON_SECONDARY].kind==BINDING_NONE);
    assert(remove(HOST_CONFIG_PATH)==0);
    assert(rename("config/before-migration.conf",HOST_CONFIG_PATH)==0);
}

static void check_live_inputs(struct host *host,const char *output_dir)
{
    assert(CHIRKY_BUTTON_COUNT==8);
    host->console.controller_settings=true;host->console.selected_option=2;
    tap(host,1,KEY_X);assert(host->console.input_test);
    /* Every keyboard key addresses one Chirky button, independent of UI behavior. */
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        unsigned int key=host->keyboard_bindings[i].code;
        event(host,1,EV_KEY,key,1);update_controller_buttons(host);assert(host->inputs.state.buttons[i]);
        event(host,1,EV_KEY,key,0);update_controller_buttons(host);assert(!host->inputs.state.buttons[i]);
    }
    update_host(host);
    char name[96];host->last_keyboard=true;
    button_label(host,CHIRKY_BUTTON_SECONDARY,name,sizeof(name));assert(!strcmp(name,"SECONDARY"));
    host->last_keyboard=false;
    button_label(host,CHIRKY_BUTTON_SECONDARY,name,sizeof(name));assert(!strcmp(name,"SECONDARY"));
    event(host,0,EV_KEY,BTN_SOUTH,1);event(host,1,EV_KEY,KEY_R,1);
    event(host,0,EV_KEY,BTN_MODE,1);event(host,1,EV_KEY,KEY_T,1);
    event(host,0,EV_ABS,ABS_HAT0X,-1);update_host(host);
    assert(host->console.input_test && host->inputs.state.buttons[CHIRKY_BUTTON_SECONDARY]);
    held_input_names(host,false,name,sizeof(name));assert(strstr(name,"304") && strstr(name,"316") && strstr(name,"AX16NEG"));
    held_input_names(host,true,name,sizeof(name));assert(strstr(name," R") && strstr(name," T"));
    draw_controller_settings(host);
    char output[512];snprintf(output,sizeof(output),"%s/input-test.ppm",output_dir);write_preview(output);
    int cell=(host->api.screen_width-20)/4,x=host->safe_x+10+(CHIRKY_BUTTON_SECONDARY%4)*cell;
    int indicator_y=host->safe_y+63-(CHIRKY_BUTTON_SECONDARY/4)*21;
    assert(framebuffer[indicator_y][x+2][1]==220);
    assert(framebuffer[indicator_y][x+cell/2][0]==250);
    event(host,1,EV_KEY,KEY_R,0);update_host(host);draw_controller_settings(host);
    assert(host->inputs.state.buttons[CHIRKY_BUTTON_SECONDARY]);
    assert(framebuffer[indicator_y][x+cell/2][0]==60);
    event(host,0,EV_KEY,BTN_SOUTH,0);event(host,0,EV_KEY,BTN_MODE,0);event(host,1,EV_KEY,KEY_T,0);
    event(host,0,EV_ABS,ABS_HAT0X,0);update_host(host);
    held_input_names(host,false,name,sizeof(name));assert(!strcmp(name,"PAD NONE"));
    held_input_names(host,true,name,sizeof(name));assert(!strcmp(name,"KEY NONE"));
    tap(host,0,BTN_TR2);tap(host,0,BTN_TL2);assert(host->console.input_test);
    event(host,1,EV_KEY,KEY_R,1);for(int i=0;i<59;i++) update_host(host);assert(host->console.input_test);
    update_host(host);assert(!host->console.input_test && host->console.controller_settings);
    event(host,1,EV_KEY,KEY_R,0);update_host(host);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    static const uint8_t expected_colon[7]={0,4,4,0,4,4,0};
    assert(!memcmp(glyph(':'),expected_colon,sizeof(expected_colon)));
    assert(memcmp(glyph(':'),glyph('?'),sizeof(expected_colon)));
    char directory[]="/tmp/chirky-host-test-XXXXXX";assert(mkdtemp(directory));assert(chdir(directory)==0);
    assert(mkdir("config",0700)==0);
    check_pause_menu(argv[1]);
    check_settings_shortcuts();
    check_launcher_menu();
    check_transition_gates();
    check_timing_toggle();check_capture_gpu();
    FILE *config=fopen(HOST_CONFIG_PATH,"w");assert(config);
    fputs("boot_game=launcher\n# Keep this comment\nbind_confirm=key:313\nframe_timing=1\n",config);fclose(config);
    static struct host host;
    char art_path[1024];snprintf(art_path,sizeof(art_path),"%s/../assets/launcher/splash.ppm",argv[1]);
    assert(splash_load_file(&host.launcher_art,art_path));
    host.api.context=&host;host.api.fill_rect=fill_rect;
    host.mode.hdisplay=320;host.mode.vdisplay=240;
    load_host_config(&host);update_safe_area(&host);
    assert(!host.frame_timing_enabled);
    assert(host.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_EAST);
    assert(host.api.screen_width==288 && host.api.screen_height==216);
    host.inputs.count=2;host.inputs.devices[0].controller=true;
    strcpy(host.inputs.devices[0].name,"GP2040");
    for(int axis=ABS_HAT0X;axis<=ABS_HAT0Y;axis++) {
        host.inputs.devices[0].abs_minimums[axis]=-1;host.inputs.devices[0].abs_maximums[axis]=1;
    }
    clear_screen();fill_rect(&host,-10,-10,400,400,255,255,255);
    assert(scissor_x==16 && scissor_y==12 && scissor_w==288 && scissor_h==216);
    /* Every launcher item uses exactly the same confirmation policy. */
    tap(&host,0,BTN_TR2);assert(!host.console.controller_settings);
    tap(&host,0,BTN_SOUTH);assert(!host.console.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.console.settings_menu && !host.console.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.console.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.console.setup.active && !host.console.setup.keyboard);
    update_host(&host);assert(!host.console.setup.wait_release);
    /* D-pad must return to neutral, and another event cannot overwrite capture. */
    event(&host,0,EV_ABS,ABS_HAT0X,-1);event(&host,0,EV_KEY,BTN_SOUTH,1);update_host(&host);
    assert(host.console.setup.step==0 && host.console.setup.candidate.code==ABS_HAT0X);
    event(&host,0,EV_KEY,BTN_SOUTH,0);update_host(&host);assert(host.console.setup.step==0);
    event(&host,0,EV_ABS,ABS_HAT0X,0);update_host(&host);assert(host.console.setup.step==1);
    event(&host,0,EV_ABS,ABS_HAT0X,1);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0X,0);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0Y,-1);event(&host,0,EV_ABS,ABS_HAT0Y,0);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0Y,1);event(&host,0,EV_ABS,ABS_HAT0Y,0);update_host(&host);
    assert(host.console.setup.step==4);
    tap(&host,0,BTN_TR2);assert(host.console.setup.step==5);
    tap(&host,0,BTN_TR2);assert(host.console.setup.step==5 && *host.console.setup.message);
    tap(&host,0,BTN_SOUTH);assert(host.console.setup.step==6);
    tap(&host,0,BTN_EAST);
    assert(host.console.setup.step==7 && host.console.setup.active);
    tap(&host,0,BTN_TL2);assert(!host.console.setup.active && host.console.controller_settings);
    assert(host.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_TR2);
    update_host(&host);
    /* Keyboard setup captures Escape as a mapping; only F1 cancels. */
    host.console.selected_option=1;tap(&host,1,KEY_X);update_host(&host);
    assert(host.console.setup.active && host.console.setup.keyboard);
    const int keyboard[]={KEY_A,KEY_D,KEY_W,KEY_S,KEY_X,KEY_R,KEY_ENTER,KEY_ESC};
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) tap(&host,1,keyboard[i]);
    assert(!host.console.setup.active && host.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_R);
    update_host(&host);
    /* Keyboard and controller states are independent; quick taps survive polling. */
    event(&host,1,EV_KEY,KEY_R,1);event(&host,1,EV_KEY,KEY_R,0);update_controller_buttons(&host);
    assert(host.inputs.state.button_pressed[CHIRKY_BUTTON_SECONDARY]);
    event(&host,0,EV_KEY,KEY_ESC,1);assert(!host.inputs.pressed[KEY_ESC]);event(&host,0,EV_KEY,KEY_ESC,0);
    update_host(&host);
    tap(&host,1,KEY_X);update_host(&host);tap(&host,1,KEY_Q);tap(&host,1,KEY_F1);
    assert(!host.console.setup.active && host.keyboard_bindings[CHIRKY_BUTTON_LEFT].code==KEY_A);
    update_host(&host);
    struct host reloaded={0};load_host_config(&reloaded);
    assert(reloaded.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_R);
    assert(reloaded.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_TR2);
    check_migration();
    check_live_inputs(&host,argv[1]);
    /* A controller-only user can cancel without consuming Start/Select as back. */
    setup_begin(&host.console.setup,false);update_host(&host);
    event(&host,0,EV_KEY,BTN_EAST,1);event(&host,0,EV_KEY,BTN_SOUTH,1);
    for(int i=0;i<60;i++) update_host(&host);
    assert(!host.console.setup.active && host.bindings[CHIRKY_BUTTON_PRIMARY].code==BTN_TR2);
    event(&host,0,EV_KEY,BTN_EAST,0);event(&host,0,EV_KEY,BTN_SOUTH,0);update_host(&host);
    /* Failed persistence must not change the live mappings. */
    for(int i=0;i<3;i++)update_host(&host);
    assert(rename(HOST_CONFIG_PATH,"config/saved.conf")==0);assert(mkdir(HOST_CONFIG_PATH,0700)==0);
    setup_begin(&host.console.setup,true);host.console.setup.complete=true;
    default_keyboard_bindings(host.console.setup.pending);update_host(&host);
    assert(!host.console.setup.active && host.keyboard_bindings[CHIRKY_BUTTON_SECONDARY].code==KEY_R);
    assert(rmdir(HOST_CONFIG_PATH)==0);assert(rename("config/saved.conf",HOST_CONFIG_PATH)==0);update_host(&host);
    host.console.controller_settings=false;host.console.settings_menu=true;host.console.settings_option=1;
    tap(&host,0,BTN_TR2);assert(host.console.display_settings);
    tap(&host,1,KEY_D);assert(host.safe_x==17);
    tap(&host,1,KEY_R);assert(!host.console.display_settings && host.safe_x==16);
    tap(&host,0,BTN_TR2);tap(&host,1,KEY_D);tap(&host,1,KEY_S);tap(&host,1,KEY_D);
    assert(host.safe_x==17 && host.safe_y==13);
    tap(&host,1,KEY_S);tap(&host,1,KEY_D); /* horizontal +1 */
    tap(&host,1,KEY_S);tap(&host,1,KEY_D); /* vertical +1 */
    assert(host.safe_offset_x==1 && host.safe_offset_y==1);
    assert(host.api.screen_width==286 && host.api.screen_height==214);
    fill_rect(&host,0,0,8,8,255,255,255);
    assert(scissor_x==18 && scissor_y==14 && scissor_w==8 && scissor_h==8);
    tap(&host,1,KEY_S);tap(&host,0,BTN_TR2);assert(!host.console.display_settings);
    load_host_config(&reloaded);assert(reloaded.safe_x==17 && reloaded.safe_y==13);
    assert(reloaded.safe_offset_x==1 && reloaded.safe_offset_y==1);
    tap(&host,0,BTN_TR2);host.console.display_option=2;tap(&host,1,KEY_A);
    host.console.display_option=3;tap(&host,1,KEY_A);
    assert(host.safe_offset_x==0 && host.safe_offset_y==0);
    tap(&host,1,KEY_R);assert(host.safe_offset_x==1 && host.safe_offset_y==1);
    /* Offsets cannot push the logical viewport outside the physical framebuffer. */
    struct host moved={0};moved.mode.hdisplay=320;moved.mode.vdisplay=240;
    moved.safe_x=16;moved.safe_y=12;moved.safe_offset_x=32;moved.safe_offset_y=-24;
    update_safe_area(&moved);assert(moved.safe_offset_x==16 && moved.safe_offset_y==-12);
    fill_rect(&moved,-10,-10,400,400,255,255,255);
    assert(scissor_x==32 && scissor_y==0 && scissor_w==288 && scissor_h==216);
    struct rect_vertex translated[6];
    moved.renderer=(struct rect_renderer){.program=1,.vertices=translated,.width=320,.height=240};
    fill_rect(&moved,0,0,8,8,255,255,255);
    assert(translated[0].x==2.f*32/320-1.f && translated[0].y==-1.f);
    assert(translated[2].x==2.f*40/320-1.f && translated[2].y==2.f*8/240-1.f);
    moved.safe_x=moved.safe_y=0;update_safe_area(&moved);
    assert(!moved.safe_offset_x && !moved.safe_offset_y);
    /* Gameplay chord is Start+Select, not ordinary jump+dash. */
    const struct chirky_game_api fake={.update=fake_update,.shutdown=fake_shutdown};
    host.console.game_active=true;host.active_game=&host.games[0];host.runtime=(struct chirky_runtime){.game=&fake,.active=true};
    event(&host,0,EV_KEY,BTN_SOUTH,1);event(&host,0,EV_KEY,BTN_EAST,1);
    for(int i=0;i<65;i++) update_host(&host);
    assert(host.active_game && game_updates==65);
    event(&host,0,EV_KEY,BTN_SOUTH,0);event(&host,0,EV_KEY,BTN_EAST,0);
    host.active_game=NULL;host.runtime=(struct chirky_runtime){0};
    /* Default calibration with the actual shipped menu labels and artwork. */
    host.safe_x=16;host.safe_y=12;host.safe_offset_x=host.safe_offset_y=0;
    update_safe_area(&host);host.game_count=2;host.console.selected_game=0;
    snprintf(host.games[0].id,sizeof(host.games[0].id),"phosphor-run");
    snprintf(host.games[0].name,sizeof(host.games[0].name),"Phosphor Run");
    snprintf(host.games[1].id,sizeof(host.games[1].id),"rosey-chop");
    snprintf(host.games[1].name,sizeof(host.games[1].name),"Rosey Chop");
    host.console.launcher.count=0;
    draw_launcher(&host);
    char artwork_preview[1024];snprintf(artwork_preview,sizeof(artwork_preview),"%s/launcher-art.ppm",argv[1]);write_preview(artwork_preview);
    /* Render fixtures at the maximum margins as well as the default. */
    host.safe_offset_x=host.safe_offset_y=0;
    host.safe_x=32;host.safe_y=24;update_safe_area(&host);host.game_count=6;host.console.selected_game=7;
    for(int i=0;i<6;i++) snprintf(host.games[i].name,sizeof(host.games[i].name),"GAME %d",i+1);
    draw_launcher(&host);char output[512];snprintf(output,sizeof(output),"%s/launcher.ppm",argv[1]);write_preview(output);
    host.console.settings_message="";host.console.selected_option=2;
    draw_controller_settings(&host);snprintf(output,sizeof(output),"%s/input-settings.ppm",argv[1]);write_preview(output);
    setup_begin(&host.console.setup,false);host.console.setup.step=6;host.console.setup.wait_release=false;
    memcpy(host.console.setup.pending,host.bindings,sizeof(host.bindings));
    draw_controller_settings(&host);snprintf(output,sizeof(output),"%s/button-setup.ppm",argv[1]);write_preview(output);
    host.console.settings_message="";
    draw_display_settings(&host);snprintf(output,sizeof(output),"%s/display-area.ppm",argv[1]);write_preview(output);
    host.console.setup.active=false;host.console.controller_settings=false;
    draw_settings_menu(&host);snprintf(output,sizeof(output),"%s/settings.ppm",argv[1]);write_preview(output);
    check_frame_timing(&host,argv[1]);
    splash_free(&host.launcher_art);
    assert(remove(HOST_CONFIG_PATH)==0);assert(rmdir("config")==0);
    assert(remove("run/status.json")==0);assert(rmdir("run")==0);
    assert(chdir("/tmp")==0);assert(rmdir(directory)==0);
    puts("Host: Chirky buttons, migration, live controller/keyboard indicators, test mode, viewport, setup and persistence passed.");
}
