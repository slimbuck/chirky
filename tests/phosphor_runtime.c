#include "game_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct chirky_game_api *chirky_game_entry(void);
static unsigned int draws;
static struct colour pixels[240][320];
static void rectangle(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    (void)ctx; draws++;
    for (int yy=y;yy<y+h;yy++) for (int xx=x;xx<x+w;xx++)
        if (xx>=0 && xx<320 && yy>=0 && yy<240) pixels[yy][xx]=(struct colour){r,g,b};
}
static void sound(void *ctx,const char *device,const char *path) { (void)ctx;(void)device;(void)path; }
static void text(void *ctx,int x,int y,const char *value,int scale,unsigned char r,unsigned char g,unsigned char b)
{
    (void)ctx;(void)r;(void)g;(void)b;
    assert(x>=0 && x+(int)strlen(value)*6*scale-scale<=host->screen_width);
    assert(y-6*scale>=0 && y+scale<=host->screen_height);
}

int main(void)
{
    struct chirky_host_api host_api={.abi_version=CHIRKY_ABI_VERSION,
        .screen_width=320,.screen_height=240,.fill_rect=rectangle,.play_sound=sound,.draw_text=text};
    const struct chirky_game_api *api=chirky_game_entry();
    struct chirky_input input={0};
    assert(api->init(&host_api,"games/phosphor-run/game.conf"));
    assert(content.level_count==4 && content.sprite_count==20);
    for(int i=0;i<3600;i++)api->update(&input);
    assert(phase==PHASE_TITLE);
    api->render(); assert(draws>0 && draws<320*240);
    input.button_pressed[CHIRKY_BUTTON_START]=true; api->update(&input); input.button_pressed[CHIRKY_BUTTON_START]=false;
    assert(current_level==0 && phase==PHASE_PLAY);
    const struct animation *shard=content_animation(&content,"shard");
    assert(shard && shard->count==3);
    assert(animation_frame(shard,0)==animation_frame(shard,shard->ticks*3));
    assert(animation_frame(shard,0)!=animation_frame(shard,shard->ticks));
    /* Pixel-equivalence check for merged rectangles and mirrored player art. */
    const struct grid *idle=animation_frame(content_animation(&content,"player-idle"),0);
    player_x=150;player_y=150;camera_x=camera_y=0;on_ground=true;
    for (int direction=-1;direction<=1;direction+=2) {
        facing=direction;api->render();
        for (int y=0;y<idle->height;y++) for (int x=0;x<idle->width;x++) {
            char c=idle->pixels[y*idle->width+(direction<0?idle->width-1-x:x)];
            if (c=='.') continue;
            struct colour expected=c=='n'?settings.deep:c=='s'?settings.platform:
                c=='c'?settings.edge:c=='a'?settings.amber:c=='w'?settings.paper:settings.hazard;
            struct colour actual=pixels[150+idle->height-1-y][150+x];
            assert(actual.r==expected.r && actual.g==expected.g && actual.b==expected.b);
        }
    }
    deaths=7;
    for (int stage=0;stage<content.level_count;stage++) {
        api->update(&input);api->update(&input);
        int expected_shards=0, exit_x=0,exit_y=0;
        for (int y=0;y<level.height;y++) for (int x=0;x<level.width;x++) {
            expected_shards+=tile_at(x,y)=='o';
            if (tile_at(x,y)=='E') {exit_x=x;exit_y=y;}
        }
        assert(level.total_shards==expected_shards+collected_shards);
        collected_shards=level.total_shards;
        player_x=exit_x*TILE; player_y=exit_y*TILE;
        velocity_x=velocity_y=0;
        api->update(&input); assert(phase==PHASE_WIN);
        api->render();
        input.buttons[CHIRKY_BUTTON_B]=true;
        for(int i=0;i<3;i++)api->update(&input);
        assert(phase==PHASE_WIN && current_level==stage);
        input.buttons[CHIRKY_BUTTON_B]=false;
        api->update(&input);api->update(&input);
        input.button_pressed[CHIRKY_BUTTON_B]=true;
        api->update(&input); input.button_pressed[CHIRKY_BUTTON_B]=false;
        assert(phase==PHASE_PLAY && collected_shards==0);
        assert(current_level==(stage+1)%content.level_count);
        assert(deaths==(stage==content.level_count-1?0:7));
        assert(player_x==level.initial_x && player_y==level.initial_y);
        assert(!on_ground && !touching_left && !touching_right && dash_available);
        assert(camera_x>=0 && camera_y>=0);
    }
    settings.start_level=1;
    api->update(&input);api->update(&input);
    phase=PHASE_TITLE; input.button_pressed[CHIRKY_BUTTON_B]=true; api->update(&input);
    assert(current_level==1);
    /* All UI remains inside the safe viewport; the world uses its own camera. */
    const int sizes[][2]={{288,216},{256,192}};
    for(int i=0;i<2;i++) {
        host_api.screen_width=sizes[i][0];host_api.screen_height=sizes[i][1];
        phase=PHASE_TITLE;api->render();phase=PHASE_PLAY;api->render();
        phase=PHASE_DEAD;api->render();phase=PHASE_WIN;api->render();
        /* A jump above the map remains below the HUD despite camera lag. */
        memset(&input,0,sizeof(input));input.buttons[CHIRKY_BUTTON_B]=true;
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
    assert(api->init(&host_api,"games/phosphor-run/game.conf")); api->shutdown();
    FILE *file=fopen("/tmp/phosphor-invalid.sprite","w"); assert(file);
    fputs("# ticks=0\ncc\n---\nc\n",file); fclose(file);
    struct animation invalid={0};
    assert(!animation_load("/tmp/phosphor-invalid.sprite",&invalid));
    assert(!invalid.count); remove("/tmp/phosphor-invalid.sprite");
    puts("Runtime: campaign transitions, replay, selected start, animation timing and cleanup passed.");
}
