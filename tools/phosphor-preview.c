/* Development-only viewer. Uses the exact game pose evaluator and host GPU renderer. */
#include "../games/phosphor-run/robot.h"
#include "rect_renderer.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#include <math.h>
static struct rect_renderer renderer;
static struct chirky_host_api api;
static struct robot_face face;
static bool mesh(void *unused,const struct chirky_mesh_vertex *v,size_t count,float ambient)
{ (void)unused;return rect_renderer_mesh(&renderer,v,count,ambient,0,0,320,240); }
EMSCRIPTEN_KEEPALIVE int preview_init(void)
{
    EmscriptenWebGLContextAttributes attrs;emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha=0;attrs.depth=1;attrs.antialias=0;attrs.stencil=0;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context=emscripten_webgl_create_context("#screen",&attrs);
    if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS)return 0;
    if(!rect_renderer_init(&renderer,320,240))return 0;
    api=(struct chirky_host_api){.screen_width=320,.screen_height=240,.draw_mesh=mesh};
    robot_face_init(&face);
    return robot_load("games/phosphor-run/game.conf");
}
EMSCRIPTEN_KEEPALIVE int preview_reload(void)
{ return robot_load("games/phosphor-run/game.conf"); }
EMSCRIPTEN_KEEPALIVE void preview_style(float ambient,float brightness,float lead,float lag,float look,int blink)
{ robot_style=(struct robot_tuning){ambient,brightness,lead,lag,look,(float)blink}; }
EMSCRIPTEN_KEEPALIVE int preview_face(int steps,int expression,int blink,int clip)
{
    if(expression>=ROBOT_FACE_NEUTRAL && expression<=ROBOT_FACE_AUTO)face.expression=(enum robot_expression)expression;
    face.idle=clip==ROBOT_IDLE;
    if(blink)robot_face_blink(&face);
    for(int i=0;i<steps && i<12;i++)robot_face_update(&face);
    return (int)face.tick;
}
EMSCRIPTEN_KEEPALIVE int preview_render(int clip,float tick,int facing,float scale)
{
    glViewport(0,0,320,240);glDisable(GL_SCISSOR_TEST);glClearColor(.025f,.05f,.065f,1);glClear(GL_COLOR_BUFFER_BIT);
    rect_renderer_begin(&renderer);
    for(int x=0;x<320;x+=16)rect_renderer_rect(&renderer,x,0,1,240,12,25,30);
    for(int y=0;y<240;y+=16)rect_renderer_rect(&renderer,0,y,320,1,12,25,30);
    int floor=48-(int)fmaxf(0,(scale-8)*8);
    rect_renderer_rect(&renderer,0,floor-1,320,1,72,105,110);
    struct robot_motion motion={0};
    if(clip==ROBOT_RUN)for(int i=0;i<(int)tick;i++)robot_motion_update(&motion,facing,2.35f*facing,true);
    bool ok=robot_draw_character(&api,160,floor,facing,(enum robot_clip)clip,tick,&motion,&face,scale);
    rect_renderer_flush(&renderer);return ok && glGetError()==GL_NO_ERROR;
}
