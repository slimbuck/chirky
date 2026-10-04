#ifndef CHIRKY_INPUT_LABELS_H
#define CHIRKY_INPUT_LABELS_H
#include "chirky.h"
#include "text_encoding.h"
#include <stdio.h>
#include <string.h>

/* Presentation only: resolve on each draw so remapping/device changes are live. */
static inline void chirky_input_label(const struct chirky_host_api *api,
    enum chirky_button action,char *out,size_t size)
{
    if(!size)return;
    snprintf(out,size,"UNBOUND");
    if(api->button_label)api->button_label(api->context,action,out,size);
    out[size-1]=0;
}

/* Compact a real direction group, never assume that movement uses arrows. */
static inline void chirky_direction_label(const struct chirky_host_api *api,
    int first,int count,char *out,size_t size)
{
    if(!size)return;
    if(first<0 || first>=4 || count<1 || count>4-first){snprintf(out,size,"UNBOUND");return;}
    char labels[4][24];bool arrows=true;
    for(int i=0;i<count;i++) {
        chirky_input_label(api,(enum chirky_button)(first+i),labels[i],sizeof(labels[i]));
        const char *p=labels[i];unsigned c=chirky_text_next(&p);
        arrows=arrows && c>=0x2190 && c<=0x2193 && !*p;
    }
    if(size)out[0]=0;
    for(int i=0;i<count && size;i++) {
        size_t used=strlen(out);
        if(i && !arrows && used+1<size)out[used++]='/';
        chirky_text_copy(out+used,size-used,labels[i],24);
    }
}
#endif
