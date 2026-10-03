#include "game_state.h"
#include "splash_art.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static struct splash_art title_art;
void title_art_load(const char *config) { splash_load_api(&title_art,host,config); }
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

enum { SCENERY_ATLAS_SIZE=512, SCENERY_ATLAS_PAGES=16 };
static chirky_asset scenery_atlas[SCENERY_ATLAS_PAGES];
static unsigned scenery_pages;
void scenery_atlas_free(void)
{
    for(unsigned i=0;i<scenery_pages;i++)if(host && host->asset_release)
        host->asset_release(host->context,scenery_atlas[i]);
    memset(scenery_atlas,0,sizeof(scenery_atlas));scenery_pages=0;
}
bool scenery_atlas_load(void)
{
    scenery_atlas_free();
    if(!host->image_create || !host->draw_sprite || !host->asset_release)return true;
    const size_t bytes=(size_t)SCENERY_ATLAS_SIZE*SCENERY_ATLAS_SIZE*4;
    unsigned char *rgba=calloc(1,bytes);if(!rgba)return false;
    int x=0,y=0,row=0;bool ok=true;
    /* Pack the editable text grids once, at their authored resolution. Each
       frame has colour + white silhouette cells, each with a one-texel gutter.
       The silhouette preserves the old particle colour-override semantics. */
    for(int i=0;ok && i<content.sprite_count;i++)for(int f=0;ok && f<content.sprites[i].animation.count;f++) {
        struct grid *g=&content.sprites[i].animation.frames[f];
        int w=2*(g->width+2),h=g->height+2;
        if(x+w>SCENERY_ATLAS_SIZE){x=0;y+=row;row=0;}
        if(y+h>SCENERY_ATLAS_SIZE) {
            chirky_asset image=host->image_create(host->context,SCENERY_ATLAS_SIZE,SCENERY_ATLAS_SIZE,rgba,bytes);
            if(!image){ok=false;break;}
            scenery_atlas[scenery_pages++]=image;
            if(scenery_pages==SCENERY_ATLAS_PAGES){ok=false;break;}
            memset(rgba,0,bytes);x=y=row=0;
        }
        g->atlas_page=scenery_pages;g->atlas_x=x+1;g->atlas_y=y+1;
        for(int yy=0;yy<g->height;yy++)for(int xx=0;xx<g->width;xx++) {
            char pixel=g->pixels[(size_t)yy*g->width+xx];if(pixel=='.')continue;
            struct colour c=sprite_colour(pixel);
            size_t index=((size_t)(y+1+yy)*SCENERY_ATLAS_SIZE+x+1+xx)*4;
            rgba[index]=c.r;rgba[index+1]=c.g;rgba[index+2]=c.b;rgba[index+3]=255;
            index+=(g->width+2)*4;
            memset(rgba+index,255,4);
        }
        x+=w;if(h>row)row=h;
    }
    if(ok) {
        chirky_asset image=host->image_create(host->context,SCENERY_ATLAS_SIZE,SCENERY_ATLAS_SIZE,rgba,bytes);
        if(image)scenery_atlas[scenery_pages++]=image;else ok=false;
    }
    free(rgba);if(!ok)scenery_atlas_free();return ok;
}

/* Camera operates on the scene, including the mesh; HUD remains screen-space. */
static bool scene_view;
static float zoom=1,shift_x,shift_y;
float level_intro_zoom(void)
{
    if(phase!=PHASE_LEVEL_INTRO)return 1;
    float t=clampf((LEVEL_INTRO_TICKS-level_intro_timer-LEVEL_INTRO_HOLD_TICKS)/
                   (float)LEVEL_INTRO_ZOOM_TICKS,0,1);
    /* Cubic ease-in/out: linger at the close-up, move briskly through the
       middle, then settle into the gameplay camera without a velocity snap. */
    float remaining=1-t;
    float eased=t<.5f?4*t*t*t:1-4*remaining*remaining*remaining;
    return 1+(LEVEL_INTRO_SCALE-1)*(1-eased);
}
static int view_x(int x) { return scene_view?(int)lroundf(x*zoom+shift_x):x; }
static int view_y(int y) { return scene_view?(int)lroundf(y*zoom+shift_y):y; }

static void rectangle(int x, int y, int width, int height, struct colour colour)
{
    int right=view_x(x+width),top=view_y(y+height);x=view_x(x);y=view_y(y);
    if(right>x && top>y)host->fill_rect(host->context,x,y,right-x,top-y,colour.r,colour.g,colour.b);
}

static void text(int x, int y, const char *value, int scale, struct colour colour)
{
    host->draw_text(host->context,x,y,value,scale,colour.r,colour.g,colour.b);
}

