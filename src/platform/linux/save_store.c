#include "save_store.h"
#include "save_data.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static bool directory(const char *path)
{
    struct stat st;
    return (mkdir(path,0700)==0 || errno==EEXIST) && stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
size_t save_store_read(void *unused,const char *game,const char *key,void *data,size_t capacity)
{
    (void)unused;
    if(!chirky_save_name(game) || !chirky_save_name(key) || !data || !capacity)return 0;
    char path[224];snprintf(path,sizeof(path),"saves/%s/%s",game,key);
    FILE *file=fopen(path,"rb");if(!file)return 0;
    size_t limit=capacity<CHIRKY_SAVE_LIMIT?capacity:CHIRKY_SAVE_LIMIT;
    size_t size=fread(data,1,limit,file);
    bool ok=!ferror(file) && fgetc(file)==EOF && !ferror(file);
    fclose(file);return ok?size:0;
}
bool save_store_write(void *unused,const char *game,const char *key,const void *data,size_t size)
{
    (void)unused;
    if(!chirky_save_name(game) || !chirky_save_name(key) || !data || !size || size>CHIRKY_SAVE_LIMIT)return false;
    char folder[112],path[224],temporary[240];snprintf(folder,sizeof(folder),"saves/%s",game);
    if(!directory("saves") || !directory(folder))return false;
    snprintf(path,sizeof(path),"%s/%s",folder,key);snprintf(temporary,sizeof(temporary),"%s.tmp-XXXXXX",path);
    int fd=mkstemp(temporary);if(fd<0)return false;
    FILE *file=fdopen(fd,"wb");if(!file){close(fd);unlink(temporary);return false;}
    bool ok=fwrite(data,1,size,file)==size && fflush(file)==0 && fsync(fd)==0;
    if(fclose(file)!=0)ok=false;
    if(ok)ok=rename(temporary,path)==0;
    if(!ok)unlink(temporary);
    return ok;
}
