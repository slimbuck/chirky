#include "chirky.h"
#include "text_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* First local multiplayer consumer. Fighters intentionally use simple geometric
   placeholders; all movement and drawing stays on the logical pixel grid. */
static const struct chirky_host_api *host;
struct fighter { int x,y,vy,health,facing,attack,stun; bool hit,ready; };
static struct fighter fighters[2];
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
    fighters[0]=(struct fighter){.x=host->screen_width*8/4,.health=100,.facing=1};
    fighters[1]=(struct fighter){.x=host->screen_width*8*3/4,.health=100,.facing=-1};
    countdown=90;round_ticks=0;winner=-1;release_gate=true;
}
static bool init(const struct chirky_host_api *api,const char *config)
{
    (void)config;host=api;
    memset(players,0,sizeof(players));memset(controls,0,sizeof(controls));
    memset(connected,0,sizeof(connected));memset(gates,0,sizeof(gates));
    keyboard_join[0]=0;started=false;waiting=true;reset_round();return true;
}
static void shutdown(void){host=NULL;}
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
        else {countdown=90;release_gate=true;}
    }
    if(release_gate) {
        if(neutral(&controls[0]) && neutral(&controls[1]))release_gate=false;
        return;
    }
    if(countdown){countdown--;return;}
    if(winner!=-1) {
        for(int p=0;p<2;p++)if(controls[p].button_pressed[CHIRKY_BUTTON_PRIMARY])fighters[p].ready=true;
        if(fighters[0].ready && fighters[1].ready)reset_round();
        return;
    }
    round_ticks++;
    for(int p=0;p<2;p++) {
        struct fighter *f=&fighters[p];const struct chirky_device_input *c=&controls[p];
        if(f->stun)f->stun--;
        if(f->attack)f->attack--;
        if(!f->attack)f->facing=f->x<=fighters[1-p].x?1:-1;
        if(!f->stun && !f->attack) {
            f->x+=12*((int)c->buttons[CHIRKY_BUTTON_RIGHT]-(int)c->buttons[CHIRKY_BUTTON_LEFT]);
            if(!f->y && (c->button_pressed[CHIRKY_BUTTON_SECONDARY] || c->button_pressed[CHIRKY_BUTTON_UP]))f->vy=34;
            if(c->button_pressed[CHIRKY_BUTTON_PRIMARY]){f->attack=22;f->hit=false;}
        }
        f->y+=f->vy;if(f->y>0)f->vy-=2;else {f->y=0;f->vy=0;}
        if(f->x<12*8)f->x=12*8;
        if(f->x>(host->screen_width-12)*8)f->x=(host->screen_width-12)*8;
    }
    /* Evaluate both strikes before applying either, allowing fair trades/KOs. */
    bool strikes[2]={false,false};
    for(int p=0;p<2;p++) {
        struct fighter *a=&fighters[p],*b=&fighters[1-p];
        int ahead=(b->x-a->x)*a->facing;
        strikes[p]=a->attack>=14 && a->attack<=16 && !a->hit && !a->stun &&
            ahead>=0 && ahead<38*8 && abs(a->y-b->y)<24*8;
    }
    for(int p=0;p<2;p++)if(strikes[p]) {
        struct fighter *a=&fighters[p],*b=&fighters[1-p];a->hit=true;
        b->health-=20;b->stun=12;b->x+=a->facing*8*8;
        if(b->x<12*8)b->x=12*8;
        if(b->x>(host->screen_width-12)*8)b->x=(host->screen_width-12)*8;
    }
    if(fighters[0].health<=0 || fighters[1].health<=0) {
        winner=fighters[0].health<=0?(fighters[1].health<=0?2:1):0;
        release_gate=true;
    }
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
static void draw_fighter(int p)
{
    const struct fighter *f=&fighters[p];int x=f->x/8,y=42+f->y/8;
    unsigned body=f->stun && (round_ticks/2)%2?0xfff6df:colour(p);
    rect(x-12,38,24,3,0x111828);
    rect(x-8,y,6,9,body);rect(x+2,y,6,9,body);
    rect(x-9,y+9,18,22,0x101a2a);rect(x-7,y+11,14,18,body);
    rect(x-8,y+32,16,14,body);rect(x-6,y+36,12,5,0x101a2a);
    rect(x+(f->facing>0?1:-5),y+37,4,3,0xfff6df);
    int reach=f->attack>=14 && f->attack<=16?27:f->attack>16?4:12;
    rect(f->facing>0?x+7:x-reach-7,y+19,reach,7,body);
    rect(f->facing>0?x+reach+3:x-reach-9,y+17,8,11,0xfff6df);
}
static void render(void)
{
    int w=host->screen_width,h=host->screen_height;
    rect(0,0,w,h,0x101828);rect(0,40,w,1,0x53647e);
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
                label_line(x+8,72,p,CHIRKY_BUTTON_SECONDARY,"JUMP");
            } else {
                text(x+8,h-88,"PRESS AN ACTION",1,0xf6eed9);
                text(x+8,h-103,"TO JOIN",1,0xf6eed9);
            }
        }
        centered(32,keyboard_join,1,0xffdc83);
        centered(17,"USE CONTROLLERS",1,0x9baec2);
        return;
    }
    for(int p=0;p<2;p++) {
        int x=p?w/2+8:8,bw=w/2-16;
        rect(x,h-40,bw,9,0x344158);rect(x,h-40,bw* fighters[p].health/100,9,colour(p));
        text(x,h-48,p?"P2":"P1",1,colour(p));
        char movement[64],clipped[64];
        snprintf(movement,sizeof(movement),"%s %s",controls[p].labels[CHIRKY_BUTTON_LEFT],controls[p].labels[CHIRKY_BUTTON_RIGHT]);
        chirky_text_copy(clipped,sizeof(clipped),movement,(size_t)(w/2-40)/6);
        text(x+24,h-48,clipped,1,0x9baec2);
        label_line(x,26,p,CHIRKY_BUTTON_PRIMARY,"PUNCH");
        label_line(x,12,p,CHIRKY_BUTTON_SECONDARY,"JUMP");
        draw_fighter(p);
    }
    if(winner!=-1) {
        rect(18,98,w-36,52,0x203047);
        centered(138,winner==2?"DOUBLE KO":winner?"PLAYER 2 WINS":"PLAYER 1 WINS",1,0xffdc83);
        centered(119,"BOTH PRESS PUNCH TO REMATCH",1,0xf6eed9);
        centered(104,fighters[0].ready?(fighters[1].ready?"READY":"P1 READY"):fighters[1].ready?"P2 READY":"",1,0x52dccb);
    } else if(release_gate)centered(128,"RELEASE BUTTONS",1,0xffdc83);
    else if(countdown) {
        char n[16];snprintf(n,sizeof(n),"%u",(countdown+29)/30);centered(135,n,3,0xffdc83);
    } else if(round_ticks<40)centered(135,"FIGHT",2,0xffdc83);
}
__attribute__((visibility("default"))) const struct chirky_game_api *chirky_game_entry(void)
{
    static const struct chirky_game_api game={CHIRKY_ABI_VERSION,init,shutdown,update,render};return &game;
}
