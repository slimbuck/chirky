#include "chirky.h"
#include "text_encoding.h"
#include "tuning.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct chirky_host_api *host;
struct fighter {
    int x,y,vx,vy,health,facing,attack,action_tick,stun,transition,jump_wait,landing;
    unsigned animation_tick;
    bool hit,ready,crouch,blocking,guard_stun;
};
static struct fighter fighters[2];
static struct character_tuning characters[2];
static struct stage_tuning stage;
static chirky_asset artwork[2];
static bool practice,show_boxes;
static uint32_t players[2];
static struct chirky_device_input controls[2];
static char keyboard_join[64];
static bool connected[2],started,waiting,release_gate;
static unsigned countdown,round_ticks;
static int winner;
struct join_gate { uint32_t id; bool neutral; };
static struct join_gate gates[CHIRKY_INPUT_DEVICES];

static bool neutral(const struct chirky_device_input *d)
{
    for(int b=0;b<CHIRKY_BUTTON_COUNT;b++)if(d->buttons[b] || d->button_pressed[b])return false;
    return true;
}
static void reset_round(void)
{
    fighters[0]=(struct fighter){.x=host->screen_width*8*stage.spawn_left_percent/100,.health=characters[0].health,.facing=1};
    fighters[1]=(struct fighter){.x=host->screen_width*8*stage.spawn_right_percent/100,.health=characters[1].health,.facing=-1};
    countdown=stage.countdown_ticks;round_ticks=0;winner=-1;release_gate=true;
}
static void release_art(void)
{
    for(int p=0;p<2;p++) {
        if(artwork[p] && host && host->asset_release)host->asset_release(host->context,artwork[p]);
        artwork[p]=0;
    }
}
static bool load_content(const char *config)
{
    char directory[512],ids[3][64]={{0}},path[768],line[512];bool seen[4]={0};
    if(!config || strlen(config)>=sizeof(directory))return false;
    strcpy(directory,config);char *slash=strrchr(directory,'/');
    if(slash)*slash=0;else strcpy(directory,".");
    struct chirky_file file=chirky_file_open(host,config);if(!file.stream)return false;
    bool ok=true;show_boxes=false;
    while(ok && fgets(line,sizeof(line),file.stream)) {
        if(!strchr(line,'\n') && !feof(file.stream)){ok=false;break;}
        char *key=trim(line);if(!*key || *key=='#')continue;
        char *value=strchr(key,'=');if(!value){ok=false;break;}
        *value++=0;key=trim(key);value=trim(value);
        const char *keys[]={"player1_character","player2_character","stage","show_boxes"};
        int i;for(i=0;i<4;i++)if(!strcmp(key,keys[i]))break;
        if(i==4)continue; /* Catalog metadata belongs to the host. */
        if(seen[i]){ok=false;break;}seen[i]=true;
        if(i<3){if(!content_id(value)){ok=false;break;}strcpy(ids[i],value);}
        else {if(strcmp(value,"0") && strcmp(value,"1")){ok=false;break;}show_boxes=*value=='1';}
    }
    if(ferror(file.stream))ok=false;
    chirky_file_close(&file);
    if(!ok || !seen[0] || !seen[1] || !seen[2])return false;
    snprintf(path,sizeof(path),"%s/assets/stages/%s.conf",directory,ids[2]);
    if(!read_stage(host,path,&stage))return false;
    for(int p=0;p<2;p++) {
        snprintf(path,sizeof(path),"%s/assets/characters/%s.conf",directory,ids[p]);
        if(!read_character(host,path,&characters[p]))return false;
        if(host->asset_request && host->draw_sprite) {
            snprintf(path,sizeof(path),"%s/%s",directory,characters[p].atlas);
            artwork[p]=host->asset_request(host->context,path,CHIRKY_ASSET_IMAGE);
            if(!artwork[p] || host->asset_status(host->context,artwork[p])!=CHIRKY_ASSET_READY)return false;
            struct chirky_asset_view v=host->asset_data(host->context,artwork[p]);
            if(v.width!=384 || v.height!=96)return false;
        }
    }
    return true;
}
static bool init(const struct chirky_host_api *api,const char *config)
{
    if(!api || api->abi_version!=CHIRKY_ABI_VERSION || !api->fill_rect || !api->draw_text)return false;
    host=api;memset(artwork,0,sizeof(artwork));practice=false;
    if(!load_content(config)){release_art();host=NULL;return false;}
    memset(players,0,sizeof(players));memset(controls,0,sizeof(controls));
    memset(connected,0,sizeof(connected));memset(gates,0,sizeof(gates));
    keyboard_join[0]=0;started=false;waiting=true;reset_round();return true;
}
static void shutdown(void){release_art();host=NULL;}

