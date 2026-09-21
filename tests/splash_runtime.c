#include "splash_art.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char screen[240][320][3];
static void paint(void *ctx,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b)
{
    const struct two_forty_host_api *api=ctx;
    assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=api->screen_width && y+h<=api->screen_height);
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++) {
        screen[yy][xx][0]=r;screen[yy][xx][1]=g;screen[yy][xx][2]=b;
    }
}
int main(void)
{
    struct splash_art art={0};
    const char *games[]={"games/phosphor-run/game.conf","games/rosey-chop/game.conf"};
    for(int game=0;game<2;game++) {
        assert(splash_load(&art,games[game]));
        assert(art.width==288 && art.height==216);
        const int sizes[][2]={{320,240},{288,216},{256,192}};
        for(int i=0;i<3;i++) {
            struct two_forty_host_api api={.screen_width=sizes[i][0],.screen_height=sizes[i][1],.fill_rect=paint};
            api.context=&api;
            assert(splash_draw(&art,&api));
            /* Integer edge scaling maps each destination pixel to the last
               source cell starting at that edge, with top-down art flipped. */
            for(int y=0;y<api.screen_height;y++)for(int x=0;x<api.screen_width;x++) {
                int sx=((x+1)*art.width-1)/api.screen_width;
                int sy=((y+1)*art.height-1)/api.screen_height;
                assert(!memcmp(screen[api.screen_height-1-y][x],art.pixels+(sy*art.width+sx)*3,3));
            }
        }
    }
    char dir[]="/tmp/two-forty-art-XXXXXX",path[256],config[256];
    assert(mkdtemp(dir));
    snprintf(path,sizeof(path),"%s/assets",dir);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/assets/artwork",dir);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/assets/artwork/splash.ppm",dir);
    snprintf(config,sizeof(config),"%s/game.conf",dir);
    const char *bad[]={"P6\n321 240\n255\n","P6\n1 1\n255\n\1","P3\n1 1\n255\n0 0 0","P6\n-1 1\n255\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        FILE *file=fopen(path,"wb");assert(file);fwrite(bad[i],1,strlen(bad[i]),file);fclose(file);
        assert(!splash_load(&art,config) && !art.pixels);
    }
    FILE *file=fopen(path,"wb");assert(file);
    fputs("P6\n1 1\n255\n",file);
    const unsigned char bytes[]={10,32,0};fwrite(bytes,1,3,file);fclose(file);
    assert(splash_load(&art,config) && !memcmp(art.pixels,bytes,3));
    remove(path);
    assert(!splash_load(&art,config));splash_free(&art);splash_free(&art);
    snprintf(path,sizeof(path),"%s/assets/artwork",dir);rmdir(path);
    snprintf(path,sizeof(path),"%s/assets",dir);rmdir(path);rmdir(dir);
    puts("Splash art: shipped images, pixel orientation, safe viewport scaling, invalid files and cleanup passed.");
}
