#ifndef LAUNCHER_CONFIG_H
#define LAUNCHER_CONFIG_H
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define LAUNCHER_MAX 40
struct launcher_item { char id[64],label[25]; int action; bool settings,visible; };
struct launcher_config { struct launcher_item items[LAUNCHER_MAX]; int count; };
static void launcher_add(struct launcher_config *c,const char *id,const char *label,int action,bool settings)
{
    if(c->count>=LAUNCHER_MAX)return;
    struct launcher_item *e=&c->items[c->count++];
    snprintf(e->id,sizeof(e->id),"%s",id);snprintf(e->label,sizeof(e->label),"%.24s",label);
    e->action=action;e->settings=settings;e->visible=true;
}
static int launcher_count(const struct launcher_config *c,bool settings)
{ int n=0;for(int i=0;i<c->count;i++)if(c->items[i].settings==settings && c->items[i].visible)n++;return n; }
static const struct launcher_item *launcher_at(const struct launcher_config *c,bool settings,int index)
{
    for(int i=0;i<c->count;i++)if(c->items[i].settings==settings && c->items[i].visible && index--==0)return &c->items[i];
    return NULL;
}
/* Invalid or partial files leave the complete default menu intact. */
static bool launcher_read(struct launcher_config *c,const char *path)
{
    FILE *f=fopen(path,"r");if(!f)return false;
    struct launcher_config next={0};bool seen[LAUNCHER_MAX]={0},valid=true;char line[256];
    while(fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;if(!line[0] || line[0]=='#')continue;
        char section[16],id[64],label[25],extra;int visible;
        if(sscanf(line,"%15[^|]|%63[^|]|%d|%24[^|]%c",section,id,&visible,label,&extra)!=4 || (visible!=0 && visible!=1)) { valid=false;break; }
        bool settings=!strcmp(section,"settings");
        if(!settings && strcmp(section,"root")){valid=false;break;}
        int found=-1;
        for(int i=0;i<c->count;i++)if(!strcmp(c->items[i].id,id) && c->items[i].settings==settings)found=i;
        if(found<0 || seen[found]){valid=false;break;}seen[found]=true;
        for(int i=0;label[i];i++)if((unsigned char)label[i]<32 || (unsigned char)label[i]>126)valid=false;
        next.items[next.count]=c->items[found];next.items[next.count].visible=visible;
        snprintf(next.items[next.count++].label,25,"%s",label);
    }
    if(ferror(f))valid=false;
    fclose(f);
    if(!valid || next.count!=c->count || !launcher_count(&next,false) || !launcher_count(&next,true))return false;
    *c=next;return true;
}
#endif
