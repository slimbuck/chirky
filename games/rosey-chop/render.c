#include "game_state.h"
#include "splash_art.h"
#include <stdio.h>
#include <string.h>

static struct splash_art title_art;
void title_art_load(const char *config) { splash_load(&title_art,config); }
void title_art_free(void) { splash_free(&title_art); }

static int camera_x, camera_y;
/* Art uses top-down coordinates; the CRT host's origin is bottom-left. */
static void rect(int x, int y, int w, int h, unsigned int c)
{
    host->fill_rect(host->context, x, host->screen_height-y-h, w, h, c>>16, c>>8, c);
}
static void world(int x, int y, int w, int h, unsigned int c)
{
    x -= camera_x; y += 28-camera_y;
    if (x+w <= 0 || x >= host->screen_width || y+h <= 28 || y >= host->screen_height-14) return;
    rect(x, y, w, h, c);
}
static void text(int x, int y, const char *s, int scale, unsigned int c)
{
    host->draw_text(host->context, x, host->screen_height-y-scale, s, scale, c>>16, c>>8, c);
}
static void center(int y, const char *s, int scale, unsigned int c)
{
    text((host->screen_width-((int)strlen(s)*6-1)*scale)/2, y, s, scale, c);
}

static void rose_art(int x, int y, char kind, bool cut)
{
    world(x-6, y-1, 13, 3, 0x234f40);
    if (cut) {
        world(x-1,y-4,2,5,0x897864); world(x-3,y-1,2,1,0xc2ae80);
        return;
    }
    bool dead = kind == 'd';
    world(x-1,y-10,2,11,dead ? 0x777167 : 0x8fa95b);
    world(x-6,y-6,5,3,dead ? 0x4e4c43 : 0x427547);
    world(x+1,y-8,5,3,dead ? 0x5d5849 : 0x6da353);
    world(x-5,y-6,3,1,dead ? 0x858071 : 0xb0c675);
    unsigned int dark = dead ? 0x101320 : kind == 'r' ? 0x9b3055 : kind == 'p' ? 0xa85183 : kind == 'a' ? 0xb55f43 : 0xaa9995;
    unsigned int mid = dead ? 0x292838 : kind == 'r' ? 0xda4f6a : kind == 'p' ? 0xe591ba : kind == 'a' ? 0xedaa60 : 0xe9d7b8;
    unsigned int light = dead ? 0x514656 : kind == 'r' ? 0xff9a9b : kind == 'p' ? 0xffc5d6 : kind == 'a' ? 0xffd986 : 0xfff3d6;
    int sway = !dead && (garden.tick/40+x/16)%5 == 0 ? 1 : 0;
    x += sway;
    world(x-5,y-17,10,11,dark); world(x-7,y-14,14,6,dark);
    world(x-4,y-17,7,2,mid); world(x-6,y-14,4,5,mid);
    world(x+2,y-13,4,5,mid); world(x-3,y-9,6,3,mid);
    world(x-3,y-15,4,2,light); world(x-2,y-13,5,3,dark);
    world(x,y-12,3,2,light); world(x-2,y-10,2,1,light);
    if (dead) { world(x+5,y-7,2,2,0x292838); world(x-6,y-3,2,1,0x514656); }
}

static void gardener(void)
{
    int x = (int)garden.x, feet = (int)garden.y, y = feet-(int)garden.z;
    int step = garden.moving && !garden.z ? (garden.tick/6)%2 : 0;
    world(x-5,feet-1,11,3,0x23483b);
    world(x-4,y-4-step,3,5,0x363448); world(x+2,y-4+step,3,5,0x363448);
    world(x-5,y-11,11,7,0xa84267); world(x-3,y-10,7,5,0xdb6e8a);
    world(x-2,y-8,5,4,0xf5d7ac);
    world(x-5,y-17,10,7,0x654139); world(x-3,y-16,7,6,0xf3be95);
    world(x+(garden.facing > 0 ? 2 : -2),y-14,1,2,0x363448);
    world(x-5,y-21,10,5,0xe9bf7a); world(x-8,y-17,16,2,0xf8d899);
    world(x-4,y-18,9,1,0xa85b64); world(x-6,y-19,3,2,0xe17d99);
    int side = garden.facing;
    world(x+side*7-1,y-9,3,6,0x865b43);
    world(x+side*8-3,y-11,6,3,0xd5e5d0);
    if (garden.chop > 7) {
        /* A readable all-around sweep matches the circular hit area. */
        int reach = 22-(20-garden.chop)/2;
        world(x-reach,y-10,2,9,0xffedc6); world(x+reach-1,y-10,2,9,0xffedc6);
        world(x-reach+3,y-15,8,2,0xffc99b); world(x+reach-10,y-15,8,2,0xffc99b);
        world(x-8,y+4,16,2,0xffedc6);
    }
}

