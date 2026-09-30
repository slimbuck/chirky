#include "game_state.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void score_key(int index,char key[96])
{ snprintf(key,96,"scores-%s",content.levels[index].id); }
void scores_load(void)
{
    if(!host->save_read)return;
    for(int index=0;index<content.level_count;index++){
        char key[96],data[256];score_key(index,key);
        size_t size=host->save_read(host->context,"phosphor-run",key,data,sizeof(data)-1);
        if(!size || size>=sizeof(data) || memchr(data,0,size))continue;
        data[size]=0;
        if(strncmp(data,"CHIRKY-SCORES-1\n",16))continue;
        struct high_score entries[HIGH_SCORE_COUNT]={0};int count=0;bool valid=true;
        char *line=data+16;
        while(*line && count<HIGH_SCORE_COUNT){
            char *end;errno=0;long ticks=strtol(line,&end,10);
            if(errno==ERANGE || *line<'0' || *line>'9' || end==line || ticks<=0 || ticks>INT_MAX || *end!=' ' || strlen(end)<5 ||
                end[1]<'A' || end[1]>'Z' || end[2]<'A' || end[2]>'Z' || end[3]<'A' || end[3]>'Z' ||
                end[4]!='\n' || (count && entries[count-1].ticks>ticks)){valid=false;break;}
            entries[count].ticks=(int)ticks;memcpy(entries[count].initials,end+1,3);count++;line=end+5;
        }
        if(valid && !*line)memcpy(high_scores[index],entries,sizeof(entries));
    }
}
bool scores_save(int index)
{
    if(!host->save_write)return false;
    char key[96],data[256]="CHIRKY-SCORES-1\n";score_key(index,key);size_t used=strlen(data);
    for(int row=0;row<HIGH_SCORE_COUNT && high_scores[index][row].ticks;row++){
        const struct high_score *entry=&high_scores[index][row];
        int count=snprintf(data+used,sizeof(data)-used,"%d %.3s\n",entry->ticks,entry->initials);
        if(count<0 || (size_t)count>=sizeof(data)-used)return false;
        used+=(size_t)count;
    }
    return host->save_write(host->context,"phosphor-run",key,data,used);
}
