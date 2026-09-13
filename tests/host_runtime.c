/* Exercise host input and layout without DRM, a controller, or the live Pi. */
#define main unused_host_main
#include "../src/host.c"
#undef main
#include <assert.h>

static unsigned char framebuffer[240][320][3], clear_colour[3];
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
    if(host->ui_wait_release)for(int i=0;i<3;i++)update_host(host);
    event(host,device,EV_KEY,code,1);event(host,device,EV_KEY,code,0);update_host(host);
    if(host->ui_wait_release)for(int i=0;i<3;i++)update_host(host);
}
static void write_preview(const char *path)
{
    FILE *file=fopen(path,"wb");assert(file);fputs("P6\n320 240\n255\n",file);
    for(int y=239;y>=0;y--) fwrite(framebuffer[y],1,sizeof(framebuffer[y]),file);
    fclose(file);
}
static unsigned int game_updates;
static void fake_update(const struct two_forty_input *input) { (void)input;game_updates++; }
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
    h.controller_settings=true;h.setup.active=true;block_transition_input(&h);
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

static void check_settings_shortcuts(void)
{
    struct host h={0};h.mode.hdisplay=320;h.mode.vdisplay=240;h.safe_x=16;h.safe_y=12;
    open_settings_screen(&h,1);
    assert(current_screen(&h)==SCREEN_DISPLAY && h.settings_menu && h.ui_wait_release);
    h.safe_x=20;h.safe_offset_x=2;
    open_settings_screen(&h,0);
    assert(current_screen(&h)==SCREEN_INPUT && h.safe_x==16 && h.safe_offset_x==0);
    assert(h.selected_option==0 && !h.setup.active);
    h.setup.active=true;
    open_settings_screen(&h,1);
    assert(!h.setup.active && current_screen(&h)==SCREEN_DISPLAY && h.saved_safe_x==16);
}

static void check_launcher_menu(void)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    h.game_count=3;
    strcpy(h.games[0].id,"hardware-test");strcpy(h.games[0].name,"Hardware Test");
    strcpy(h.games[1].id,"rosey-chop");strcpy(h.games[1].name,"Rosey Chop");
    strcpy(h.games[2].id,"phosphor-run");strcpy(h.games[2].name,"Phosphor Run");
    qsort(h.games,h.game_count,sizeof(h.games[0]),compare_games);
    assert(launcher_game_count(&h)==2);
    assert(!strcmp(h.games[0].id,"phosphor-run") && !strcmp(h.games[1].id,"rosey-chop"));
    tap(&h,1,KEY_DOWN);assert(h.selected_game==1);
    tap(&h,1,KEY_DOWN);assert(h.selected_game==2);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_DOWN);tap(&h,1,KEY_DOWN);assert(h.settings_option==2);
    const struct two_forty_game_api fake={.update=fake_update,.shutdown=fake_shutdown};
    h.active_game=&h.games[2];h.game_api=&fake;
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_SETTINGS && h.settings_option==2);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_LAUNCHER && h.selected_game==2);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_UP);assert(h.settings_option==3);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_LAUNCHER);
    tap(&h,1,KEY_X);tap(&h,1,KEY_ESC);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_X);assert(current_screen(&h)==SCREEN_INPUT);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_SETTINGS);
    tap(&h,1,KEY_Z);assert(current_screen(&h)==SCREEN_LAUNCHER);
    tap(&h,1,KEY_DOWN);assert(h.selected_game==3);
    tap(&h,1,KEY_DOWN);assert(h.selected_game==0);
}

