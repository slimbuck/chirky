#include "launcher_config.h"
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
int main(void) {
    struct launcher_config c={0};
    launcher_add(&c,"game","Game",0,false);launcher_add(&c,"settings","Settings",-1,false);
    launcher_add(&c,"input","Input",-3,true);launcher_add(&c,"display","Display",-4,true);
    char path[]="/tmp/launcher-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);FILE *f=fdopen(fd,"w");assert(f);
    fputs("root|settings|1|Options\nroot|game|0|Hidden Game\nsettings|display|1|Screen\nsettings|input|1|Controls\n",f);fclose(f);
    assert(launcher_read(&c,path));assert(launcher_count(&c,false)==1);
    assert(launcher_at(&c,false,0)->action==-1 && !strcmp(launcher_at(&c,false,0)->label,"Options"));
    assert(launcher_at(&c,true,0)->action==-4 && launcher_at(&c,true,1)->action==-3);
    f=fopen(path,"w");assert(f);fputs("root|settings|1|Broken\n",f);fclose(f);
    assert(!launcher_read(&c,path));assert(!strcmp(launcher_at(&c,false,0)->label,"Options"));
    assert(remove(path)==0);puts("Launcher configuration: action mapping, ordering, visibility and invalid-file fallback passed.");
}
