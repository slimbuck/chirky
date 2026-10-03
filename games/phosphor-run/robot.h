#ifndef PHOSPHOR_ROBOT_H
#define PHOSPHOR_ROBOT_H
#include "chirky.h"
#include <stdbool.h>
struct robot_tuning { float ambient,brightness,head_lead,body_lag,idle_look,blink; };
extern struct robot_tuning robot_style;

enum robot_clip { ROBOT_IDLE, ROBOT_RUN, ROBOT_JUMP, ROBOT_FALL, ROBOT_DASH, ROBOT_DEATH };
enum robot_expression { ROBOT_FACE_NEUTRAL, ROBOT_FACE_HAPPY, ROBOT_FACE_CURIOUS, ROBOT_FACE_SLEEPY };
struct robot_face {
    unsigned tick, requested_blink;
    enum robot_expression expression;
    float gaze_x,gaze_y,blink,left_open,right_open,smile;
};
void robot_face_init(struct robot_face *face);
void robot_face_update(struct robot_face *face);
void robot_face_blink(struct robot_face *face);
struct robot_motion {
    float roll, roll_speed, lean, lean_speed;
    int intent, delay;
};
void robot_motion_update(struct robot_motion *motion,int intent,float distance,bool grounded);
bool robot_draw_weighted(const struct chirky_host_api *api,int center_x,int floor_y,
                         int facing,enum robot_clip clip,float tick,const struct robot_motion *motion);
bool robot_load(const char *config_path);
bool robot_load_api(const struct chirky_host_api *api,const char *config_path);
void robot_free(void);
bool robot_ready(void);
/* Pure rendering: repeated renders of the same simulation state are identical. */
bool robot_draw(const struct chirky_host_api *api, int center_x, int floor_y,
                int facing, enum robot_clip clip, float tick);
bool robot_draw_scaled(const struct chirky_host_api *api, int center_x, int floor_y,
                       int facing, enum robot_clip clip, float tick, int scale);
bool robot_draw_view(const struct chirky_host_api *api,int center_x,int floor_y,int facing,
                     enum robot_clip clip,float tick,const struct robot_motion *motion,float scale);
bool robot_draw_character(const struct chirky_host_api *api,int center_x,int floor_y,int facing,
                     enum robot_clip clip,float tick,const struct robot_motion *motion,
                     const struct robot_face *face,float scale);
#endif
