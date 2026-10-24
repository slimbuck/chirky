#include <assert.h>
#include "../tools/circuit-workbench.c"
int main(void)
{
    /* Native validation of the same adapter used in the dashboard WASM. */
    host=&wb_host;
    assert(read_character(host,"games/circuit-clash/assets/characters/arthur.conf",&characters[0]));
    characters[1]=characters[0];assert(read_stage(host,"games/circuit-clash/assets/stages/dojo.conf",&stage));
    artwork[0]=1;
    for(int direction=-1;direction<=1;direction+=2) {
        wb_reset(direction);int start=fighters[0].x;
        for(int i=0;i<45;i++)wb_step(0);
        assert(fighters[0].x==start && wb_state()[4]<CC_WALK_FIRST);
        for(int i=0;i<18;i++) {
            wb_step(direction>0?2:1);
            assert(wb_state()[32]==(i<characters[0].push_off_ticks?MOTION_PUSH:MOTION_WALK));
        }
        assert((fighters[0].x-start)*direction>0 && fighters[0].vx==direction*12);
        wb_step(0);assert(fighters[0].vx==direction*6 && wb_state()[32]==MOTION_PLANT);
        wb_step(0);assert(!fighters[0].vx);
        for(int i=0;i<10;i++)wb_step(0);
        wb_step(16);assert(fighters[0].attack==1 && wb_state()[9]==1);
        for(int i=0;i<4;i++)wb_step(0);
        assert(wb_state()[8] && wb_state()[4]==CC_PUNCH_FIRST+1);
        for(int i=0;i<2;i++)wb_step(0);
        assert(!wb_state()[8] && wb_state()[9]==3 && wb_state()[4]==CC_PUNCH_FIRST+1);
        for(int i=0;i<4;i++)wb_step(0);
        assert(wb_state()[9]==4 && wb_state()[4]==CC_PUNCH_FIRST+2);
        const int *s=wb_state();assert(s[16]==96 && s[17]==96 && s[20]==96 && s[21]==96);
        assert(s[22]==(direction<0));assert(s[28]==CHIRKY_FRAMEBUFFER_WIDTH && s[29]==CHIRKY_FRAMEBUFFER_HEIGHT);
        for(int i=0;i<4;i++)wb_step(0);
        assert(!fighters[0].attack);
    }
    /* Reset and replay are deterministic, and the adapter reads the editable values. */
    wb_reset(1);for(int i=0;i<18;i++)wb_step(2);int first=fighters[0].x;
    wb_reset(1);for(int i=0;i<18;i++)wb_step(2);assert(fighters[0].x==first);
    characters[0].ground_accel_fp8=1;wb_reset(1);for(int i=0;i<18;i++)wb_step(2);assert(fighters[0].x<first);
    puts("Animation workbench: exact movement/combat phases, braking, mirrored draws, deterministic replay and draft tuning passed.");
}