struct attack_tuning { int startup,active,recovery,damage,reach,bottom,top,hitstun,blockstun,knockback; };
static struct attack_tuning move_for(int p)
{
    const struct character_tuning *c=&characters[p];
    if(fighters[p].attack==2)return (struct attack_tuning){c->kick_startup_ticks,c->kick_active_ticks,
        c->kick_recovery_ticks,c->kick_damage,c->kick_reach_px,c->kick_bottom_px,c->kick_top_px,
        c->kick_hitstun_ticks,c->kick_blockstun_ticks,c->kick_knockback_px};
    return (struct attack_tuning){c->punch_startup_ticks,c->punch_active_ticks,c->punch_recovery_ticks,
        c->punch_damage,c->punch_reach_px,c->punch_bottom_px,c->punch_top_px,
        c->punch_hitstun_ticks,c->punch_blockstun_ticks,c->punch_knockback_px};
}
static bool active_attack(int p)
{
    struct attack_tuning a=move_for(p);const struct fighter *f=&fighters[p];
    return f->attack && f->action_tick>=a.startup && f->action_tick<a.startup+a.active;
}
static void clamp_fighter(struct fighter *f)
{
    int left=stage.wall_margin_px*8,right=(host->screen_width-stage.wall_margin_px)*8;
    if(f->x<left)f->x=left;
    if(f->x>right)f->x=right;
}
static int approach(int value,int target,int step)
{ return value<target?(value+step<target?value+step:target):(value-step>target?value-step:target); }
static int body_height(int p)
{return fighters[p].crouch?characters[p].crouching_height_px:characters[p].standing_height_px;}
static void simulate(void)
{
    round_ticks++;
    for(int p=0;p<2;p++) {
        struct fighter *f=&fighters[p];const struct character_tuning *t=&characters[p];
        const struct chirky_device_input *c=&controls[p];
        f->animation_tick++;f->blocking=false;
        if(f->stun)f->stun--;
        if(f->transition)f->transition--;
        if(f->landing)f->landing--;
        if(f->attack) {
            struct attack_tuning a=move_for(p);
            if(++f->action_tick>=a.startup+a.active+a.recovery)f->attack=0;
        }
        if(!f->attack)f->facing=f->x<=fighters[1-p].x?1:-1;
        int direction=(int)c->buttons[CHIRKY_BUTTON_RIGHT]-(int)c->buttons[CHIRKY_BUTTON_LEFT];
        /* Holding guard continues to protect during blockstun; ordinary hitstun
           cannot be converted into guard until its recovery has completed. */
        f->blocking=f->stun && f->guard_stun && direction==-f->facing;
        bool locked=f->stun || f->attack || f->landing;
        if(f->jump_wait && !locked) {if(!--f->jump_wait)f->vy=t->jump_velocity_fp8;}
        if(!locked && !f->jump_wait && !f->y && !f->vy) {
            bool crouch=c->buttons[CHIRKY_BUTTON_DOWN];
            if(crouch!=f->crouch) {f->crouch=crouch;f->transition=crouch?t->crouch_enter_ticks:t->crouch_exit_ticks;}
            if(!f->transition) {
                if(!crouch && c->button_pressed[CHIRKY_BUTTON_UP]) {
                    f->jump_wait=t->jump_startup_ticks;
                    if(!f->jump_wait)f->vy=t->jump_velocity_fp8;
                } else if(c->button_pressed[CHIRKY_BUTTON_PRIMARY] || c->button_pressed[CHIRKY_BUTTON_SECONDARY]) {
                    f->attack=c->button_pressed[CHIRKY_BUTTON_PRIMARY]?1:2;
                    f->action_tick=0;f->hit=false;
                } else f->blocking=direction==-f->facing;
            }
        }
        int target=0,accel=t->ground_brake_fp8;
        if(!locked && !f->attack && !f->crouch && !f->transition && !f->jump_wait) {
            int speed=f->y?t->air_speed_fp8:direction==f->facing?t->walk_speed_fp8:t->back_speed_fp8;
            target=direction*speed;
            accel=f->y?t->air_accel_fp8:direction?t->ground_accel_fp8:t->ground_brake_fp8;
        }
        f->vx=approach(f->vx,target,accel);f->x+=f->vx;
        bool airborne=f->y>0 || f->vy>0;
        f->y+=f->vy;
        if(f->y>0) {
            f->vy-=t->gravity_fp8;
            if(f->vy < -t->max_fall_speed_fp8)f->vy=-t->max_fall_speed_fp8;
        } else {
            f->y=0;f->vy=0;
            if(airborne)f->landing=t->landing_ticks;
        }
        clamp_fighter(f);
    }
    /* Separate overlapping bodies, including at a wall; jumping can cross over. */
    if(fighters[0].y<fighters[1].y+body_height(1)*8 && fighters[1].y<fighters[0].y+body_height(0)*8) {
        int l=fighters[0].x<=fighters[1].x?0:1,r=1-l;
        int width=(characters[0].body_half_width_px+characters[1].body_half_width_px)*8;
        int overlap=width-(fighters[r].x-fighters[l].x);
        if(overlap>0) {
            fighters[l].x-=overlap/2;fighters[r].x+=overlap-overlap/2;
            clamp_fighter(&fighters[l]);clamp_fighter(&fighters[r]);
            if(fighters[r].x-fighters[l].x<width) {
                if(fighters[l].x==stage.wall_margin_px*8)fighters[r].x=fighters[l].x+width;
                else fighters[l].x=fighters[r].x-width;
            }
        }
    }
    /* Snapshot both contacts and guards before applying either: fair trades. */
    bool strikes[2]={false,false},blocked[2]={false,false};
    struct attack_tuning moves[2]={move_for(0),move_for(1)};
    for(int p=0;p<2;p++) {
        struct fighter *a=&fighters[p],*b=&fighters[1-p];struct attack_tuning m=moves[p];
        int ahead=(b->x-a->x)*a->facing;
        strikes[p]=active_attack(p) && !a->hit && !a->stun && ahead>=0 &&
            ahead-characters[1-p].body_half_width_px*8<m.reach*8 &&
            a->y+m.top*8>b->y && a->y+m.bottom*8<b->y+body_height(1-p)*8;
        blocked[p]=b->blocking && b->facing==-a->facing;
    }
    for(int p=0;p<2;p++)if(strikes[p]) {
        struct fighter *a=&fighters[p],*b=&fighters[1-p];struct attack_tuning m=moves[p];a->hit=true;
        if(!blocked[p])b->health-=m.damage;
        if(b->health<0)b->health=0;
        b->stun=blocked[p]?m.blockstun:m.hitstun;b->guard_stun=blocked[p];b->attack=0;b->jump_wait=0;b->vx=0;
        b->x+=a->facing*m.knockback*8*100/characters[1-p].weight_percent/(blocked[p]?2:1);
        clamp_fighter(b);
    }
    if(fighters[0].health<=0 || fighters[1].health<=0) {
        winner=fighters[0].health<=0?(fighters[1].health<=0?2:1):0;release_gate=true;
    }
}
static void update(const struct chirky_input *input)
{
    bool was_waiting=waiting;
    char join_labels[2][CHIRKY_INPUT_LABEL_SIZE]={{0}};unsigned keyboards=0;
    for(unsigned d=0;d<input->device_count && keyboards<2;d++)if(input->devices[d].kind==CHIRKY_DEVICE_KEYBOARD)
        chirky_text_copy(join_labels[keyboards++],CHIRKY_INPUT_LABEL_SIZE,input->devices[d].labels[CHIRKY_BUTTON_PRIMARY],12);
    if(keyboards==2)snprintf(keyboard_join,sizeof(keyboard_join),"KEYBOARD: %s / %s",join_labels[0],join_labels[1]);
    else snprintf(keyboard_join,sizeof(keyboard_join),"SHARE A KEYBOARD OR");
    connected[0]=connected[1]=false;
    for(unsigned d=0;d<input->device_count;d++)for(int p=0;p<2;p++)
        if(players[p] && players[p]==input->devices[d].id) {
            connected[p]=true;controls[p]=input->devices[d];
        }
    if(!started && connected[0] && !connected[1] && controls[0].button_pressed[CHIRKY_BUTTON_START])practice=true;
    if(practice){connected[1]=true;memset(&controls[1],0,sizeof(controls[1]));}
    waiting=!connected[0] || !connected[1];
    struct join_gate next[CHIRKY_INPUT_DEVICES]={0};
    for(unsigned d=0;d<input->device_count && d<CHIRKY_INPUT_DEVICES;d++) {
        const struct chirky_device_input *device=&input->devices[d];
        bool armed=false,press=false;
        for(unsigned g=0;g<CHIRKY_INPUT_DEVICES;g++)if(gates[g].id==device->id)armed=gates[g].neutral;
        for(int b=CHIRKY_BUTTON_PRIMARY;b<CHIRKY_BUTTON_START;b++)press|=device->button_pressed[b];
        next[d]=(struct join_gate){device->id,armed || neutral(device)};
        if(!waiting || !armed || !press || device->id==players[0] || device->id==players[1])continue;
        int p=connected[0]?1:0;
        players[p]=device->id;controls[p]=*device;connected[p]=true;
        waiting=!connected[0] || !connected[1];
        next[d].neutral=false;
    }
    memcpy(gates,next,sizeof(gates));
    if(waiting)return;
    if(was_waiting) {
        if(!started){reset_round();started=true;}
        else {countdown=stage.countdown_ticks;release_gate=true;}
    }
    if(release_gate) {
        if(neutral(&controls[0]) && neutral(&controls[1]))release_gate=false;
        return;
    }
    if(countdown){countdown--;return;}
    if(practice && controls[0].button_pressed[CHIRKY_BUTTON_START]){reset_round();return;}
    if(winner!=-1) {
        for(int p=0;p<2;p++)if(controls[p].button_pressed[CHIRKY_BUTTON_PRIMARY])fighters[p].ready=true;
        if(fighters[0].ready && (practice || fighters[1].ready))reset_round();
        return;
    }
    simulate();
}

