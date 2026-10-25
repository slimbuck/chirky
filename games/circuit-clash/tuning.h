#ifndef CIRCUIT_TUNING_H
#define CIRCUIT_TUNING_H
#include "asset_file.h"
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* This field list also generates the future editor's numeric schema. Speeds
   use eighths of a pixel per update; timers count game updates (nominally 60Hz). */
#define CC_CHARACTER_FIELDS(X) \
 X(schema_version,10,10) \
 X(hit_reaction_style,0,2) \
 X(hit_impact_ticks,1,12) \
 X(hit_recover_ticks,1,12) \
 X(health,1,10000) \
 X(weight_percent,25,400) \
 X(walk_speed_fp8,1,64) \
 X(back_speed_fp8,1,64) \
 X(ground_accel_fp8,1,64) \
 X(ground_brake_fp8,1,64) \
 X(push_off_ticks,0,30) \
 X(stop_plant_ticks,0,30) \
 X(stop_sink_ticks,0,30) \
 X(stop_rise_ticks,0,30) \
 X(air_speed_fp8,1,64) \
 X(air_accel_fp8,1,32) \
 X(jump_velocity_fp8,1,80) \
 X(gravity_fp8,1,16) \
 X(fall_gravity_fp8,1,24) \
 X(max_fall_speed_fp8,1,96) \
 X(jump_startup_ticks,0,60) \
 X(jump_buffer_ticks,0,30) \
 X(landing_ticks,0,60) \
 X(air_attack_landing_ticks,0,60) \
 X(air_punch_startup_ticks,0,120) \
 X(air_punch_active_ticks,1,120) \
 X(air_punch_recovery_ticks,1,120) \
 X(air_punch_followthrough_ticks,0,119) \
 X(air_punch_hitstop_ticks,0,12) \
 X(air_punch_blockstop_ticks,0,12) \
 X(air_punch_recoil_ticks,0,30) \
 X(air_kick_startup_ticks,0,120) \
 X(air_kick_active_ticks,1,120) \
 X(air_kick_recovery_ticks,1,120) \
 X(air_kick_followthrough_ticks,0,119) \
 X(air_kick_hitstop_ticks,0,12) \
 X(air_kick_blockstop_ticks,0,12) \
 X(air_kick_recoil_ticks,0,30) \
 X(block_release_ticks,0,60) \
 X(block_chord_ticks,0,6) \
 X(block_knockback_percent,0,100) \
 X(crouch_enter_ticks,0,60) \
 X(crouch_exit_ticks,0,60) \
 X(body_half_width_px,1,24) \
 X(standing_height_px,8,64) \
 X(crouching_height_px,8,64) \
 X(idle_0_ticks,1,600) \
 X(idle_1_ticks,1,600) \
 X(idle_2_ticks,1,600) \
 X(idle_3_ticks,1,600) \
 X(walk_0_ticks,1,600) \
 X(walk_1_ticks,1,600) \
 X(walk_2_ticks,1,600) \
 X(walk_3_ticks,1,600) \
 X(punch_startup_ticks,0,120) \
 X(punch_active_ticks,1,120) \
 X(punch_recovery_ticks,1,120) \
 X(punch_followthrough_ticks,0,119) \
 X(punch_hitstop_ticks,0,12) \
 X(punch_blockstop_ticks,0,12) \
 X(punch_recoil_ticks,0,30) \
 X(punch_damage,0,1000) \
 X(punch_reach_px,1,96) \
 X(punch_bottom_px,0,64) \
 X(punch_top_px,1,96) \
 X(punch_hitstun_ticks,1,180) \
 X(punch_blockstun_ticks,1,180) \
 X(punch_knockback_px,0,64) \
 X(kick_startup_ticks,0,120) \
 X(kick_active_ticks,1,120) \
 X(kick_recovery_ticks,1,120) \
 X(kick_followthrough_ticks,0,119) \
 X(kick_hitstop_ticks,0,12) \
 X(kick_blockstop_ticks,0,12) \
 X(kick_recoil_ticks,0,30) \
 X(kick_damage,0,1000) \
 X(kick_reach_px,1,96) \
 X(kick_bottom_px,0,64) \
 X(kick_top_px,1,96) \
 X(kick_hitstun_ticks,1,180) \
 X(kick_blockstun_ticks,1,180) \
 X(kick_knockback_px,0,64) \
 X(crouch_punch_startup_ticks,0,120) \
 X(crouch_punch_active_ticks,1,120) \
 X(crouch_punch_recovery_ticks,1,120) \
 X(crouch_punch_followthrough_ticks,0,119) \
 X(crouch_punch_hitstop_ticks,0,12) \
 X(crouch_punch_blockstop_ticks,0,12) \
 X(crouch_punch_recoil_ticks,0,30) \
 X(crouch_punch_damage,0,1000) \
 X(crouch_punch_reach_px,1,96) \
 X(crouch_punch_bottom_px,0,64) \
 X(crouch_punch_top_px,1,96) \
 X(crouch_punch_hitstun_ticks,1,180) \
 X(crouch_punch_blockstun_ticks,1,180) \
 X(crouch_punch_knockback_px,0,64) \
 X(crouch_kick_startup_ticks,0,120) \
 X(crouch_kick_active_ticks,1,120) \
 X(crouch_kick_recovery_ticks,1,120) \
 X(crouch_kick_followthrough_ticks,0,119) \
 X(crouch_kick_hitstop_ticks,0,12) \
 X(crouch_kick_blockstop_ticks,0,12) \
 X(crouch_kick_recoil_ticks,0,30) \
 X(crouch_kick_damage,0,1000) \
 X(crouch_kick_reach_px,1,96) \
 X(crouch_kick_bottom_px,0,64) \
 X(crouch_kick_top_px,1,96) \
 X(crouch_kick_hitstun_ticks,1,180) \
 X(crouch_kick_blockstun_ticks,1,180) \
 X(crouch_kick_knockback_px,0,64) \
 X(air_punch_damage,0,1000) \
 X(air_punch_reach_px,1,96) \
 X(air_punch_bottom_px,0,64) \
 X(air_punch_top_px,1,96) \
 X(air_punch_hitstun_ticks,1,180) \
 X(air_punch_blockstun_ticks,1,180) \
 X(air_punch_knockback_px,0,64) \
 X(air_kick_damage,0,1000) \
 X(air_kick_reach_px,1,96) \
 X(air_kick_bottom_px,0,64) \
 X(air_kick_top_px,1,96) \
 X(air_kick_hitstun_ticks,1,180) \
 X(air_kick_blockstun_ticks,1,180) \
 X(air_kick_knockback_px,0,64)
