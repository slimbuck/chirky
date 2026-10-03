#include "game_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct chirky_game_api *chirky_game_entry(void);
static unsigned int draws;
static struct colour pixels[240][320];
static bool saw_level_title,saw_lives,saw_time,saw_old_level,saw_old_signal,saw_pickup_multiplier;
static bool saw_hud_panel,saw_intro_panel;
static char save_keys[CONTENT_LIMIT][96],save_bytes[CONTENT_LIMIT][256];
static size_t save_sizes[CONTENT_LIMIT];
static size_t read_score(void *unused,const char *game,const char *key,void *data,size_t capacity)
{
    (void)unused;assert(!strcmp(game,"phosphor-run"));
    for(int i=0;i<CONTENT_LIMIT;i++)if(!strcmp(save_keys[i],key)){
        assert(save_sizes[i]<=capacity);memcpy(data,save_bytes[i],save_sizes[i]);return save_sizes[i];
    }
    return 0;
}
static bool write_score(void *unused,const char *game,const char *key,const void *data,size_t size)
{
    (void)unused;assert(!strcmp(game,"phosphor-run") && size<256);
    for(int i=0;i<CONTENT_LIMIT;i++)if(!save_keys[i][0] || !strcmp(save_keys[i],key)){
        snprintf(save_keys[i],sizeof(save_keys[i]),"%s",key);memcpy(save_bytes[i],data,size);save_sizes[i]=size;return true;
    }
    return false;
}
static void rectangle(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)ctx; draws++;
    if (x==4 && w==host->screen_width-8 && h==20) saw_hud_panel=true;
    if (x==12 && w==host->screen_width-24 && h==40) saw_intro_panel=true;
    for (int yy=y;yy<y+h;yy++) for (int xx=x;xx<x+w;xx++)
        if (xx>=0 && xx<320 && yy>=0 && yy<240) pixels[yy][xx]=(struct colour){r,g,b};
}
static unsigned letter_sounds,select_sounds;
static void sound(void *ctx,const char *device,const char *path) {
    (void)ctx;(void)device;
    if(strstr(path,"/ui-tick.wav"))letter_sounds++;
    if(strstr(path,"/ui-select.wav"))select_sounds++;
}
static void text(void *ctx,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    (void)ctx;(void)r;(void)g;(void)b;
    assert(x>=0 && x+(int)strlen(value)*6*scale-scale<=host->screen_width);
    assert(y-6*scale>=0 && y+scale<=host->screen_height);
    if (!strncmp(value,"LEVEL 1 - RELAY SHAFT",21)) saw_level_title=true;
    if (!strncmp(value,"LIVES ",6)) saw_lives=true;
    if (strlen(value)==8 && value[2]==':' && value[5]=='.') saw_time=true;
    if (!strncmp(value,"LV ",3)) saw_old_level=true;
    if (!strncmp(value,"SIGNAL ",7)) saw_old_signal=true;
    if (!strncmp(value,"X ",2)) saw_pickup_multiplier=true;
}

static void neutral(const struct chirky_game_api *api, struct chirky_input *input, int frames)
{
    memset(input,0,sizeof(*input));
    for (int i=0;i<frames;i++) api->update(input);
}

static void press(const struct chirky_game_api *api, struct chirky_input *input,
                  enum chirky_button button)
{
    memset(input,0,sizeof(*input));
    input->buttons[button]=input->button_pressed[button]=true;
    api->update(input);
    neutral(api,input,2);
}

static void finish_intro(const struct chirky_game_api *api, struct chirky_input *input)
{
    int guard=LEVEL_INTRO_TICKS+2;
    while (phase==PHASE_LEVEL_INTRO && guard-->0) neutral(api,input,1);
    assert(phase==PHASE_PLAY);
    neutral(api,input,2);
}

