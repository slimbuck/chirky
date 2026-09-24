#ifndef PHOSPHOR_ROBOT_H
#define PHOSPHOR_ROBOT_H
#include "chirky.h"
#include <stdbool.h>
enum robot_clip { ROBOT_IDLE, ROBOT_RUN, ROBOT_JUMP, ROBOT_FALL, ROBOT_DASH, ROBOT_DEATH };
struct robot_motion {
    float roll, roll_speed, lean, lean_speed;
    int intent, delay;
};
void robot_motion_update(struct robot_motion *motion,int intent,float distance,bool grounded);
bool robot_draw_weighted(const struct chirky_host_api *api,int center_x,int floor_y,
                         int facing,enum robot_clip clip,float tick,const struct robot_motion *motion);
bool robot_load(const char *config_path);
void robot_free(void);
bool robot_ready(void);
/* Pure rendering: repeated renders of the same simulation state are identical. */
bool robot_draw(const struct chirky_host_api *api, int center_x, int floor_y,
                int facing, enum robot_clip clip, float tick);
#endif