static void outlined_text(int x, int y, const char *value, int scale, struct colour colour)
{
    if(host->draw_text_outlined) {
        host->draw_text_outlined(host->context,x,y,value,scale,colour.r,colour.g,colour.b);return;
    }
    static const struct colour black={0,0,0};
    for (int offset_y=-1;offset_y<=1;offset_y++)
        for (int offset_x=-1;offset_x<=1;offset_x++)
            if (offset_x || offset_y) text(x+offset_x,y+offset_y,value,scale,black);
    text(x,y,value,scale,colour);
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
    if (!frame || view_x(x)>=host->screen_width || view_y(y)>=host->screen_height ||
        view_x(x+frame->width)<=0 || view_y(y+frame->height)<=0) return;
    if(scenery_pages) {
        int left=view_x(x),bottom=view_y(y),w=view_x(x+frame->width)-left,h=view_y(y+frame->height)-bottom;
        struct colour c=tint?*tint:(struct colour){255,255,255};
        if(scene_view && host->draw_sprite_projected) {
            host->draw_sprite_projected(host->context,scenery_atlas[frame->atlas_page],x*zoom+shift_x,y*zoom+shift_y,zoom,
                frame->atlas_x+(tint?frame->width+2:0),frame->atlas_y,frame->width,frame->height,
                c.r,c.g,c.b,255,flip);return;
        }
        host->draw_sprite(host->context,scenery_atlas[frame->atlas_page],left,bottom,w,h,
            frame->atlas_x+(tint?frame->width+2:0),frame->atlas_y,frame->width,frame->height,
            c.r,c.g,c.b,255,flip);
        return;
    }
    /* Merge identical texel runs; the shared renderer batches these as GPU triangles. */
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

/* Phase comes from a stable world identity, never a camera/screen coordinate.
   Different scenery keeps its authored speed without blinking in lockstep. */
int scenery_animation_tick(const char *id,int world_x,int world_y)
{
    const struct animation *animation=content_animation(&content,id);
    if(!animation || animation->count<1 || animation->ticks<1)return 0;
    uint32_t hash=(uint32_t)world_x*0x9e3779b1u^(uint32_t)world_y*0x85ebca6bu;
    for(const char *p=id;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
    hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
    unsigned period=(unsigned)animation->count*(unsigned)animation->ticks;
    return (int)(((unsigned)frame_number+hash%period)%period);
}
static void draw_scenery(const char *id,int x,int y,int world_x,int world_y)
{ draw_sprite(id,x,y,false,scenery_animation_tick(id,world_x,world_y),NULL); }

static void render_background(void)
{
    host->fill_rect(host->context,0,0,host->screen_width,host->screen_height,
                    settings.background.r,settings.background.g,settings.background.b);
    int slow = (int)(camera_x*.18f);
    for (int index = -1; index < 8; ++index) {
        int x = index*52-(slow%52);
        int height = 58 + ((index*37+113)&63);
        int world_column=index+slow/52;
        draw_scenery("machinery",x,height-70,world_column,0);
        for (int lamp=0;lamp<3;++lamp)
            draw_scenery("lamp",x+13,height-14-lamp*13,world_column,lamp);
    }
    for (int index=0;index<18;++index) {
        int x=(index*71-(int)(camera_x*.05f))%360;
        if (x<0) x+=360;
        int y=52+(index*43)%165;
        draw_scenery("spark",x,y,index,0);
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
                draw_scenery(tile_at(tx,ty+1)=='#'?"platform":"platform-top",x,y,tx,ty);
                if (((tx*13+ty*7)&3)==0) draw_scenery("platform-detail",x,y,tx,ty);
            } else if (tile=='^') draw_scenery("hazard",x,y,tx,ty);
            else if (tile=='o') draw_scenery("shard",x,y,tx,ty);
            else if (tile=='C') {
                bool active=respawn_x==tx*TILE-2 && respawn_y==(ty+1)*TILE;
                draw_scenery(active?"checkpoint-active":"checkpoint",x,y,tx,ty);
            } else if (tile=='E') draw_scenery(collected_shards==level.total_shards?
                "portal-active":"portal-locked",x-5,y-6,tx,ty);
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
    if(!robot_draw_character(host,view_x(x+PLAYER_WIDTH/2),view_y(y),facing,
        phase==PHASE_LEVEL_INTRO?ROBOT_IDLE:player_robot_clip(),(float)player_animation_tick,&player_motion,&player_face,zoom))
        draw_sprite(id,x,y,facing<0,frame_number,NULL);
}

static void centered_text(int y, const char *value, int scale, struct colour colour)
{
    char visible[96];
    int columns=(host->screen_width-12)/(6*scale);
    snprintf(visible,sizeof(visible),"%.*s",columns,value);
    text((host->screen_width-(int)strlen(visible)*6*scale)/2,y,visible,scale,colour);
}

static void centered_outlined_text(int y, const char *value, int scale, struct colour colour)
{
    char visible[96];
    int columns=(host->screen_width-12)/(6*scale);
    snprintf(visible,sizeof(visible),"%.*s",columns,value);
    outlined_text((host->screen_width-(int)strlen(visible)*6*scale)/2,y,visible,scale,colour);
}

static void format_time(int ticks, char *value, size_t capacity)
{
    int seconds=ticks/60;
    int hundredths=(ticks%60)*100/60;
    snprintf(value,capacity,"%02d:%02d.%02d",seconds/60,seconds%60,hundredths);
}

static void level_name(char *value, size_t capacity)
{
    const char *id=content.levels[current_level].id;
    size_t used=0;
    for (;*id && used+1<capacity;id++) {
        char letter=*id=='-'?' ':*id;
        if (letter>='a' && letter<='z') letter=(char)(letter-'a'+'A');
        value[used++]=letter;
    }
    value[used]='\0';
}

static void render_hud(void)
{
    char line[64],clock[24];
    int top=host->screen_height-10;
    draw_sprite("shard",8,top-6,false,frame_number,NULL);
    snprintf(line,sizeof(line),"%02d",level.total_shards-collected_shards);
    outlined_text(18,top,line,1,settings.paper);
    snprintf(line,sizeof(line),"LIVES %d",lives);
    outlined_text(host->screen_width-8-(int)strlen(line)*6,top,line,1,settings.amber);
    format_time(phase==PHASE_WIN || phase==PHASE_INITIALS?completed_ticks:level_ticks,
                clock,sizeof(clock));
    centered_outlined_text(top,clock,1,settings.edge);
    if (dash_available) rectangle(8,top-12,8,3,settings.edge);
}

static void render_level_intro(void)
{
    char name[64],line[96];
    level_name(name,sizeof(name));
    snprintf(line,sizeof(line),"LEVEL %d - %s",current_level+1,name);
    centered_outlined_text(28,line,
        (int)strlen(line)*12<=host->screen_width-24?2:1,settings.paper);
}

static void render_initials(void)
{
    char line[96],clock[24];
    int top=host->screen_height-35;
    rectangle(16,22,host->screen_width-32,host->screen_height-44,settings.background);
    snprintf(line,sizeof(line),"TOP 10 - LEVEL %d",current_level+1);
    centered_text(top,line,1,settings.phosphor);
    for (int row=0;row<HIGH_SCORE_COUNT;row++) {
        struct high_score entry={0};
        bool pending=row==score_rank;
        if (pending) {
            entry.ticks=completed_ticks;
            copy_text(entry.initials,sizeof(entry.initials),score_initials);
        } else {
            int source=row-(row>score_rank && score_rank>=0?1:0);
            if (source>=0 && source<HIGH_SCORE_COUNT) entry=high_scores[current_level][source];
        }
        if (entry.ticks>0) {
            format_time(entry.ticks,clock,sizeof(clock));
            snprintf(line,sizeof(line),"%02d  %s  %s",row+1,entry.initials,clock);
        } else snprintf(line,sizeof(line),"%02d  ---  --:--.--",row+1);
        int table_x=(host->screen_width-17*6)/2,baseline=top-15-row*10;
        if(pending && initials_blink>=30)line[4+initial_cursor]=' ';
        text(table_x,baseline,line,1,pending?settings.amber:settings.paper);
        if(pending)rectangle(table_x+(4+initial_cursor)*6,baseline-9,5,1,settings.amber);
    }
    char next[16],back[16];
    controller_label(CHIRKY_BUTTON_PRIMARY,"PRIMARY",next,sizeof(next));
    controller_label(CHIRKY_BUTTON_SECONDARY,"SECONDARY",back,sizeof(back));
    centered_text(39,"UP/DOWN LETTER",1,settings.edge);
    snprintf(line,sizeof(line),"%s %s  %s BACK",next,initial_cursor<2?"NEXT":"SAVE",back);
    centered_text(29,line,1,settings.edge);
}

void render_title(void)
{
    char jump[32],dash[32],menu[32],line[96];
    controller_label(CHIRKY_BUTTON_PRIMARY,"PRIMARY",jump,sizeof(jump));
    controller_label(CHIRKY_BUTTON_SECONDARY,"SECONDARY",dash,sizeof(dash));
    controller_label(CHIRKY_BUTTON_MENU,"MENU",menu,sizeof(menu));
    if (splash_draw(&title_art,host)) {
        rectangle(0,0,host->screen_width,48,settings.background);
        centered_text(39,"RESTORE THE LAST SIGNAL",1,settings.paper);
        centered_text(27,"PRIMARY JUMP / SECONDARY DASH",1,settings.edge);
        centered_text(17,"START USE LIFE / MENU PAUSE",1,settings.edge);
        centered_text(7,"PRIMARY - BEGIN",1,settings.amber);
        return;
    }
    render_background();
    int height=host->screen_height;
    centered_text(height-24,"PHOSPHOR",3,settings.phosphor);
    centered_text(height-54,"RUN",3,settings.amber);
    rectangle(16,height-80,host->screen_width-32,2,settings.edge);
    centered_text(height-98,"RESTORE THE LAST SIGNAL",1,settings.paper);
    centered_text(height-116,"ARROWS MOVE / START USE LIFE",1,settings.edge);
    snprintf(line,sizeof(line),"JUMP - %s",jump); centered_text(height-130,line,1,settings.edge);
    snprintf(line,sizeof(line),"DASH - %s",dash); centered_text(height-144,line,1,settings.edge);
    snprintf(line,sizeof(line),"%s - PAUSE",menu); centered_text(height-158,line,1,settings.edge);
    if ((title_timer/28)&1) {
        centered_text(14,"PRESS A BUTTON TO BEGIN",1,settings.paper);
    }
}

void render_game(void)
{
    zoom=level_intro_zoom();
    float blend=(zoom-1)/(LEVEL_INTRO_SCALE-1);
    float anchor_x=(int)(player_x-camera_x)+PLAYER_WIDTH/2;
    float anchor_y=(int)(player_y-camera_y)+9;
    shift_x=blend*(host->screen_width*.5f-anchor_x)-anchor_x*(zoom-1);
    shift_y=blend*(host->screen_height*.53f-anchor_y)-anchor_y*(zoom-1);
    scene_view=true;
    chirky_scope(host,"background",true);render_background();chirky_scope(host,"background",false);
    chirky_scope(host,"world",true);render_world();chirky_scope(host,"world",false);
    chirky_scope(host,"particles",true);render_particles();chirky_scope(host,"particles",false);
    chirky_scope(host,"player",true);
    int middle=host->screen_height/2;
    if (robot_ready() || (phase!=PHASE_DEAD && phase!=PHASE_GAME_OVER) || (death_timer&3)<2)
        render_player();
    chirky_scope(host,"player",false);
    scene_view=false;
    chirky_scope(host,"hud",true);render_hud();chirky_scope(host,"hud",false);
    if (phase==PHASE_LEVEL_INTRO) {
        render_level_intro();
    } else if (phase==PHASE_DEAD) {
        rectangle((host->screen_width-170)/2,middle-16,170,32,settings.background);
        centered_text(middle+5,"SIGNAL LOST",2,settings.hazard);
    } else if (phase==PHASE_INITIALS) {
        render_initials();
    } else if (phase==PHASE_WIN) {
        char confirm[32],line[96],clock[24];
        controller_label(CHIRKY_BUTTON_PRIMARY,"B",confirm,sizeof(confirm));
        rectangle(8,middle-58,host->screen_width-16,116,settings.background);
        centered_text(middle+29,"TRANSMISSION",3,settings.phosphor);
        centered_text(middle-2,"RESTORED",2,settings.paper);
        format_time(completed_ticks,clock,sizeof(clock));
        snprintf(line,sizeof(line),"TIME %s",clock);
        centered_text(middle-22,line,1,settings.edge);
        if (score_rank>=0) {
            if(score_save_failed)snprintf(line,sizeof(line),"SCORE NOT SAVED");
            else snprintf(line,sizeof(line),"TOP 10 RANK %02d",score_rank+1);
            centered_text(middle-34,line,1,settings.amber);
        }
        snprintf(line,sizeof(line),"%s - %s",confirm,current_level+1<content.level_count?"NEXT LEVEL":"RUN AGAIN");
        centered_text(middle-48,line,1,settings.amber);
    } else if (phase==PHASE_GAME_OVER) {
        char confirm[32],line[64];
        controller_label(CHIRKY_BUTTON_PRIMARY,"B",confirm,sizeof(confirm));
        rectangle(24,middle-42,host->screen_width-48,84,settings.background);
        centered_text(middle+15,"GAME OVER",3,settings.hazard);
        snprintf(line,sizeof(line),"%s - TITLE",confirm);
        centered_text(middle-24,line,1,settings.amber);
    }
}