#define CC_STAGE_FIELDS(X) \
 X(schema_version,1,1) \
 X(floor_y_px,32,60) \
 X(wall_margin_px,26,48) \
 X(spawn_left_percent,15,40) \
 X(spawn_right_percent,60,85) \
 X(countdown_ticks,0,600)
#define CC_DECLARE(n,lo,hi) int n;
struct character_tuning { CC_CHARACTER_FIELDS(CC_DECLARE) char name[24],atlas[128]; };
struct stage_tuning { CC_STAGE_FIELDS(CC_DECLARE) char name[24]; };
#undef CC_DECLARE
struct tuning_field { const char *name; size_t offset; int min,max; };
#define CC_CHAR_FIELD(n,lo,hi) {#n,offsetof(struct character_tuning,n),lo,hi},
static const struct tuning_field character_fields[]={CC_CHARACTER_FIELDS(CC_CHAR_FIELD)};
#undef CC_CHAR_FIELD
#define CC_STAGE_FIELD(n,lo,hi) {#n,offsetof(struct stage_tuning,n),lo,hi},
static const struct tuning_field stage_fields[]={CC_STAGE_FIELDS(CC_STAGE_FIELD)};
#undef CC_STAGE_FIELD
#define CC_COUNT(a) (sizeof(a)/sizeof((a)[0]))