static void wasp_art(void)
{
    int x = (int)garden.wasp_x, y = (int)garden.wasp_y;
    world(x-5,y,10,3,0x254839);
    int flap = (garden.tick/3)%2;
    world(x-7,y-13-flap*2,6,4,0xd2e9df); world(x+1,y-13-flap*2,6,4,0xf3f4d6);
    world(x-6,y-10,12,6,0x262435); world(x-4,y-10,3,6,0xf2c464);
    world(x+1,y-10,3,6,0xf2c464); world(x+5,y-9,2,2,0xfff0ba);
    world(x-8,y-7,2,1,0xf0dcb9);
}

static void scenery(void)
{
    rect(0,28,host->screen_width,host->screen_height-42,0x386b4e);
    for (int row = 0; row < GARDEN_ROWS; row++) for (int col = 0; col < GARDEN_COLS; col++) {
        int x = col*TILE, y = row*TILE;
        if ((row+col)%2) world(x,y,TILE,TILE,0x3b7051);
        if (col == 0 || col == 23 || row == 0 || row == 15) {
            world(x,y,16,16,0x264f40); world(x+1,y+2,13,9,0x42754c);
            world(x+3,y+2,6,2,0x699253);
        } else if (col == 11 || col == 12 || row == 7 || row == 8) {
            world(x,y,16,16,0xb7a57d); world(x+1,y+2,13,11,0xc9b98e);
            world(x+3,y+3,8,1,0xdfcda0); world(x+12,y+12,2,1,0x998d6f);
        } else {
            world(x+3,y+9,1,3,0x699057); world(x+4,y+10,2,1,0x699057);
            if ((row*7+col)%9 == 0) world(x+11,y+4,2,2,0xd5c58e);
        }
    }
    /* Small lily fountain at the crossing. It is decorative and walkable. */
    world(180,115,25,24,0x897f70); world(178,119,29,16,0xe0cca0);
    world(181,118,23,18,0x568e8e); world(184,120,17,13,0x79b8aa);
    world(185+(garden.tick/18)%5,123,9,1,0xc8e5c8);
    world(193,130,6,3,0x477c55); world(196,128,2,2,0xf5bac4);
    for (int row = 0; row < GARDEN_ROWS; row++) {
        for (int i = 0; i < garden.rose_count; i++) {
            const struct rose *r = &garden.roses[i];
            if (r->y/TILE == row) rose_art(r->x,r->y,r->kind,r->cut);
        }
        if ((int)garden.y/TILE == row) gardener();
    }
    for (int i = 0; i < MAX_PETALS; i++) {
        const struct petal *p = &garden.petals[i];
        if (p->life) world((int)p->x,(int)p->y,2,2,p->colour);
    }
    if (garden.wasp_life) wasp_art();
}

static void hud(void)
{
    int w = host->screen_width, h = host->screen_height;
    char label[48];
    rect(0,0,w,28,0x203d36); rect(0,26,w,2,0x8aa575);
    text(6,5,"ROSEY CHOP",1,0xf7d6b4);
    snprintf(label,sizeof(label),"DEAD %02d",garden.remaining);
    text(6,16,label,1,0xc6d5ad);
    int seconds = (garden.storm_ticks-garden.elapsed+59)/60;
    snprintf(label,sizeof(label),"RAIN %02d",seconds);
    text(w-49,5,label,1,seconds <= 10 ? 0xff9b9b : 0xf7d6b4);
    rect(w-95,17,89,4,0x142b29);
    rect(w-95,17,89*(garden.storm_ticks-garden.elapsed)/garden.storm_ticks,4,seconds <= 10 ? 0xdb7790 : 0x95bca8);
    rect(0,h-14,w,14,0x203d36);
    const char *status = garden.warning ? "WASP INCOMING - KEEP MOVING" : garden.wasp_life ? "WASP CHASING - JUMP TO DODGE" : "LEVEL 1 / THE ROSE GARDEN";
    center(h-10,status,1,garden.warning || garden.wasp_life ? 0xffd483 : 0xa9c49e);
    /* Compact map: every remaining dead rose, the player, and the wasp. */
    int mx = w-53, my = h-53;
    rect(mx-2,my-2,52,36,0x203d36); rect(mx,my,48,32,0x54785b);
    for (int i = 0; i < garden.rose_count; i++) {
        const struct rose *r = &garden.roses[i];
        if (r->kind == 'd' && !r->cut) rect(mx+r->x/8-1,my+r->y/8-1,2,2,0x171828);
    }
    if (garden.wasp_life) rect(mx+(int)garden.wasp_x/8-1,my+(int)garden.wasp_y/8-1,2,2,0xffd483);
    rect(mx+(int)garden.x/8-1,my+(int)garden.y/8-1,2,2,0xffd4c3);
}

