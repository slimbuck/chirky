#ifndef CHIRKY_CONTROLLER_PROFILES_H
#define CHIRKY_CONTROLLER_PROFILES_H
#include "input_bindings.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CONTROLLER_PROFILES_PATH "config/controllers.conf"
#define CONTROLLER_PROFILE_LIMIT 32
struct saved_controller {
    char model[40];
    enum controller_profile profile;
    struct controller_binding bindings[CHIRKY_BUTTON_COUNT];
};
struct controller_profiles { unsigned count; struct saved_controller items[CONTROLLER_PROFILE_LIMIT]; };

/* USB identity plus reported name: stable across ports and connection order. */
static void controller_model(char *out,size_t capacity,const struct input_id *id,const char *name)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)name;*p;p++){hash^=*p;hash*=UINT64_C(1099511628211);}
    snprintf(out,capacity,"%04x%04x%04x%04x%016" PRIx64,id->bustype,id->vendor,id->product,id->version,hash);
}
static struct saved_controller *controller_profile_find(struct controller_profiles *profiles,const char *model)
{
    for(unsigned i=0;i<profiles->count;i++)if(!strcmp(profiles->items[i].model,model))return &profiles->items[i];
    return NULL;
}
static bool controller_map_valid(const struct controller_binding *map)
{
    for(int i=0;i<CHIRKY_BUTTON_COUNT;i++) {
        const struct controller_binding *b=&map[i];
        if(!((b->kind==BINDING_KEY && b->code<=KEY_MAX && !b->direction) ||
             (b->kind==BINDING_ABS && b->code<=ABS_MAX && (b->direction==-1 || b->direction==1))))return false;
        for(int j=0;j<i;j++)if(b->kind==map[j].kind && b->code==map[j].code && b->direction==map[j].direction)return false;
    }
    return true;
}
static void controller_profiles_load(struct controller_profiles *profiles)
{
    *profiles=(struct controller_profiles){0};
    FILE *file=fopen(CONTROLLER_PROFILES_PATH,"r");if(!file)return;
    char line[1024];
    while(fgets(line,sizeof(line),file) && profiles->count<CONTROLLER_PROFILE_LIMIT) {
        struct saved_controller item={0};unsigned version,profile;int used=0;
        if(sscanf(line,"%u %39s %u %n",&version,item.model,&profile,&used)!=3 || version!=1 || profile>CONTROLLER_SNES)continue;
        char *cursor=line+used;bool valid=true;item.profile=(enum controller_profile)profile;
        for(int b=0;b<CHIRKY_BUTTON_COUNT;b++) {
            unsigned kind,code;int direction;
            if(sscanf(cursor,"%u %u %d %n",&kind,&code,&direction,&used)!=3){valid=false;break;}
            item.bindings[b]=(struct controller_binding){kind,code,direction};cursor+=used;
        }
        if(valid && !*cursor && controller_map_valid(item.bindings) && !controller_profile_find(profiles,item.model))
            profiles->items[profiles->count++]=item;
    }
    fclose(file);
}
static bool controller_profiles_save(struct controller_profiles *profiles,const char *model,
    enum controller_profile profile,const struct controller_binding *bindings)
{
    if((unsigned)profile>CONTROLLER_SNES || !*model || strlen(model)>=sizeof(profiles->items[0].model) || !controller_map_valid(bindings))return false;
    struct saved_controller *target=controller_profile_find(profiles,model);
    if(!target && profiles->count==CONTROLLER_PROFILE_LIMIT)return false;
    bool added=!target;
    if(added)target=&profiles->items[profiles->count++];
    struct saved_controller original=*target;
    *target=(struct saved_controller){.profile=profile};snprintf(target->model,sizeof(target->model),"%s",model);
    memcpy(target->bindings,bindings,sizeof(target->bindings));
    FILE *file=fopen(CONTROLLER_PROFILES_PATH ".tmp","w");bool ok=file!=NULL;
    if(file) {
        for(unsigned i=0;i<profiles->count;i++) {
            const struct saved_controller *p=&profiles->items[i];
            fprintf(file,"1 %s %u",p->model,p->profile);
            for(int b=0;b<CHIRKY_BUTTON_COUNT;b++)fprintf(file," %u %u %d",p->bindings[b].kind,p->bindings[b].code,p->bindings[b].direction);
            fputc('\n',file);
        }
        if(ferror(file) || fflush(file) || fsync(fileno(file)))ok=false;
        if(fclose(file))ok=false;
        if(ok && rename(CONTROLLER_PROFILES_PATH ".tmp",CONTROLLER_PROFILES_PATH))ok=false;
    }
    if(!ok){*target=original;if(added)profiles->count--;unlink(CONTROLLER_PROFILES_PATH ".tmp");}
    return ok;
}
#endif
