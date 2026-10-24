#include <assert.h>
#include "../games/circuit-clash/game.c"
#include "local_input.h"

static unsigned rectangles,texts,sprites;
static void sprite(void *ctx,chirky_asset image,int x,int y,int w,int h,int sx,int sy,int sw,int sh,
                   unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool flip)
{
    (void)ctx;(void)image;(void)x;(void)y;(void)flip;
    assert(w==96 && h==96 && sw==w && sh==h && sy%96==0 && sy<CC_ATLAS_HEIGHT && sx%96==0 && sx<CC_ATLAS_WIDTH);
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
    assert(fighters[0].crouch && fighters[0].transition==3 && body_height(0)==47);
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
    assert(peak==312 && air_ticks==22 && !fighters[0].y && !fighters[0].vy);
    /* Exact attack windows and one contact, independently of idle timings. */
    clean_simulation();characters[0].idle_0_ticks=1;
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=false;
    assert(fighters[0].attack==1 && !active_attack(0) && fighter_frame(0)==CC_PUNCH_FIRST);
    for(int i=1;i<approved.punch_startup_ticks;i++) {
        simulate();assert(fighters[1].health==100 && fighter_frame(0)==CC_PUNCH_FIRST);
    }
    simulate();assert(active_attack(0) && fighters[1].health==100-characters[0].punch_damage && fighter_frame(0)==CC_PUNCH_FIRST+1);
    for(int i=1;i<approved.punch_active_ticks;i++) {
        simulate();assert(active_attack(0) && fighter_frame(0)==CC_PUNCH_FIRST+1 && fighters[1].health==100-characters[0].punch_damage);
    }
    for(int i=0;i<approved.punch_recovery_ticks;i++) {
        simulate();assert(!active_attack(0) && fighter_frame(0)==CC_PUNCH_FIRST+(i<approved.punch_followthrough_ticks?1:2));
    }
    assert(fighters[0].attack==1 && !active_attack(0));
    simulate();assert(!fighters[0].attack && fighters[1].health==100-characters[0].punch_damage);
    characters[0]=approved;
    /* Guard cancels damage; weight changes received displacement, not gravity. */
    clean_simulation();controls[1].buttons[CHIRKY_BUTTON_PRIMARY]=controls[1].buttons[CHIRKY_BUTTON_SECONDARY]=true;
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;
    simulate();controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=false;
    for(int i=0;i<approved.punch_startup_ticks;i++)simulate();
    assert(fighters[1].health==100 && fighters[1].stun==approved.punch_blockstun_ticks);
    fighters[1].x=140*8;fighters[0].hit=false;simulate();
    assert(fighters[1].health==100 && fighters[1].guard_stun);
    clean_simulation();characters[1].weight_percent=200;
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=false;
    assert(fighters[0].attack==2);
    for(int i=0;i<approved.kick_startup_ticks;i++)simulate();
    assert(fighters[1].health==100-characters[0].kick_damage && fighters[1].x==146*8);characters[1]=approved;
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
                       "schema_version=4", "crouching_height_px=64", "kick_bottom_px=40",
                       "punch_followthrough_ticks=8", "air_punch_followthrough_ticks=9",
                       "fall_gravity_fp8=0", "jump_buffer_ticks=31", "air_punch_active_ticks=0",
                       "air_kick_startup_ticks=121", "air_attack_landing_ticks=-1"};
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
static void animation_timing(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));
    /* Every held drawing lasts its configured duration in either direction. */
    for(int direction=-1;direction<=1;direction+=2) {
        clean_simulation();fighters[0].x=100*8;fighters[1].x=230*8;
        controls[0].buttons[direction>0?CHIRKY_BUTTON_RIGHT:CHIRKY_BUTTON_LEFT]=true;
        int durations[]={10,4,10,4};
        for(int i=0;i<4;i++) {
            int pose=direction>0?i:(4-i)%4;
            for(int t=0;t<durations[pose];t++) {
                simulate();assert(fighter_frame(0)==CC_WALK_FIRST+pose);
            }
        }
        simulate();assert(fighter_frame(0)==CC_WALK_FIRST);
        memset(controls,0,sizeof(controls));simulate();
        assert(!fighters[0].walk_direction && !fighters[0].walk_tick && fighter_frame(0)<CC_WALK_FIRST);
    }
    clean_simulation();fighters[0].x=stage.wall_margin_px*8;
    controls[0].buttons[CHIRKY_BUTTON_LEFT]=true;
    for(int t=0;t<40;t++){simulate();assert(!fighters[0].walk_direction);}
    /* Actual collision displacement, not just nonzero velocity, drives gait. */
    clean_simulation();fighters[1].x=(host->screen_width-stage.wall_margin_px)*8;
    fighters[0].x=fighters[1].x-40*8;controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;
    for(int t=0;t<40;t++){simulate();assert(!fighters[0].walk_direction);}
    clean_simulation();fighters[1].x=230*8;
    characters[0].walk_0_ticks=1;characters[0].walk_1_ticks=2;
    controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;
    simulate();assert(fighter_frame(0)==CC_WALK_FIRST);
    simulate();assert(fighter_frame(0)==CC_WALK_FIRST+1);
    simulate();assert(fighter_frame(0)==CC_WALK_FIRST+1);
    simulate();assert(fighter_frame(0)==CC_WALK_FIRST+2);
    controls[0].buttons[CHIRKY_BUTTON_RIGHT]=false;controls[0].buttons[CHIRKY_BUTTON_LEFT]=true;
    for(int t=0;t<4;t++)simulate();
    assert(fighters[0].walk_direction==-1);
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;simulate();
    assert(!fighters[0].walk_direction && fighter_frame(0)==CC_PUNCH_FIRST);
    /* Immediate moves skip wind-up; artwork follows arbitrary balance edits. */
    characters[0].punch_startup_ticks=0;characters[0].punch_active_ticks=1;
    fighters[0].action_tick=0;assert(fighter_frame(0)==CC_PUNCH_FIRST+1);
    fighters[0].action_tick=1;assert(fighter_frame(0)==CC_PUNCH_FIRST+1 && !active_attack(0));
    fighters[0].action_tick=1+characters[0].punch_followthrough_ticks;assert(fighter_frame(0)==CC_PUNCH_FIRST+2);
    fighters[0].stun=5;assert(fighter_frame(0)==CC_IDLE_FIRST);
    shutdown();
}
static void jump_response(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));clean_simulation();
    fighters[1].x=230*8;
    controls[0].buttons[CHIRKY_BUTTON_UP]=true;
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;simulate();
    assert(fighters[0].jump_wait==1 && !fighters[0].y && fighter_frame(0)==CC_IDLE_FIRST+2);
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;simulate();
    assert(fighters[0].y==48 && fighter_frame(0)==CC_JUMP_FIRST);
    for(int i=0;i<11;i++)simulate();
    assert(fighters[0].y==312 && !fighters[0].vy && fighter_frame(0)==CC_JUMP_FIRST);
    simulate();assert(fighters[0].vy==-6 && fighter_frame(0)==CC_IDLE_FIRST);
    while(fighters[0].y)simulate();
    assert(fighters[0].landing==3 && fighter_frame(0)==CC_IDLE_FIRST+2);
    for(int i=0;i<2;i++){simulate();assert(fighter_frame(0)==CC_IDLE_FIRST+2);}
    simulate();assert(!fighters[0].landing);
    for(int i=0;i<40;i++)simulate();
    assert(!fighters[0].y && !fighters[0].jump_wait); /* Holding up never auto-hops. */
    /* A fresh press shortly before touchdown survives landing recovery. */
    clean_simulation();fighters[0].y=48;fighters[0].vy=-32;
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;
    for(int i=0;i<4;i++)simulate();
    assert(fighters[0].jump_wait==1 && !fighters[0].jump_buffer);
    simulate();assert(fighters[0].y==48);
    /* An earlier press expires, and a disabled buffer preserves immediate input. */
    clean_simulation();fighters[0].y=312;fighters[0].vy=0;
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;
    for(int i=0;i<30;i++)simulate();
    assert(!fighters[0].y && !fighters[0].jump_wait && !fighters[0].jump_buffer);
    characters[0].jump_buffer_ticks=0;characters[0].jump_startup_ticks=0;
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;simulate();assert(fighters[0].y==48);
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;
    /* Fall gravity is per character and respects the configured terminal speed. */
    characters[0].fall_gravity_fp8=9;characters[0].max_fall_speed_fp8=10;
    fighters[0].y=312;fighters[0].vy=0;simulate();assert(fighters[0].vy==-9);
    simulate();assert(fighters[0].vy==-10);
    shutdown();
}
static void aerial_attacks(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));
    struct fighter arc[30];
    clean_simulation();fighters[0].x=60*8;fighters[1].x=230*8;
    controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;
    controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;
    for(int t=0;t<30;t++) {
        simulate();arc[t]=fighters[0];controls[0].button_pressed[CHIRKY_BUTTON_UP]=false;
    }
    for(int kind=1;kind<=2;kind++) {
        int button=kind==1?CHIRKY_BUTTON_PRIMARY:CHIRKY_BUTTON_SECONDARY;
        int first=kind==1?CC_AIR_PUNCH_FIRST:CC_AIR_KICK_FIRST;
        /* A held attack does not repeat, alter the arc, or brake air steering. */
        clean_simulation();fighters[0].x=60*8;fighters[1].x=230*8;
        controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;
        controls[0].button_pressed[CHIRKY_BUTTON_UP]=true;
        for(int t=0;t<30;t++) {
            if(t==2)controls[0].buttons[button]=controls[0].button_pressed[button]=true;
            simulate();memset(controls[0].button_pressed,0,sizeof(controls[0].button_pressed));
            assert(fighters[0].y==arc[t].y && fighters[0].vy==arc[t].vy);
            if(arc[t].y)assert(fighters[0].x==arc[t].x);
            if(t==2)assert(fighters[0].air_attack && fighters[0].attack==kind);
            if(fighters[0].attack) {
                struct attack_tuning m=move_for(0);int tick=t-2;
                int phase=tick<m.startup?0:tick<m.startup+m.active?1:2;
                bool extended=tick>=m.startup && tick<m.startup+m.active+m.followthrough;
                assert(fighter_frame(0)==(extended?first:CC_JUMP_FIRST) && active_attack(0)==(phase==1));
            }
        }
        assert(!fighters[0].attack && !fighters[0].air_attack);
        /* Rising/falling hits, air-to-air hits, vertical misses and ground guard. */
        for(int scenario=0;scenario<5;scenario++) {
            clean_simulation();fighters[0].y=(scenario>=2?80:10)*8;
            fighters[0].vy=scenario==1?-2:24;
            if(scenario==1)fighters[0].y=30*8;
            if(scenario==2)fighters[1].y=80*8;
            if(scenario==4){fighters[0].y=10*8;controls[1].buttons[CHIRKY_BUTTON_PRIMARY]=controls[1].buttons[CHIRKY_BUTTON_SECONDARY]=true;}
            controls[0].button_pressed[button]=true;simulate();
            controls[0].button_pressed[button]=false;
            struct attack_tuning m=move_for(0);
            for(int t=0;t<m.startup+m.active;t++)simulate();
            assert(fighters[1].health==((scenario==3 || scenario==4)?100:100-m.damage));
            if(scenario==4)assert(fighters[1].guard_stun);
        }
        /* Touchdown cancels startup, contact and recovery before resolving hits. */
        for(int phase=0;phase<3;phase++) {
            clean_simulation();fighters[0].y=8;fighters[0].vy=-8;
            fighters[0].attack=kind;fighters[0].air_attack=true;
            struct attack_tuning m=move_for(0);
            fighters[0].action_tick=phase==0?0:phase==1?m.startup:m.startup+m.active;
            simulate();
            assert(!fighters[0].y && !fighters[0].attack && !fighters[0].air_attack);
            assert(fighters[0].landing==characters[0].air_attack_landing_ticks && fighters[1].health==100);
            for(int t=1;t<characters[0].air_attack_landing_ticks;t++) {
                controls[0].button_pressed[button]=true;simulate();assert(!fighters[0].attack);
            }
            simulate();assert(fighters[0].attack==kind && !fighters[0].air_attack);
        }
        /* Hitstun prevents new air attacks; an anti-air hit interrupts one. */
        clean_simulation();fighters[0].y=10*8;fighters[0].stun=5;
        controls[0].button_pressed[button]=true;simulate();assert(!fighters[0].attack);
        clean_simulation();fighters[0].y=10*8;
        controls[0].button_pressed[button]=true;
        fighters[1].attack=1;fighters[1].action_tick=characters[1].punch_startup_ticks-1;
        simulate();assert(fighters[0].stun && !fighters[0].attack && !fighters[0].air_attack);
    }
    /* Each slot owns air timings; zero startup also selects the strike drawing. */
    clean_simulation();fighters[0].x=60*8;fighters[1].x=230*8;
    characters[1].air_kick_startup_ticks=0;characters[1].air_kick_active_ticks=1;
    for(int p=0;p<2;p++){fighters[p].y=80*8;controls[p].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;}
    simulate();assert(!active_attack(0) && active_attack(1));
    assert(fighter_frame(0)==CC_JUMP_FIRST && fighter_frame(1)==CC_AIR_KICK_FIRST);
    memset(controls,0,sizeof(controls));simulate();assert(fighter_frame(1)==CC_JUMP_FIRST);
    shutdown();
}
static void kick_and_kneel(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));clean_simulation();
    const struct character_tuning *c=&characters[0];
    /* Hit the inner edge of the authored reach, but not its outer boundary. */
    int limit=c->kick_reach_px+characters[1].body_half_width_px;
    for(int gap=limit-1;gap<=limit;gap++) {
        clean_simulation();fighters[1].x=fighters[0].x+gap*8;
        controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;simulate();
        controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=false;
        for(int t=0;t<c->kick_startup_ticks;t++)simulate();
        assert(fighters[1].health==(gap<limit?100-c->kick_damage:100));
    }
    clean_simulation();
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;simulate();
    controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=false;
    for(int t=0;t<c->kick_startup_ticks+c->kick_active_ticks+c->kick_recovery_ticks;t++) {
        int phase=t<c->kick_startup_ticks?0:t<c->kick_startup_ticks+c->kick_active_ticks?1:2;
        assert(fighter_frame(0)==CC_KICK_FIRST+phase);
        assert(active_attack(0)==(phase==1));
        assert(fighters[1].health==(phase==0?100:100-c->kick_damage));
        simulate();
    }
    assert(!fighters[0].attack && fighters[1].health==100-characters[0].kick_damage);
    for(int kind=1;kind<=2;kind++) {
        clean_simulation();controls[0].buttons[CHIRKY_BUTTON_DOWN]=true;
        for(int i=0;i<4;i++)simulate();
        assert(fighter_frame(0)==CC_KNEEL_FIRST && body_height(0)==47);
        controls[0].button_pressed[kind==1?CHIRKY_BUTTON_PRIMARY:CHIRKY_BUTTON_SECONDARY]=true;simulate();
        assert(fighters[0].attack==kind && fighters[0].crouch && body_height(0)==47);
        struct attack_tuning m=move_for(0);
        memset(controls[0].button_pressed,0,sizeof(controls[0].button_pressed));
        for(int t=0;t<m.startup+m.active+m.recovery;t++) {
            bool active=t>=m.startup && t<m.startup+m.active;
            bool extended=t>=m.startup && t<m.startup+m.active+m.followthrough;
            assert(fighter_frame(0)==(extended?(kind==1?CC_CROUCH_PUNCH_FIRST:CC_CROUCH_KICK_FIRST):CC_KNEEL_FIRST));
            assert(active_attack(0)==active);
            assert(fighters[0].crouch && body_height(0)==47);
            simulate();
        }
        assert(fighters[1].health==100-m.damage);
        controls[0].buttons[CHIRKY_BUTTON_DOWN]=false;simulate();
        assert(!fighters[0].crouch && fighters[0].transition==c->crouch_exit_ticks && body_height(0)==58);
    }
    shutdown();
}
static void explicit_guard_and_balance(const struct chirky_host_api *api)
{
    assert(init(api,"games/circuit-clash/game.conf"));
    for(int low=0;low<2;low++) {
        clean_simulation();controls[0].buttons[CHIRKY_BUTTON_DOWN]=low;
        controls[0].buttons[CHIRKY_BUTTON_PRIMARY]=controls[0].buttons[CHIRKY_BUTTON_SECONDARY]=true;
        controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;
        controls[0].buttons[CHIRKY_BUTTON_RIGHT]=true;simulate();
        assert(fighters[0].blocking && !fighters[0].attack && fighters[0].x==100*8);
        assert(fighter_frame(0)==(low?CC_CROUCH_BLOCK_FIRST:CC_BLOCK_FIRST));
        memset(controls[0].button_pressed,0,sizeof(controls[0].button_pressed));
        fighters[1].attack=2;fighters[1].action_tick=characters[1].kick_startup_ticks-1;
        simulate();assert(fighters[0].health==100 && fighters[0].guard_stun);
        int hp=fighters[0].health;
        for(int t=0;t<30;t++)simulate();
        assert(fighters[0].health==hp && fighters[0].blocking);
        controls[0].buttons[CHIRKY_BUTTON_SECONDARY]=false;simulate();
        assert(!fighters[0].blocking && !fighters[0].attack && fighters[0].guard_release==characters[0].block_release_ticks);
        for(int t=0;t<10;t++)simulate();
        assert(!fighters[0].attack); /* Releasing a chord never attacks. */
    }
    /* Backwards movement is no longer guard. Hitstun cannot turn into block. */
    clean_simulation();controls[0].buttons[CHIRKY_BUTTON_LEFT]=true;simulate();assert(!fighters[0].blocking);
    controls[0].buttons[CHIRKY_BUTTON_PRIMARY]=controls[0].buttons[CHIRKY_BUTTON_SECONDARY]=true;
    fighters[0].stun=5;fighters[0].guard_stun=false;simulate();assert(!fighters[0].blocking);
    /* Both press orders tolerate two ticks, without cancelling active/recovery. */
    for(int kind=1;kind<=2;kind++)for(int phase=0;phase<3;phase++) {
        clean_simulation();fighters[0].attack=kind;
        struct attack_tuning m=move_for(0);
        fighters[0].action_tick=phase==0?0:phase==1?m.startup:m.startup+m.active;
        controls[0].buttons[CHIRKY_BUTTON_PRIMARY]=controls[0].buttons[CHIRKY_BUTTON_SECONDARY]=true;
        simulate();assert(fighters[0].blocking==(phase==0));assert((fighters[0].attack!=0)==(phase!=0));
    }
    clean_simulation();fighters[0].y=20*8;
    controls[0].buttons[CHIRKY_BUTTON_PRIMARY]=controls[0].buttons[CHIRKY_BUTTON_SECONDARY]=true;
    controls[0].button_pressed[CHIRKY_BUTTON_PRIMARY]=true;simulate();assert(!fighters[0].blocking && !fighters[0].attack);
    /* Six moves retain independent values; shipped kicks trade speed for power. */
    for(int mode=0;mode<3;mode++) {
        clean_simulation();fighters[0].air_attack=mode==2;fighters[0].crouch=mode==1;
        fighters[0].attack=1;struct attack_tuning punch=move_for(0);
        fighters[0].attack=2;struct attack_tuning kick=move_for(0);
        assert(kick.damage>punch.damage && kick.startup>punch.startup && kick.recovery>punch.recovery);
        assert(punch.blockstun<punch.active+punch.recovery && kick.blockstun<kick.active+kick.recovery);
    }
    /* A strike that missed its active window cannot hit on visual followthrough,
       even when the opponent moves into reach. Recovery still locks actions. */
    for(int mode=0;mode<3;mode++) {
        clean_simulation();fighters[0].attack=1;fighters[0].crouch=mode==1;fighters[0].air_attack=mode==2;
        if(mode==2)fighters[0].y=fighters[1].y=20*8;
        struct attack_tuning m=move_for(0);
        fighters[0].action_tick=m.startup+m.active;
        int strike=mode==0?CC_PUNCH_FIRST+1:mode==1?CC_CROUCH_PUNCH_FIRST:CC_AIR_PUNCH_FIRST;
        assert(fighter_frame(0)==strike && !active_attack(0) && !fighters[0].hit);
        controls[0].button_pressed[CHIRKY_BUTTON_SECONDARY]=true;
        simulate();assert(fighters[0].attack==1 && fighters[1].health==100 && !fighters[0].hit);
        fighters[0].action_tick=m.startup+m.active+m.followthrough;
        assert(fighter_frame(0)!=strike && !active_attack(0));
        fighters[0].action_tick=m.startup+m.active+m.recovery-1;
        memset(controls,0,sizeof(controls));simulate();assert(!fighters[0].attack);
    }
    characters[1].crouch_punch_damage=37;fighters[1].attack=1;fighters[1].crouch=true;
    assert(move_for(1).damage==37 && characters[0].crouch_punch_damage==10);
    shutdown();
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
    tick(4,0,0);assert(fighters[0].jump_wait==1 && !fighters[0].y);
    tick(0,0,0);
    assert(fighters[0].y>0 && fighters[1].y==0);
    for(int i=0;i<60;i++)tick(0,0,0);
    /* Move into reach and prove one held punch causes only one hit. */
    while(fighters[1].x-fighters[0].x>44*8)tick(2,1,0);
    tick(16,0,0);for(int i=0;i<60;i++)tick(16,0,0);
    assert(fighters[1].health==100-characters[0].punch_damage && fighters[0].health==100);
    tick(0,256,0);assert(waiting);ax=fighters[0].x;
    tick(2,256,0);assert(fighters[0].x==ax && fighters[1].health==100-characters[0].punch_damage);
    tick(0,256,16);assert(players[1]==3 && !waiting && release_gate);
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    for(int hit=0;hit<(characters[1].health-1)/characters[0].punch_damage;hit++) {
        while(fighters[1].x-fighters[0].x>44*8)tick(2,256,0);
        tick(16,256,0);for(int i=0;i<30;i++)tick(0,256,0);
    }
    assert(winner==0 && fighters[1].health==0);
    tick(16,256,0);assert(fighters[0].ready && winner==0);
    tick(0,256,16);assert(winner==-1 && fighters[0].health==100 && fighters[1].health==100);
    /* Equal active-frame attacks resolve together, including the final hit. */
    tick(0,256,0);for(int i=0;i<90;i++)tick(0,256,0);
    fighters[0].x=100*8;fighters[1].x=124*8;
    fighters[0].health=fighters[1].health=characters[0].punch_damage;
    tick(16,256,16);for(int i=0;i<6;i++)tick(0,256,0);
    assert(winner==2 && fighters[0].health==0 && fighters[1].health==0);
    shutdown();assert(init(&api,"games/circuit-clash/game.conf") && !players[0] && !players[1]);
    api.screen_width=256;api.screen_height=192;tick(0,0,0);tick(16,16,0);tick(0,0,0);
    assert(rectangles && texts);
    shutdown();api.screen_width=288;api.screen_height=216;
    movement_and_balance(&api);animation_timing(&api);jump_response(&api);kick_and_kneel(&api);aerial_attacks(&api);explicit_guard_and_balance(&api);malformed_tuning();
    puts("Circuit Clash: joins, practice, movement, crouch, jump, attack windows, guard, weight, collision, KO, tuning validation and 1:1 sprites passed.");
}