static void action_line(int y, enum chirky_button action, const char *verb)
{
    char key[24], line[64];
    if (host->button_label) host->button_label(host->context,action,key,sizeof(key));
    else snprintf(key,sizeof(key),"%s",action == CHIRKY_BUTTON_Y ? "Y" : "B");
    int key_width = (host->screen_width-48)/6-3-(int)strlen(verb);
    if (key_width < 1) key_width = 1;
    if (key_width > 22) key_width = 22;
    snprintf(line,sizeof(line),"%.*s / %s",key_width,key,verb);
    center(y,line,1,0xf7d6b4);
}

static void overlay(void)
{
    int w = host->screen_width, h = host->screen_height;
    int top = (h-156)/2;
    rect(15,top+3,w-26,156,0x172d2a); rect(12,top,w-24,156,0xefd8ac);
    rect(14,top+2,w-28,152,0x294b3c); rect(18,top+6,w-36,144,0x203d36);
    if (garden.phase == TITLE) {
        center(top+14,"ROSEY CHOP",2,0xffb8c6);
        center(top+37,"1 / THE ROSE GARDEN",1,0xaacb9f);
        center(top+55,"CHOP EVERY BLACK ROSE",1,0xffedcc);
        center(top+68,"BEFORE THE RAIN ARRIVES",1,0xffedcc);
        center(top+84,"ONE WASP STING ENDS THE RUN",1,0xe9b76a);
        action_line(top+101,CHIRKY_BUTTON_B,"CHOP / HOLD TO SWEEP");
        action_line(top+113,CHIRKY_BUTTON_Y,"JUMP TO DODGE");
        center(top+125,"SELECT - PAUSE",1,0xaacb9f);
        center(top+139,"PRESS A BUTTON TO BEGIN",1,0xffedcc);
    } else {
        bool won = garden.phase == WON;
        center(top+18,won ? "GARDEN SAVED" : "GAME OVER",2,won ? 0xffb8c6 : 0xe9b76a);
        center(top+48,won ? "EVERY DEAD ROSE IS GONE" : garden.phase == STUNG ? "THE WASP STUNG YOU" : "THE RAINSTORM HAS ARRIVED",1,0xffedcc);
        char line[48];
        snprintf(line,sizeof(line),"%d / %d BLACK ROSES CHOPPED",garden.total-garden.remaining,garden.total);
        center(top+68,line,1,0xaacb9f);
        if (won) snprintf(line,sizeof(line),"TIME %d.%02d SECONDS",garden.elapsed/60,(garden.elapsed%60)*100/60);
        else snprintf(line,sizeof(line),"%s",garden.phase == STUNG ? "KEEP MOVING AND TIME YOUR JUMP" : "HOLD CHOP AS YOU RUN PAST");
        center(top+87,line,1,0xf7d6b4);
        center(top+109,won ? "LEVEL 1 COMPLETE" : "THE GARDEN NEEDS YOU",1,0xffb8c6);
        action_line(top+135,CHIRKY_BUTTON_B,"PLAY AGAIN");
    }
}

void garden_render(void)
{
    if (garden.phase==TITLE && splash_draw(&title_art,host)) {
        int top=host->screen_height-57;
        rect(0,top,host->screen_width,57,0x203d36);
        center(top+4,"CHOP BLACK ROSES BEFORE THE RAIN",1,0xffedcc);
        center(top+15,"DODGE THE WASP - ONE STING ENDS IT",1,0xe9b76a);
        center(top+27,"D-PAD MOVE / HOLD B CHOP / Y JUMP",1,0xaacb9f);
        center(top+38,"SELECT - PAUSE",1,0xaacb9f);
        center(top+49,"B - BEGIN",1,0xffb8c6);
        return;
    }
    int view_h = host->screen_height-42;
    camera_x = (int)clamp_value(garden.x-host->screen_width/2,0,WORLD_W-host->screen_width > 0 ? WORLD_W-host->screen_width : 0);
    camera_y = (int)clamp_value(garden.y-view_h/2,0,WORLD_H-view_h > 0 ? WORLD_H-view_h : 0);
    scenery();
    if (garden.elapsed > garden.storm_ticks-15*60 || garden.phase == STORM) {
        int drops = garden.phase == STORM ? 65 : 14;
        for (int i = 0; i < drops; i++) {
            int x = (i*47+garden.tick*2)%host->screen_width;
            int y = 28+(i*31+garden.tick*5)%view_h;
            rect(x,y,1,5,0x93b5ba);
        }
    }
    hud();
    if (garden.phase != PLAY) overlay();
}
