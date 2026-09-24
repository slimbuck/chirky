#include "game_state.h"
#include "splash_art.h"
#include <stdio.h>
#include <string.h>

static struct splash_art title_art;
void title_art_load(const char *config) { splash_load(&title_art,config); }
void title_art_free(void) { splash_free(&title_art); }

static struct colour sprite_colour(char value)
{
    if (value == 'g') return settings.phosphor;
    if (value == 'n') return settings.deep;
    if (value == 's') return settings.platform;
    if (value == 'c') return settings.edge;
    if (value == 'a') return settings.amber;
    if (value == 'w') return settings.paper;
    if (value == 'r') return settings.hazard;
    return settings.background;
}

static void rectangle(int x, int y, int width, int height, struct colour colour)
{
    host->fill_rect(host->context,x,y,width,height,colour.r,colour.g,colour.b);
}

static void text(int x, int y, const char *value, int scale, struct colour colour)
{
    host->draw_text(host->context,x,y,value,scale,colour.r,colour.g,colour.b);
}

static void controller_label(enum chirky_button action, const char *fallback,
                             char *label, size_t capacity)
{
    if (host->button_label != NULL)
        host->button_label(host->context, action, label, capacity);
    else
        copy_text(label, capacity, fallback);
}

/* Drawing consumes state only; effects can later be added as separate passes. */
static void draw_sprite(const char *id, int x, int y, bool flip, int tick,
                        const struct colour *tint)
{
    const struct grid *frame=animation_frame(content_animation(&content,id),tick);
    if (!frame || x>=host->screen_width || y>=host->screen_height ||
        x+frame->width<=0 || y+frame->height<=0) return;
    /* The host clears a scissored rectangle per call. Merge repeated rows and
       colour runs so solid scenery does not issue thousands of GL clears. */
    for (int row=0;row<frame->height;) {
        int height=1;
        while (row+height<frame->height &&
            !memcmp(frame->pixels+(size_t)row*frame->width,
                    frame->pixels+(size_t)(row+height)*frame->width,(size_t)frame->width)) height++;
        for (int column=0;column<frame->width;) {
            int source=flip?frame->width-1-column:column;
            char pixel=frame->pixels[(size_t)row*frame->width+source];
            int width=1;
            while (column+width<frame->width &&
                frame->pixels[(size_t)row*frame->width+(flip?source-width:source+width)]==pixel) width++;
            if (pixel!='.') rectangle(x+column,y+frame->height-row-height,width,height,
                                      tint?*tint:sprite_colour(pixel));
            column+=width;
        }
        row+=height;
    }
}

static void render_background(void)
{
    rectangle(0,0,host->screen_width,host->screen_height,settings.background);
    int slow = (int)(camera_x*.18f);
    for (int index = -1; index < 8; ++index) {
        int x = index*52-(slow%52);
        int height = 58 + ((index*37+113)&63);
        draw_sprite("machinery",x,height-70,false,frame_number,NULL);
        for (int lamp=0;lamp<3;++lamp)
            draw_sprite("lamp",x+13,height-14-lamp*13,false,frame_number+(index+lamp+8)*45,NULL);
    }
    for (int index=0;index<18;++index) {
        int x=(index*71-(int)(camera_x*.05f))%360;
        if (x<0) x+=360;
        int y=52+(index*43)%165;
        draw_sprite("spark",x,y,false,frame_number+index*30,NULL);
    }
}

static void render_world(void)
{
    /* Include the maximum editable sprite overhang around the viewport. */
    int first=(int)camera_x/TILE-16;
    int last=((int)camera_x+host->screen_width)/TILE+16;
    int bottom=(int)camera_y/TILE-16;
    int top=((int)camera_y+host->screen_height)/TILE+16;
    if (bottom<0) bottom=0;
    if (top>level.height) top=level.height;
    for (int ty=bottom;ty<top;++ty) {
        for (int tx=first;tx<=last;++tx) {
            char tile=tile_at(tx,ty);
            int x=tx*TILE-(int)camera_x, y=ty*TILE-(int)camera_y;
            if (tile=='#') {
                draw_sprite(tile_at(tx,ty+1)=='#'?"platform":"platform-top",x,y,false,frame_number,NULL);
                if (((tx*13+ty*7)&3)==0) draw_sprite("platform-detail",x,y,false,frame_number,NULL);
            } else if (tile=='^') draw_sprite("hazard",x,y,false,frame_number,NULL);
            else if (tile=='o') draw_sprite("shard",x,y,false,frame_number,NULL);
            else if (tile=='C') {
                bool active=respawn_x==tx*TILE-2 && respawn_y==(ty+1)*TILE;
                draw_sprite(active?"checkpoint-active":"checkpoint",x,y,false,frame_number,NULL);
            } else if (tile=='E') draw_sprite(collected_shards==level.total_shards?
                "portal-active":"portal-locked",x-5,y-6,false,frame_number,NULL);
        }
    }
}