static void check_transition_gates(void)
{
    struct host h={0};default_bindings(h.bindings);default_keyboard_bindings(h.keyboard_bindings);
    h.inputs.count=2;h.inputs.devices[0].controller=true;
    h.inputs.devices[0].abs_minimums[ABS_HAT0X]=-1;h.inputs.devices[0].abs_maximums[ABS_HAT0X]=1;
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);
    assert(h.settings_menu && !h.controller_settings && h.ui_wait_release);
    for(int i=0;i<30;i++){event(&h,0,EV_KEY,BTN_EAST,2);update_host(&h);}
    assert(!h.setup.active);
    /* A brief release followed by bounce, or a second input source, must
       not activate the first item on the new screen. */
    event(&h,0,EV_KEY,BTN_EAST,0);update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,1);event(&h,1,EV_KEY,KEY_X,1);update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<4;i++)update_host(&h);
    assert(h.ui_wait_release && !h.setup.active);
    event(&h,1,EV_KEY,KEY_X,0);update_host(&h);assert(h.ui_wait_release);
    update_host(&h);assert(!h.ui_wait_release && !h.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,2);update_host(&h);assert(!h.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);assert(h.controller_settings && !h.setup.active);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_KEY,BTN_EAST,1);update_host(&h);assert(h.setup.active && h.setup.step==0);
    for(int i=0;i<5;i++)update_host(&h);
    assert(!h.setup.ready);
    event(&h,0,EV_KEY,BTN_EAST,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_ABS,ABS_HAT0X,-1);update_host(&h);assert(h.setup.ready && h.setup.step==0);
    event(&h,0,EV_ABS,ABS_HAT0X,0);update_host(&h);assert(h.setup.step==1);
    event(&h,1,EV_KEY,KEY_F1,1);update_host(&h);assert(!h.setup.active && h.controller_settings);
    event(&h,1,EV_KEY,KEY_F1,0);for(int i=0;i<3;i++)update_host(&h);
    event(&h,0,EV_KEY,BTN_SOUTH,1);update_host(&h);assert(!h.controller_settings);
    for(int i=0;i<4;i++)update_host(&h);
    assert(!h.controller_settings);
    /* The shared game filter suppresses both held and edge-triggered buttons. */
    struct two_forty_input_gate gate={0};struct two_forty_input in={0},out;
    two_forty_gate_begin(&gate);in.buttons[TWO_FORTY_BUTTON_B]=true;
    in.button_pressed[TWO_FORTY_BUTTON_Y]=true;
    two_forty_gate_filter(&gate,&in,&out);
    assert(!out.buttons[TWO_FORTY_BUTTON_B] && !out.button_pressed[TWO_FORTY_BUTTON_Y]);
    memset(&in,0,sizeof(in));two_forty_gate_filter(&gate,&in,&out);assert(gate.blocked);
    two_forty_gate_filter(&gate,&in,&out);assert(!gate.blocked);
    in.button_pressed[TWO_FORTY_BUTTON_B]=true;two_forty_gate_filter(&gate,&in,&out);assert(out.button_pressed[TWO_FORTY_BUTTON_B]);
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
    assert(migrated.bindings[TWO_FORTY_BUTTON_Y].code==BTN_NORTH);
    assert(migrated.bindings[TWO_FORTY_BUTTON_B].code==BTN_EAST);
    assert(migrated.bindings[TWO_FORTY_BUTTON_A].kind==BINDING_NONE);
    assert(migrated.keyboard_bindings[TWO_FORTY_BUTTON_Y].code==KEY_R);
    assert(migrated.keyboard_bindings[TWO_FORTY_BUTTON_START].code==KEY_ENTER);
    assert(save_bindings(&migrated));
    char saved[4096]={0};f=fopen(HOST_CONFIG_PATH,"r");assert(f);assert(fread(saved,1,sizeof(saved)-1,f)>0);fclose(f);
    assert(strstr(saved,"input_version=3") && strstr(saved,"bind_a=none"));
    assert(!strstr(saved,"bind_jump=") && !strstr(saved,"bind_confirm=") && !strstr(saved,"key_confirm="));
    struct host reloaded={0};load_host_config(&reloaded);
    assert(reloaded.bindings[TWO_FORTY_BUTTON_A].kind==BINDING_NONE);
    f=fopen(HOST_CONFIG_PATH,"w");assert(f);
    fputs("input_version=2\nbind_y=key:304\nbind_jump=key:307\nkey_y=key:44\nkey_jump=key:19\n",f);fclose(f);
    load_host_config(&migrated);
    assert(migrated.bindings[TWO_FORTY_BUTTON_Y].code==BTN_SOUTH);
    assert(migrated.keyboard_bindings[TWO_FORTY_BUTTON_Y].code==KEY_Z);
    assert(remove(HOST_CONFIG_PATH)==0);
    assert(rename("config/before-migration.conf",HOST_CONFIG_PATH)==0);
}

