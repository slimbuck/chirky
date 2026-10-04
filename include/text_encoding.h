#ifndef CHIRKY_TEXT_ENCODING_H
#define CHIRKY_TEXT_ENCODING_H
#include <stddef.h>
#include <string.h>

#define CHIRKY_ARROW_LEFT  "\xe2\x86\x90"
#define CHIRKY_ARROW_UP    "\xe2\x86\x91"
#define CHIRKY_ARROW_RIGHT "\xe2\x86\x92"
#define CHIRKY_ARROW_DOWN  "\xe2\x86\x93"

/* The console font supports ASCII and these four UTF-8 direction symbols.
   Keep width, clipping, software drawing and GPU drawing on one decoder. */
static inline unsigned chirky_text_next(const char **text)
{
    const unsigned char *p=(const unsigned char *)*text;
    unsigned c=*p;if(!c)return 0;
    if(c==0xe2 && p[1]==0x86 && p[2]>=0x90 && p[2]<=0x93) {
        *text+=3;return 0x2190+p[2]-0x90;
    }
    (*text)++;return c<128?c:'?';
}
static inline size_t chirky_text_length(const char *text)
{
    size_t length=0;while(chirky_text_next(&text))length++;return length;
}
static inline void chirky_text_copy(char *out,size_t capacity,const char *text,size_t columns)
{
    if(!capacity)return;
    size_t used=0;
    while(*text && columns--) {
        const char *start=text;chirky_text_next(&text);size_t bytes=(size_t)(text-start);
        if(bytes>=capacity-used)break;
        memcpy(out+used,start,bytes);used+=bytes;
    }
    out[used]=0;
}
#endif