static void render_particles(void)
{
    for (int index=0;index<MAX_PARTICLES;++index) {
        if (particles[index].life<=0) continue;
        int x=(int)(particles[index].x-camera_x), y=(int)(particles[index].y-camera_y);
        draw_sprite("particle",x,y,false,frame_number,&particles[index].colour);
    }
}

static void render_player(void)
{
    int x=(int)(player_x-camera_x), y=(int)(player_y-camera_y);
    const char *id=phase==PHASE_DEAD?"player-death":dash_timer>0?"player-dash":
        !on_ground?(velocity_y>0?"player-jump":"player-fall"):
        absolute(velocity_x)>.3f?"player-run":"player-idle";
    if (dash_timer>0) draw_sprite("dash-trail",x-facing*7,y+4,facing<0,frame_number,NULL);
    if(!robot_draw_weighted(host,x+PLAYER_WIDTH/2,y,facing,player_robot_clip(),(float)player_animation_tick,&player_motion))
        draw_sprite(id,x,y,facing<0,frame_number,NULL);
}

static void centered_text(int y, const char *value, int scale, struct colour colour)
{
    char visible[96];
    int columns=(host->screen_width-12)/(6*scale);
    snprintf(visible,sizeof(visible),"%.*s",columns,value);
    text((host->screen_width-(int)strlen(visible)*6*scale)/2,y,visible,scale,colour);
}

static void render_hud(void)
{
    char line[64];
    int top=host->screen_height-10;
    rectangle(4,top-14,host->screen_width-8,20,settings.background);
    snprintf(line,sizeof(line),"SIGNAL %02d/%02d",collected_shards,level.total_shards);
    text(8,top,line,1,settings.paper);
    snprintf(line,sizeof(line),"FALLS %02d",deaths);
    text(host->screen_width-8-(int)strlen(line)*6,top,line,1,settings.amber);
    snprintf(line,sizeof(line),"LV %d/%d",current_level+1,content.level_count);
    centered_text(top,line,1,settings.edge);
    if (dash_available) rectangle(8,top-12,8,3,settings.edge);
}

void render_title(void)
{
    char jump[32],dash[32],menu[32],line[96];
    controller_label(CHIRKY_BUTTON_B,"B",jump,sizeof(jump));
    controller_label(CHIRKY_BUTTON_Y,"Y",dash,sizeof(dash));
    controller_label(CHIRKY_BUTTON_SELECT,"SELECT",menu,sizeof(menu));
    if (splash_draw(&title_art,host)) {
        rectangle(0,0,host->screen_width,48,settings.background);
        centered_text(39,"RESTORE THE LAST SIGNAL",1,settings.paper);
        centered_text(27,"D-PAD MOVE / B JUMP / Y DASH",1,settings.edge);
        centered_text(17,"L RESTART / SELECT PAUSE",1,settings.edge);
        centered_text(7,"B - BEGIN",1,settings.amber);
        return;
    }
    render_background();
    int height=host->screen_height;
    centered_text(height-24,"PHOSPHOR",3,settings.phosphor);
    centered_text(height-54,"RUN",3,settings.amber);
    rectangle(16,height-80,host->screen_width-32,2,settings.edge);
    centered_text(height-98,"RESTORE THE LAST SIGNAL",1,settings.paper);
    centered_text(height-116,"D-PAD MOVE / L RESTART",1,settings.edge);
    snprintf(line,sizeof(line),"JUMP - %s",jump); centered_text(height-130,line,1,settings.edge);
    snprintf(line,sizeof(line),"DASH - %s",dash); centered_text(height-144,line,1,settings.edge);
    snprintf(line,sizeof(line),"%s - PAUSE",menu); centered_text(height-158,line,1,settings.edge);
    if ((title_timer/28)&1) {
        centered_text(14,"PRESS A BUTTON TO BEGIN",1,settings.paper);
    }
}

void render_game(void)
{
    render_background(); render_world(); render_particles();
    if (robot_ready() || phase!=PHASE_DEAD || (death_timer&3)<2) render_player();
    render_hud();
    int middle=host->screen_height/2;
    if (phase==PHASE_DEAD) {
        rectangle((host->screen_width-170)/2,middle-16,170,32,settings.background);
        centered_text(middle+5,"SIGNAL LOST",2,settings.hazard);
    } else if (phase==PHASE_WIN) {
        char confirm[32],line[96];
        controller_label(CHIRKY_BUTTON_B,"B",confirm,sizeof(confirm));
            rectangle(8,middle-51,host->screen_width-16,102,settings.background);
        centered_text(middle+29,"TRANSMISSION",3,settings.phosphor);
        centered_text(middle-2,"RESTORED",2,settings.paper);
        snprintf(line,sizeof(line),"%s - %s",confirm,current_level+1<content.level_count?"NEXT LEVEL":"RUN AGAIN");
        centered_text(middle-29,line,1,settings.amber);
    }
}