int main(void)
{
    struct chirky_host_api host_api={.abi_version=CHIRKY_ABI_VERSION,
        .screen_width=320,.screen_height=240,.fill_rect=rectangle,.play_sound=sound,.draw_text=text,
        .save_read=read_score,.save_write=write_score};
    const struct chirky_game_api *api=chirky_game_entry();
    struct chirky_input input={0};
    assert(api->init(&host_api,"games/phosphor-run/game.conf"));
    assert(content.level_count==4 && content.sprite_count==20);
    for(int i=0;i<3600;i++)api->update(&input);
    assert(phase==PHASE_TITLE);
    api->render(); assert(draws>0 && draws<320*240);
    press(api,&input,CHIRKY_BUTTON_START);
    assert(current_level==0 && phase==PHASE_LEVEL_INTRO && lives==STARTING_LIVES);
    api->render();
    assert(saw_level_title && saw_lives && saw_time && !saw_old_level &&
           !saw_old_signal && !saw_pickup_multiplier);
    assert(!saw_hud_panel && !saw_intro_panel);
    finish_intro(api,&input);
    unsigned face_tick=player_face.tick;
    neutral(api,&input,1);
    assert(player_face.tick==(face_tick+1)%960);
    player_animation_tick=0;face_tick=player_face.tick;
    neutral(api,&input,1);
    assert(player_face.tick==(face_tick+1)%960); /* Movement timer resets don't reset the face. */
    const struct animation *shard=content_animation(&content,"shard");
    assert(shard && shard->count==3);
    assert(animation_frame(shard,0)==animation_frame(shard,shard->ticks*3));
    assert(animation_frame(shard,0)!=animation_frame(shard,shard->ticks));
    unsigned seen=0;
    for(int x=0;x<32;x++)seen|=1u<<(scenery_animation_tick("shard",x,4)/shard->ticks);
    assert(seen==7u); /* Objects at the same time occupy all three frames. */
    int scenery_before=scenery_animation_tick("shard",3,4),saved_frame=frame_number;
    camera_x+=100;camera_y+=10;
    assert(scenery_animation_tick("shard",3,4)==scenery_before);
    frame_number+=shard->ticks*shard->count;
    assert(scenery_animation_tick("shard",3,4)==scenery_before);frame_number=saved_frame;
    /* Default player is the articulated model; legacy sprites remain available
       only as a fallback. Rendering must not mutate animation or gameplay. */
    assert(robot_ready());
    player_x=150;player_y=150;camera_x=camera_y=0;on_ground=true;
    for(int direction=-1;direction<=1;direction+=2) {
        facing=direction;draws=0;int tick=player_animation_tick;struct robot_face saved_face=player_face;api->render();
        assert(draws>0 && draws<5000 && player_animation_tick==tick);
        assert(!memcmp(&saved_face,&player_face,sizeof(player_face)));
    }
    for (int stage=0;stage<content.level_count;stage++) {
        if (phase==PHASE_LEVEL_INTRO) finish_intro(api,&input);
        int expected_rank=0,expected_ticks=stage*120+61;
        if (stage==1) {
            for (int rank=0;rank<HIGH_SCORE_COUNT;rank++) {
                high_scores[stage][rank].ticks=(rank+1)*100;
                strcpy(high_scores[stage][rank].initials,"OLD");
            }
            expected_ticks=250; expected_rank=2;
        } else if (stage==2) {
            for (int rank=0;rank<HIGH_SCORE_COUNT;rank++) {
                high_scores[stage][rank].ticks=rank+1;
                strcpy(high_scores[stage][rank].initials,"PRO");
            }
            expected_ticks=301; expected_rank=-1;
        }
        int expected_shards=0, exit_x=0,exit_y=0;
        for (int y=0;y<level.height;y++) for (int x=0;x<level.width;x++) {
            expected_shards+=tile_at(x,y)=='o';
            if (tile_at(x,y)=='E') {exit_x=x;exit_y=y;}
        }
        assert(level.total_shards==expected_shards+collected_shards);
        collected_shards=level.total_shards;
        level_ticks=expected_ticks-1;
        player_x=exit_x*TILE; player_y=exit_y*TILE;
        velocity_x=velocity_y=0;
        api->update(&input);
        assert(completed_ticks==expected_ticks && score_rank==expected_rank);
        api->render();
        if (expected_rank>=0) {
            assert(phase==PHASE_INITIALS);
            neutral(api,&input,2);
            unsigned edits=letter_sounds,selections=select_sounds;
            press(api,&input,CHIRKY_BUTTON_LEFT);press(api,&input,CHIRKY_BUTTON_RIGHT);
            press(api,&input,CHIRKY_BUTTON_SECONDARY);
            assert(initial_cursor==0 && letter_sounds==edits && select_sounds==selections);
            /* A touch slide can create diagonal edges; it edits only this letter. */
            input.buttons[CHIRKY_BUTTON_UP]=input.button_pressed[CHIRKY_BUTTON_UP]=true;
            input.buttons[CHIRKY_BUTTON_RIGHT]=input.button_pressed[CHIRKY_BUTTON_RIGHT]=true;
            api->update(&input);neutral(api,&input,2);
            assert(initial_cursor==0 && !strcmp(score_initials,"BAA") && letter_sounds==edits+1);
            press(api,&input,CHIRKY_BUTTON_DOWN);
            assert(letter_sounds==edits+2 && !strcmp(score_initials,"AAA"));
            press(api,&input,CHIRKY_BUTTON_PRIMARY);
            assert(initial_cursor==1 && initials_blink<3 && select_sounds==selections+1);
            input.buttons[CHIRKY_BUTTON_PRIMARY]=true;
            for(int held=0;held<10;held++)api->update(&input);
            assert(initial_cursor==1 && select_sounds==selections+1);
            neutral(api,&input,2);press(api,&input,CHIRKY_BUTTON_SECONDARY);
            assert(initial_cursor==0 && initials_blink<3 && select_sounds==selections+2);
            neutral(api,&input,30);assert(initials_blink>=30);
            press(api,&input,CHIRKY_BUTTON_UP);assert(initials_blink<3);
            press(api,&input,CHIRKY_BUTTON_DOWN);assert(initials_blink<3);
            if (stage==0) press(api,&input,CHIRKY_BUTTON_UP);
            press(api,&input,CHIRKY_BUTTON_PRIMARY);
            press(api,&input,CHIRKY_BUTTON_PRIMARY);
            if (stage==0) press(api,&input,CHIRKY_BUTTON_DOWN);
            press(api,&input,CHIRKY_BUTTON_PRIMARY);
            assert(high_scores[stage][expected_rank].ticks==completed_ticks);
            assert(!score_save_failed);
            assert(!strcmp(high_scores[stage][expected_rank].initials,stage==0?"BAZ":"AAA"));
            if (stage==1) assert(high_scores[stage][3].ticks==300);
        }
        assert(phase==PHASE_WIN);
        if (stage==2) assert(high_scores[stage][9].ticks==10);
        input.buttons[CHIRKY_BUTTON_PRIMARY]=true;
        for(int i=0;i<3;i++)api->update(&input);
        assert(phase==PHASE_WIN && current_level==stage);
        neutral(api,&input,2);
        press(api,&input,CHIRKY_BUTTON_PRIMARY);
        assert(phase==PHASE_LEVEL_INTRO && collected_shards==0);
        assert(current_level==(stage+1)%content.level_count);
        assert(lives==STARTING_LIVES);
        assert(player_x==level.initial_x && player_y==level.initial_y);
        assert(!on_ground && !touching_left && !touching_right && dash_available);
        assert(camera_x>=0 && camera_y>=0);
    }
    finish_intro(api,&input);
    for (int lost=1;lost<=STARTING_LIVES;lost++) {
        press(api,&input,CHIRKY_BUTTON_START);
        assert(phase==PHASE_DEAD && lives==STARTING_LIVES-lost);
        neutral(api,&input,34);
        assert(phase==((lost==STARTING_LIVES)?PHASE_GAME_OVER:PHASE_PLAY));
        neutral(api,&input,2);
    }
    press(api,&input,CHIRKY_BUTTON_PRIMARY);
    assert(phase==PHASE_TITLE);
    settings.start_level=1;
    press(api,&input,CHIRKY_BUTTON_PRIMARY);
    assert(current_level==1 && phase==PHASE_LEVEL_INTRO && lives==STARTING_LIVES);
    phase=PHASE_LEVEL_INTRO;level_intro_timer=LEVEL_INTRO_TICKS;
    assert(level_intro_zoom()==4);float last_zoom=4;
    level_intro_timer=LEVEL_INTRO_ZOOM_TICKS;
    assert(level_intro_zoom()==4); /* The full hold leaves the camera still. */
    level_intro_timer=LEVEL_INTRO_ZOOM_TICKS*3/4;
    assert(level_intro_zoom()>3.6f); /* Slower departure than a linear tween. */
    level_intro_timer=LEVEL_INTRO_ZOOM_TICKS/4;
    assert(level_intro_zoom()<1.5f); /* Most travel completes before settling. */
    for(int i=LEVEL_INTRO_TICKS;i>=0;i--) {
        level_intro_timer=i;float z=level_intro_zoom();assert(z>=1 && z<=last_zoom);last_zoom=z;
    }
    assert(last_zoom==1);phase=PHASE_PLAY;assert(level_intro_zoom()==1);
    /* All UI remains inside the safe viewport; the world uses its own camera. */
    const int sizes[][2]={{288,216},{256,192}};
    for(int i=0;i<2;i++) {
        host_api.screen_width=sizes[i][0];host_api.screen_height=sizes[i][1];
        phase=PHASE_TITLE;api->render();phase=PHASE_LEVEL_INTRO;api->render();
        phase=PHASE_PLAY;api->render();phase=PHASE_DEAD;api->render();
        phase=PHASE_INITIALS;api->render();phase=PHASE_WIN;api->render();
        phase=PHASE_GAME_OVER;api->render();
        /* A jump above the map remains below the HUD despite camera lag. */
        memset(&input,0,sizeof(input));input.buttons[CHIRKY_BUTTON_PRIMARY]=true;
        phase=PHASE_PLAY;player_x=level.width*TILE/2;player_y=level.height*TILE+8;
        velocity_x=0;velocity_y=settings.jump_speed;dash_timer=0;
        on_ground=touching_left=touching_right=false;
        float old_ceiling=fmax_zero(level.height*TILE-host_api.screen_height);
        camera_y=old_ceiling;
        for(int frame=0;frame<20;frame++) {
            api->update(&input);
            assert(player_y+PLAYER_HEIGHT-camera_y<=host_api.screen_height-40+.001f);
            assert(camera_y>=0 && phase==PHASE_PLAY);
        }
        assert(camera_y>old_ceiling);api->render();
    }
    api->shutdown(); api->shutdown();
    assert(api->init(&host_api,"games/phosphor-run/game.conf"));
    assert(high_scores[0][0].ticks==61 && !strcmp(high_scores[0][0].initials,"BAZ"));
    assert(high_scores[1][2].ticks==250 && !strcmp(high_scores[1][2].initials,"AAA"));
    const char broken[]="CHIRKY-SCORES-1\n-1 BAD\n";
    assert(write_score(NULL,"phosphor-run","scores-relay-shaft",broken,sizeof(broken)-1));
    api->shutdown();assert(api->init(&host_api,"games/phosphor-run/game.conf"));
    assert(high_scores[0][0].ticks==0 && high_scores[1][2].ticks==250);api->shutdown();
    FILE *file=fopen("/tmp/phosphor-invalid.sprite","w"); assert(file);
    fputs("# ticks=0\ncc\n---\nc\n",file); fclose(file);
    struct animation invalid={0};
    assert(!animation_load("/tmp/phosphor-invalid.sprite",&invalid));
    assert(!invalid.count); remove("/tmp/phosphor-invalid.sprite");
    puts("Runtime: lives, timing, initials, campaign transitions, replay, selected start and cleanup passed.");
}