static void rect(int x,int y,int w,int h,unsigned colour)
{host->fill_rect(host->context,x,y,w,h,colour>>16,(colour>>8)&255,colour&255);}
static void text(int x,int y,const char *s,int scale,unsigned colour)
{host->draw_text(host->context,x,y,s,scale,colour>>16,(colour>>8)&255,colour&255);}
static void centered(int y,const char *s,int scale,unsigned colour)
{text((host->screen_width-(int)chirky_text_length(s)*6*scale)/2,y,s,scale,colour);}
static unsigned colour(int p){return p?0xff795c:0x52dccb;}
static const char *device_name(int p)
{
    return controls[p].kind==CHIRKY_DEVICE_KEYBOARD?"KEYBOARD":
        controls[p].kind==CHIRKY_DEVICE_TOUCH?"TOUCH":"CONTROLLER";
}
static void label_line(int x,int y,int p,int action,const char *verb)
{
    char line[64],label[CHIRKY_INPUT_LABEL_SIZE];
    size_t columns=(size_t)(host->screen_width/2-16)/6-strlen(verb)-1;
    chirky_text_copy(label,sizeof(label),controls[p].labels[action],columns);
    snprintf(line,sizeof(line),"%s %s",label,verb);
    text(x,y,line,1,0xf6eed9);
}
static const char *state_name(int p)
{
    const struct fighter *f=&fighters[p];
    if(f->health<=0)return "KO";
    if(f->stun)return f->guard_stun?"GUARD":"HIT";
    if(f->attack)return f->attack==1?"PUNCH":"KICK";
    if(f->jump_wait)return "TAKEOFF";
    if(f->y)return f->vy>0?"JUMP":"FALL";
    if(f->landing)return "LAND";
    if(f->crouch)return "CROUCH";
    if(f->blocking)return "BLOCK / BACK";
    return f->vx?"WALK":"IDLE";
}
static int idle_frame(int p)
{
    const struct character_tuning *c=&characters[p];
    int durations[]={c->idle_0_ticks,c->idle_1_ticks,c->idle_2_ticks,c->idle_3_ticks};
    unsigned tick=fighters[p].animation_tick%(durations[0]+durations[1]+durations[2]+durations[3]);
    for(int i=0;i<4;i++){if(tick<(unsigned)durations[i])return i;tick-=durations[i];}
    return 0;
}
static void outline(int x,int y,int w,int h,unsigned c)
{rect(x,y,w,1,c);rect(x,y+h-1,w,1,c);rect(x,y,1,h,c);rect(x+w-1,y,1,h,c);}
static void draw_fighter(int p)
{
    const struct fighter *f=&fighters[p];int x=f->x/8,y=stage.floor_y_px+f->y/8;
    rect(x-12,stage.floor_y_px-4,24,3,colour(p));
    /* Other actions deliberately hold an approved pose until authored clips
       exist. Never shrink, distort or procedurally redraw the approved sprite. */
    int frame=f->crouch || f->jump_wait?2:(f->y || f->attack || f->stun)?0:idle_frame(p);
    if(artwork[p] && host->draw_sprite)
        host->draw_sprite(host->context,artwork[p],x-48,y-16,96,96,
                          frame*96,0,96,96,255,255,255,255,f->facing<0);
    if(show_boxes) {
        int half=characters[p].body_half_width_px;
        outline(x-half,y,half*2,body_height(p),f->blocking?0x80c8ff:colour(p));
        if(active_attack(p)) {
            struct attack_tuning m=move_for(p);
            outline(f->facing>0?x:x-m.reach,y+m.bottom,m.reach,m.top-m.bottom,0xffdc83);
        }
    }
}
static void render(void)
{
    int w=host->screen_width,h=host->screen_height;
    rect(0,0,w,h,0x101828);rect(0,stage.floor_y_px-2,w,1,0x53647e);
    for(int x=0;x<w;x+=24)rect(x,0,1,40,0x253347);
    rect(0,18,w,1,0x253347);rect(0,32,w,1,0x253347);
    for(int x=12;x<w;x+=32)rect(x,66,12,40+(x%3)*10,0x17243a);
    centered(h-12,"CIRCUIT CLASH",2,0xffdc83);
    if(waiting) {
        centered(h-35,started?"MATCH PAUSED - PLAYER MISSING":"LOCAL TWO-PLAYER FIGHTER",1,0x9baec2);
        for(int p=0;p<2;p++) {
            int x=6+p*(w/2),cw=w/2-12;
            rect(x,51,cw,h-101,0x203047);rect(x,h-50,cw,2,colour(p));
            text(x+8,h-61,p?"PLAYER 2":"PLAYER 1",1,colour(p));
            if(connected[p]) {
                text(x+8,h-78,device_name(p),1,0xf6eed9);
                text(x+8,h-94,"JOINED",1,colour(p));
                label_line(x+8,87,p,CHIRKY_BUTTON_PRIMARY,"PUNCH");
                label_line(x+8,72,p,CHIRKY_BUTTON_SECONDARY,"KICK");
            } else {
                text(x+8,h-88,"PRESS AN ACTION",1,0xf6eed9);
                text(x+8,h-103,"TO JOIN",1,0xf6eed9);
            }
        }
        centered(32,keyboard_join,1,0xffdc83);
        if(connected[0])label_line(12,17,0,CHIRKY_BUTTON_START,"PRACTICE");
        else centered(17,"JOIN THEN START FOR PRACTICE",1,0x9baec2);
        return;
    }
    for(int p=0;p<2;p++) {
        int x=p?w/2+8:8,bw=w/2-16;
        rect(x,h-40,bw,9,0x344158);rect(x,h-40,bw* fighters[p].health/characters[p].health,9,colour(p));
        text(x,h-48,p?"P2":"P1",1,colour(p));
        text(x,h-61,state_name(p),1,colour(p));
        char movement[64],clipped[64];
        snprintf(movement,sizeof(movement),"%s",practice && p==1?"DUMMY":characters[p].name);
        chirky_text_copy(clipped,sizeof(clipped),movement,(size_t)(w/2-40)/6);
        text(x+24,h-48,clipped,1,0x9baec2);
        if(!(practice && p==1)) {
            label_line(x,26,p,CHIRKY_BUTTON_PRIMARY,"PUNCH");
            label_line(x,12,p,CHIRKY_BUTTON_SECONDARY,"KICK");
        } else {text(x,26,"START RESET",1,0x9baec2);text(x,12,"BACK BLOCKS",1,0x9baec2);}
        draw_fighter(p);
    }
    if(winner!=-1) {
        rect(18,98,w-36,52,0x203047);
        centered(138,winner==2?"DOUBLE KO":winner?"PLAYER 2 WINS":"PLAYER 1 WINS",1,0xffdc83);
        centered(119,practice?"PUNCH TO RESET":"BOTH PRESS PUNCH TO REMATCH",1,0xf6eed9);
        centered(104,fighters[0].ready?(fighters[1].ready?"READY":"P1 READY"):fighters[1].ready?"P2 READY":"",1,0x52dccb);
    } else if(release_gate)centered(128,"RELEASE BUTTONS",1,0xffdc83);
    else if(countdown) {
        char n[16];snprintf(n,sizeof(n),"%u",(countdown+29)/30);centered(135,n,3,0xffdc83);
    } else if(round_ticks<40)centered(135,practice?"PRACTICE":"FIGHT",2,0xffdc83);
}
__attribute__((visibility("default"))) const struct chirky_game_api *chirky_game_entry(void)
{
    static const struct chirky_game_api game={CHIRKY_ABI_VERSION,init,shutdown,update,render};return &game;
}
