#include <assert.h>
#include "../games/circuit-clash/game.c"
#include "local_input.h"

static unsigned rectangles,texts,sprites;
static void sprite(void *ctx,chirky_asset image,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                   unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)ctx;(void)image;(void)x;(void)y;(void)flip;
    assert(w==96 && h==96 && sw==w && sh==h && sy==0 && sx%96==0 && sx<384);
    assert(r==255 && g==255 && b==255 && a==255);sprites++;
}
static void fill(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)x;(void)y;(void)r;(void)g;(void)b;assert(w>=0 && h>=0);rectangles++;}
static void draw(void *ctx,int x,int y,const char *s,int scale,unsigned char r,unsigned char g,unsigned char b)
{(void)ctx;(void)y;(void)r;(void)g;(void)b;assert(x>=0 && x+(int)chirky_text_length(s)*6*scale<=host->screen_width);texts++;}
static struct chirky_input frame;
static struct local_input_bank bank;
static void tick(unsigned a,unsigned b,unsigned c)
{
    frame.device_count=0;
    const unsigned masks[]={a,b,c};
    for(unsigned i=0;i<3;i++)if(masks[i]!=256) {
        struct chirky_device_input *d=local_input_add(&bank,&frame,i+1,i?CHIRKY_DEVICE_CONTROLLER:CHIRKY_DEVICE_KEYBOARD,masks[i],0);
        for(int k=0;k<CHIRKY_BUTTON_COUNT;k++)snprintf(d->labels[k],sizeof(d->labels[k]),"%s",i?"B":"X");
    }
    local_input_finish(&bank,&frame);update(&frame);render();
}
static void clean_simulation(void)
{
    reset_round();memset(controls,0,sizeof(controls));
    countdown=0;release_gate=false;waiting=false;started=true;
    fighters[0].x=100*8;fighters[1].x=140*8;
}
static void movement_and_balance(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));
    struct character_tuning approved=characters[0];
    clean_simulation();
    /* Per-player tuning, acceleration and neutral braking. */
    fighters[0].x=60*8;fighters[1].x=220*8;
    characters[1].walk_speed_fp8=6;
    controls[0].buttons[CHIRKY_BUTTON_RIGHT]=controls[1].buttons[CHIRKY_BUTTON_LEFT]=true;
    for(int i=0;i<10;i++)simulate();
    assert(fighters[0].vx==12 && fighters[1].vx==-6);
    memset(controls,0,sizeof(controls));for(int i=0;i<3;i++)simulate();
    assert(!fighters[0].vx && !fighters[1].vx);characters[1]=approved;
    clean_simulation();
    controls[0].buttons[CHIRKY_BUTTON_DOWN]=true;simulate();
    assert(fighters[0].crouch && fighters[0].transition==3 && body_height(0)==34);
    controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;
    for(int i=0;i<10;i++)simulate();
    assert(fighters[0].x==100*8);
    controls[0].buttons[CHIRKY_BUTTON_DOWN]=false;simulate();assert(fighters[0].transition==3);
    for(int i=0;i<3;i++)simulate();
    assert(fighters[0].x>100*8);
    clean_simulation();
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;
    int peak=0,air_ticks=0;
    for(int i=0;i<65;i++){simulate();if(fighters[0].y>peak)peak=fighters[0].y;if(fighters[0].y)air_ticks++;}
    assert(peak==306 && air_ticks==34 && !fighters[0].y && !fighters[0].vy);
    /* Exact attack windows and one contact, independently of idle timings. */
    clean_simulation();characters[0].idle_0_ticks=1;
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=false;
    assert(fighters[0].attack==1 && !active_attack(0));
    for(int i=0;i<5;i++){simulate();assert(fighters[1].health==100);}
    simulate();assert(active_attack(0) && fighters[1].health==80);
    for(int i=0;i<15;i++)simulate();
    assert(fighters[0].attack==1 && !active_attack(0));
    simulate();assert(!fighters[0].attack && fighters[1].health==80);
    characters[0]=approved;
    /* Guard cancels damage; weight changes received displacement, not gravity. */
    clean_simulation();controls[1].buttons[CHIRKY_BUTTON_RIGHT]=true;
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;
    simulate();controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=false;
    for(int i=0;i<6;i++)simulate();
    assert(fighters[1].health==100 && fighters[1].stun==approved.punch_blockstun_ticks);
    fighters[1].x=140*8;fighters[0].hit=false;simulate();
    assert(fighters[1].health==100 && fighters[1].guard_stun);
    clean_simulation();characters[1].weight_percent=200;
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=false;
    assert(fighters[0].attack==2);
    for(int i=0;i<10;i++)simulate();
    assert(fighters[1].health==75 && fighters[1].x==146*8);characters[1]=approved;
    clean_simulation();fighters[0].x=stage.wall_margin_px*8;fighters[1].x=fighters[0].x;
    simulate();assert(fighters[1].x-fighters[0].x==40*8 && fighters[0].x==stage.wall_margin_px*8);
    fighters[1].y=80*8;fighters[1].x=fighters[0].x;simulate();
    assert(fighters[1].x==fighters[0].x); /* Bodies can cross in the air. */
    artwork[0]=artwork[1]=1;show_boxes=true;render();assert(sprites==2);
    artwork[0]=artwork[1]=0;shutdown();
    /* Join once, start practice, reset, then disconnect/rejoin the solo slot. */
    memset(&bank,0,sizeof(bank));assert(init(api,"games/circuit-clash/game.conf"));
    tick(0,256,256);tick(16,256,256);tick(0,256,256);tick(64,256,256);
    assert(practice && !waiting && players[0]==1 && !players[1]);
    tick(0,256,256);for(int i=0;i<90;i++)tick(0,256,256);
    tick(2,256,256);int moved=fighters[0].x;
    tick(0,256,256);tick(64,256,256);assert(fighters[0].x<moved && release_gate);
    tick(256,256,0);assert(waiting);tick(256,256,16);
    assert(players[0]==3 && !waiting && release_gate);shutdown();
}
static void malformed_tuning(void)
{
    struct character_tuning c={0};
    const char *bad[]={"walk_speed_fp8=0", "gravity_fp8=nan", "typo=5", "health=99",
                       "weight_percent=999999999999999999999999999", "atlas=../../escape",
                       "schema_version=2", "crouching_height_px=64", "kick_bottom_px=40"};
    for(size_t i=0;i<CC_COUNT(bad);i++) {
        FILE *in=fopen("games/circuit-clash/assets/characters/arthur.conf","rb");assert(in);
        FILE *out=fopen("/tmp/chirky-invalid-character.conf","wb");assert(out);
        size_t keylen=(size_t)(strchr(bad[i],'=')-bad[i]);char line[512];bool replaced=false;
        while(fgets(line,sizeof(line),in)) {
            /* health=99 is deliberately appended to test duplicate rejection;
               others replace their field to actually exercise range/type rules. */
            if(i!=3 && !strncmp(line,bad[i],keylen) && line[keylen]=='=') {
                fprintf(out,"%s\n",bad[i]);replaced=true;
            } else fputs(line,out);
        }
        if(!replaced)fprintf(out,"\n%s\n",bad[i]);
        fclose(in);fclose(out);
        assert(!read_character(NULL,"/tmp/chirky-invalid-character.conf",&c));
    }
    FILE *out=fopen("/tmp/chirky-invalid-character.conf","wb");assert(out);
    fputs("schema_version=1\nname=Incomplete\n",out);fclose(out);
    assert(!read_character(NULL,"/tmp/chirky-invalid-character.conf",&c));
    remove("/tmp/chirky-invalid-character.conf");
    assert(!content_id("../arthur") && !content_path("assets/../x"));
}
int main(void)
{
    struct chirky_host_api api={.abi_version=CHIRKY_ABI_VERSION,.screen_width=288,.screen_height=216,.fill_rect=fill,.draw_text=draw,.draw_sprite=sprite};
    assert(init(&api,"games/circuit-clash/game.conf"));
    tick(16,0,0);assert(!players[0]); /* Held launch press cannot join. */
    tick(0,0,0);tick(64,64,0);assert(!players[0] && !players[1]);
    tick(0,0,0);tick(16,0,0);assert(players[0]==1 && !players[1]);
    tick(0,0,0);tick(16,0,0);assert(!players[1]); /* One device cannot claim both. */
    tick(0,16,0);assert(players[1]==2 && !waiting && release_gate);
    tick(0,0,0);for(int i=0;i<90;i++)tick(0,0,0);
    assert(!countdown && !release_gate);
    int ax=fighters[0].x,bx=fighters[1].x;
    tick(2,0,1);assert(fighters[0].x>ax && fighters[1].x==bx); /* Unassigned pad ignored. */
    tick(0,1,0);assert(fighters[1].x<bx);
    tick(4,0,0);assert(fighters[0].jump_wait==3 && !fighters[0].y);
    for(int i=0;i<3;i++)tick(0,0,0);
    assert(fighters[0].y>0 && fighters[1].y==0);
    for(int i=0;i<60;i++)tick(0,0,0);
    /* Move into reach and prove one held punch causes only one hit. */
    while(fighters[1].x-fighters[0].x>44*8)tick(2,1,0);
    tick(16,0,0);for(int i=0;i<60;i++)tick(16,0,0);
    assert(fighters[1].health==80 && fighters[0].health==100);
    tick(0,256,0);assert(waiting);ax=fighters[0].x;
    tick(2,256,0);assert(fighters[0].x==ax && fighters[1].health==80);
    tick(0,256,16);assert(players[1]==3 && !waiting && release_gate);
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    for(int hit=0;hit<4;hit++) {
        while(fighters[1].x-fighters[0].x>44*8)tick(2,256,0);
        tick(16,256,0);for(int i=0;i<30;i++)tick(0,256,0);
    }
    assert(winner==0 && fighters[1].health==0);
    tick(16,256,0);assert(fighters[0].ready && winner==0);
    tick(0,256,16);assert(winner==-1 && fighters[0].health==100 && fighters[1].health==100);
    /* Equal active-frame attacks resolve together, including the final hit. */
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    fighters[0].x=100*8;fighters[1].x=124*8;
    fighters[0].health=fighters[1].health=20;
    tick(16,256,16);for(int i=0;i<6;i++)tick(0,256,0);
    assert(winner==2 && fighters[0].health==0 && fighters[1].health==0);
    shutdown();assert(init(&api,"games/circuit-clash/game.conf") && !players[0] && !players[1]);
    api.screen_width=256;api.screen_height=192;tick(0,0,0);tick(16,16,0);tick(0,0,0);
    assert(rectangles && texts);
    shutdown();api.screen_width=288;api.screen_height=216;
    movement_and_balance(&api);malformed_tuning();
    puts("Circuit Clash: joins, practice, movement, crouch, jump, attack windows, guard, weight, collision, KO, tuning validation and 1:1 sprites passed.");
}