static char *trim(char *s)
{
    while(isspace((unsigned char)*s))s++;
    char *end=s+strlen(s);while(end>s && isspace((unsigned char)end[-1]))*--end=0;
    return s;
}
static bool content_id(const char *s)
{
    if(!*s || strlen(s)>63)return false;
    for(;*s;s++)if(!((*s>='a' && *s<='z') || isdigit((unsigned char)*s) || *s=='-'))return false;
    return true;
}
static bool content_path(const char *s)
{
    if(strncmp(s,"assets/",7) || strstr(s,"..") || strstr(s,"//"))return false;
    for(;*s;s++)if(!isalnum((unsigned char)*s) && *s!='/' && *s!='-' && *s!='_' && *s!='.')return false;
    return true;
}
static bool read_tuning(const struct chirky_host_api *api,const char *path,void *out,
                        const struct tuning_field *fields,size_t count,char *name,char *atlas)
{
    struct chirky_file file=chirky_file_open(api,path);
    if(!file.stream)return false;
    bool seen[CC_COUNT(character_fields)]={0},named=false,art=false,ok=true;
    char line[512];
    while(ok && fgets(line,sizeof(line),file.stream)) {
        if(!strchr(line,'\n') && !feof(file.stream)){ok=false;break;}
        char *key=trim(line);if(!*key || *key=='#')continue;
        char *value=strchr(key,'=');if(!value){ok=false;break;}
        *value++=0;key=trim(key);value=trim(value);
        if(!strcmp(key,"name")) {
            if(named || !*value || strlen(value)>23){ok=false;break;}
            for(char *p=value;*p;p++)if((unsigned char)*p<32 || (unsigned char)*p>126)ok=false;
            strcpy(name,value);named=true;continue;
        }
        if(atlas && !strcmp(key,"atlas")) {
            if(art || strlen(value)>127 || !content_path(value)){ok=false;break;}
            strcpy(atlas,value);art=true;continue;
        }
        size_t i;for(i=0;i<count;i++)if(!strcmp(key,fields[i].name))break;
        if(i==count || seen[i]){ok=false;break;}
        char *end;errno=0;long number=strtol(value,&end,10);
        if(errno || !*value || *end || number<fields[i].min || number>fields[i].max){ok=false;break;}
        *(int *)((char *)out+fields[i].offset)=(int)number;seen[i]=true;
    }
    if(ferror(file.stream))ok=false;
    chirky_file_close(&file);
    for(size_t i=0;i<count;i++)ok=ok && seen[i];
    return ok && named && (!atlas || art);
}
static bool read_character(const struct chirky_host_api *api,const char *path,struct character_tuning *out)
{
    struct character_tuning value={0};
    if(!read_tuning(api,path,&value,character_fields,CC_COUNT(character_fields),value.name,value.atlas) ||
       value.crouching_height_px>value.standing_height_px ||
       value.punch_bottom_px>=value.punch_top_px ||
       value.kick_bottom_px>=value.kick_top_px ||
       value.crouch_punch_bottom_px>=value.crouch_punch_top_px ||
       value.crouch_kick_bottom_px>=value.crouch_kick_top_px ||
       value.air_punch_bottom_px>=value.air_punch_top_px ||
       value.air_kick_bottom_px>=value.air_kick_top_px ||
       value.punch_followthrough_ticks>=value.punch_recovery_ticks ||
       value.kick_followthrough_ticks>=value.kick_recovery_ticks ||
       value.crouch_punch_followthrough_ticks>=value.crouch_punch_recovery_ticks ||
       value.crouch_kick_followthrough_ticks>=value.crouch_kick_recovery_ticks ||
       value.air_punch_followthrough_ticks>=value.air_punch_recovery_ticks ||
       value.air_kick_followthrough_ticks>=value.air_kick_recovery_ticks)return false;
    *out=value;return true;
}
static bool read_stage(const struct chirky_host_api *api,const char *path,struct stage_tuning *out)
{
    struct stage_tuning value={0};
    if(!read_tuning(api,path,&value,stage_fields,CC_COUNT(stage_fields),value.name,NULL))return false;
    *out=value;return true;
}
#endif