static void check_live_inputs(struct host *host,const char *output_dir)
{
    assert(TWO_FORTY_BUTTON_COUNT==12);
    host->controller_settings=true;host->selected_option=2;
    tap(host,1,KEY_X);assert(host->input_test);
    /* Every keyboard key addresses one SNES button, independent of UI behavior. */
    for(int i=0;i<TWO_FORTY_BUTTON_COUNT;i++) {
        unsigned int key=host->keyboard_bindings[i].code;
        event(host,1,EV_KEY,key,1);update_controller_buttons(host);assert(host->inputs.state.buttons[i]);
        event(host,1,EV_KEY,key,0);update_controller_buttons(host);assert(!host->inputs.state.buttons[i]);
    }
    update_host(host);
    char name[96];host->last_keyboard=true;
    button_label(host,TWO_FORTY_BUTTON_Y,name,sizeof(name));assert(!strcmp(name,"Y"));
    host->last_keyboard=false;
    button_label(host,TWO_FORTY_BUTTON_Y,name,sizeof(name));assert(!strcmp(name,"Y"));
    event(host,0,EV_KEY,BTN_SOUTH,1);event(host,1,EV_KEY,KEY_R,1);
    event(host,0,EV_KEY,BTN_MODE,1);event(host,1,EV_KEY,KEY_T,1);
    event(host,0,EV_ABS,ABS_HAT0X,-1);update_host(host);
    assert(host->input_test && host->inputs.state.buttons[TWO_FORTY_BUTTON_Y]);
    held_input_names(host,false,name,sizeof(name));assert(strstr(name,"304") && strstr(name,"316") && strstr(name,"AX16NEG"));
    held_input_names(host,true,name,sizeof(name));assert(strstr(name," R") && strstr(name," T"));
    draw_controller_settings(host);
    char output[512];snprintf(output,sizeof(output),"%s/input-test.ppm",output_dir);write_preview(output);
    int cell=(host->api.screen_width-20)/6,x=host->safe_x+10+TWO_FORTY_BUTTON_Y*cell;
    assert(framebuffer[host->safe_y+63][x+2][1]==220);
    assert(framebuffer[host->safe_y+63][x+cell/2][0]==250);
    event(host,1,EV_KEY,KEY_R,0);update_host(host);draw_controller_settings(host);
    assert(host->inputs.state.buttons[TWO_FORTY_BUTTON_Y]);
    assert(framebuffer[host->safe_y+63][x+cell/2][0]==60);
    event(host,0,EV_KEY,BTN_SOUTH,0);event(host,0,EV_KEY,BTN_MODE,0);event(host,1,EV_KEY,KEY_T,0);
    event(host,0,EV_ABS,ABS_HAT0X,0);update_host(host);
    held_input_names(host,false,name,sizeof(name));assert(!strcmp(name,"PAD NONE"));
    held_input_names(host,true,name,sizeof(name));assert(!strcmp(name,"KEY NONE"));
    tap(host,0,BTN_TR2);tap(host,0,BTN_TL2);assert(host->input_test);
    event(host,1,EV_KEY,KEY_R,1);for(int i=0;i<59;i++) update_host(host);assert(host->input_test);
    update_host(host);assert(!host->input_test && host->controller_settings);
    event(host,1,EV_KEY,KEY_R,0);update_host(host);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    char directory[]="/tmp/two-forty-host-test-XXXXXX";assert(mkdtemp(directory));assert(chdir(directory)==0);
    assert(mkdir("config",0700)==0);
    check_settings_shortcuts();
    check_launcher_menu();
    check_transition_gates();
    check_timing_toggle();
    FILE *config=fopen(HOST_CONFIG_PATH,"w");assert(config);
    fputs("boot_game=launcher\n# Keep this comment\nbind_confirm=key:313\nframe_timing=1\n",config);fclose(config);
    static struct host host;
    host.mode.hdisplay=320;host.mode.vdisplay=240;
    load_host_config(&host);update_safe_area(&host);
    assert(!host.frame_timing_enabled);
    assert(host.bindings[TWO_FORTY_BUTTON_B].code==BTN_EAST);
    assert(host.api.screen_width==288 && host.api.screen_height==216);
    host.inputs.count=2;host.inputs.devices[0].controller=true;
    strcpy(host.inputs.devices[0].name,"GP2040");
    for(int axis=ABS_HAT0X;axis<=ABS_HAT0Y;axis++) {
        host.inputs.devices[0].abs_minimums[axis]=-1;host.inputs.devices[0].abs_maximums[axis]=1;
    }
    clear_screen();fill_rect(&host,-10,-10,400,400,255,255,255);
    assert(scissor_x==16 && scissor_y==12 && scissor_w==288 && scissor_h==216);
    /* Every launcher item uses exactly the same confirmation policy. */
    tap(&host,0,BTN_TR2);assert(!host.controller_settings);
    tap(&host,0,BTN_SOUTH);assert(!host.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.settings_menu && !host.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.controller_settings);
    tap(&host,0,BTN_EAST);assert(host.setup.active && !host.setup.keyboard);
    update_host(&host);assert(!host.setup.wait_release);
    /* D-pad must return to neutral, and another event cannot overwrite capture. */
    event(&host,0,EV_ABS,ABS_HAT0X,-1);event(&host,0,EV_KEY,BTN_SOUTH,1);update_host(&host);
    assert(host.setup.step==0 && host.setup.candidate.code==ABS_HAT0X);
    event(&host,0,EV_KEY,BTN_SOUTH,0);update_host(&host);assert(host.setup.step==0);
    event(&host,0,EV_ABS,ABS_HAT0X,0);update_host(&host);assert(host.setup.step==1);
    event(&host,0,EV_ABS,ABS_HAT0X,1);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0X,0);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0Y,-1);event(&host,0,EV_ABS,ABS_HAT0Y,0);update_host(&host);
    event(&host,0,EV_ABS,ABS_HAT0Y,1);event(&host,0,EV_ABS,ABS_HAT0Y,0);update_host(&host);
    assert(host.setup.step==4);
    tap(&host,0,BTN_SOUTH);assert(host.setup.step==5);
    tap(&host,0,BTN_SOUTH);assert(host.setup.step==5 && *host.setup.message);
    tap(&host,0,BTN_TR2);assert(host.setup.step==6);
    const int remaining[]={BTN_NORTH,BTN_C,BTN_WEST,BTN_Z,BTN_EAST};
    for(int i=0;i<5;i++) tap(&host,0,remaining[i]);
    assert(host.setup.step==11 && host.setup.active);
    tap(&host,0,BTN_TL2);assert(!host.setup.active && host.controller_settings);
    assert(host.bindings[TWO_FORTY_BUTTON_B].code==BTN_TR2);
    update_host(&host);
    /* Keyboard setup captures Escape as a mapping; only F1 cancels. */
    host.selected_option=1;tap(&host,1,KEY_X);update_host(&host);
    assert(host.setup.active && host.setup.keyboard);
    const int keyboard[]={KEY_A,KEY_D,KEY_W,KEY_S,KEY_R,KEY_X,KEY_C,KEY_V,KEY_Q,KEY_E,KEY_ENTER,KEY_ESC};
    for(int i=0;i<TWO_FORTY_BUTTON_COUNT;i++) tap(&host,1,keyboard[i]);
    assert(!host.setup.active && host.keyboard_bindings[TWO_FORTY_BUTTON_Y].code==KEY_R);
    update_host(&host);
    /* Keyboard and controller states are independent; quick taps survive polling. */
    event(&host,1,EV_KEY,KEY_R,1);event(&host,1,EV_KEY,KEY_R,0);update_controller_buttons(&host);
    assert(host.inputs.state.button_pressed[TWO_FORTY_BUTTON_Y]);
    event(&host,0,EV_KEY,KEY_ESC,1);assert(!host.inputs.state.pressed[KEY_ESC]);event(&host,0,EV_KEY,KEY_ESC,0);
    update_host(&host);
    tap(&host,1,KEY_X);update_host(&host);tap(&host,1,KEY_Q);tap(&host,1,KEY_F1);
    assert(!host.setup.active && host.keyboard_bindings[TWO_FORTY_BUTTON_LEFT].code==KEY_A);
    update_host(&host);
    struct host reloaded={0};load_host_config(&reloaded);
    assert(reloaded.keyboard_bindings[TWO_FORTY_BUTTON_Y].code==KEY_R);
    assert(reloaded.bindings[TWO_FORTY_BUTTON_B].code==BTN_TR2);
    check_migration();
    check_live_inputs(&host,argv[1]);
    /* A controller-only user can cancel without consuming Start/Select as back. */
    setup_begin(&host.setup,false);update_host(&host);
    event(&host,0,EV_KEY,BTN_EAST,1);event(&host,0,EV_KEY,BTN_SOUTH,1);
    for(int i=0;i<60;i++) update_host(&host);
    assert(!host.setup.active && host.bindings[TWO_FORTY_BUTTON_B].code==BTN_TR2);
    event(&host,0,EV_KEY,BTN_EAST,0);event(&host,0,EV_KEY,BTN_SOUTH,0);update_host(&host);
    /* Failed persistence must not change the live mappings. */
    for(int i=0;i<3;i++)update_host(&host);
    assert(rename(HOST_CONFIG_PATH,"config/saved.conf")==0);assert(mkdir(HOST_CONFIG_PATH,0700)==0);
    setup_begin(&host.setup,true);host.setup.complete=true;
    default_keyboard_bindings(host.setup.pending);update_host(&host);
    assert(!host.setup.active && host.keyboard_bindings[TWO_FORTY_BUTTON_Y].code==KEY_R);
    assert(rmdir(HOST_CONFIG_PATH)==0);assert(rename("config/saved.conf",HOST_CONFIG_PATH)==0);update_host(&host);
    host.controller_settings=false;host.settings_menu=true;host.settings_option=1;
    tap(&host,0,BTN_TR2);assert(host.display_settings);
    tap(&host,1,KEY_D);assert(host.safe_x==17);
    tap(&host,1,KEY_R);assert(!host.display_settings && host.safe_x==16);
    tap(&host,0,BTN_TR2);tap(&host,1,KEY_D);tap(&host,1,KEY_S);tap(&host,1,KEY_D);
    assert(host.safe_x==17 && host.safe_y==13);
    tap(&host,1,KEY_S);tap(&host,1,KEY_D); /* horizontal +1 */
    tap(&host,1,KEY_S);tap(&host,1,KEY_D); /* vertical +1 */
    assert(host.safe_offset_x==1 && host.safe_offset_y==1);
    assert(host.api.screen_width==286 && host.api.screen_height==214);
    fill_rect(&host,0,0,8,8,255,255,255);
    assert(scissor_x==18 && scissor_y==14 && scissor_w==8 && scissor_h==8);
    tap(&host,1,KEY_S);tap(&host,0,BTN_TR2);assert(!host.display_settings);
    load_host_config(&reloaded);assert(reloaded.safe_x==17 && reloaded.safe_y==13);
    assert(reloaded.safe_offset_x==1 && reloaded.safe_offset_y==1);
    tap(&host,0,BTN_TR2);host.display_option=2;tap(&host,1,KEY_A);
    host.display_option=3;tap(&host,1,KEY_A);
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
    const struct two_forty_game_api fake={.update=fake_update,.shutdown=fake_shutdown};
    host.active_game=&host.games[0];host.game_api=&fake;
    event(&host,0,EV_KEY,BTN_SOUTH,1);event(&host,0,EV_KEY,BTN_EAST,1);
    for(int i=0;i<65;i++) update_host(&host);
    assert(host.active_game && game_updates==65);
    event(&host,0,EV_KEY,BTN_SOUTH,0);event(&host,0,EV_KEY,BTN_EAST,0);
    host.active_game=NULL;host.game_api=NULL;
    /* Render fixtures at the maximum margins as well as the default. */
    host.safe_offset_x=host.safe_offset_y=0;
    host.safe_x=32;host.safe_y=24;update_safe_area(&host);host.game_count=6;host.selected_game=7;
    for(int i=0;i<6;i++) snprintf(host.games[i].name,sizeof(host.games[i].name),"GAME %d",i+1);
    draw_launcher(&host);char output[512];snprintf(output,sizeof(output),"%s/launcher.ppm",argv[1]);write_preview(output);
    host.settings_message="";host.selected_option=2;
    draw_controller_settings(&host);snprintf(output,sizeof(output),"%s/input-settings.ppm",argv[1]);write_preview(output);
    setup_begin(&host.setup,false);host.setup.step=10;host.setup.wait_release=false;
    memcpy(host.setup.pending,host.bindings,sizeof(host.bindings));
    draw_controller_settings(&host);snprintf(output,sizeof(output),"%s/button-setup.ppm",argv[1]);write_preview(output);
    host.settings_message="";
    draw_display_settings(&host);snprintf(output,sizeof(output),"%s/display-area.ppm",argv[1]);write_preview(output);
    host.setup.active=false;host.controller_settings=false;
    draw_settings_menu(&host);snprintf(output,sizeof(output),"%s/settings.ppm",argv[1]);write_preview(output);
    check_frame_timing(&host,argv[1]);
    assert(remove(HOST_CONFIG_PATH)==0);assert(rmdir("config")==0);
    assert(remove("run/status.json")==0);assert(rmdir("run")==0);
    assert(chdir("/tmp")==0);assert(rmdir(directory)==0);
    puts("Host: SNES buttons, migration, live controller/keyboard indicators, test mode, viewport, setup and persistence passed.");
}
