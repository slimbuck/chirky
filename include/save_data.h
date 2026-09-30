#ifndef CHIRKY_SAVE_DATA_H
#define CHIRKY_SAVE_DATA_H
#include <stdbool.h>
#include <stddef.h>
#define CHIRKY_SAVE_LIMIT 65536u
static inline bool chirky_save_name(const char *name)
{
    if(!name || !*name)return false;
    size_t n=0;
    for(;*name;name++,n++)if(n>=95 || !((*name>='a' && *name<='z') ||
        (*name>='A' && *name<='Z') || (*name>='0' && *name<='9') || *name=='-' || *name=='_'))return false;
    return true;
}
#endif
