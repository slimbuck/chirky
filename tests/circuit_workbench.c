#include <assert.h>
#include "../tools/circuit-workbench.c"
int main(void)
{
    /* Native validation of the same adapter used in the dashboard WASM. */
    host=&wb_host;
    assert(read_character(host,"games/circuit-clash/assets/characters/arthur.conf",&characters[0]));
    characters[1]=characters[0];assert(read_stage(host,"games/circuit-clash/assets/stages/dojo.conf",&stage));
    artwork[0]=artwork[1]=1;
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
    characters[0].ground_accel_fp8=3;
    /* Artwork is selected independently of contact physics, with exact holds.
       Both facings use the same native cells. Short stun never grows to fit art. */
    for(int style=1;style<=2;style++)for(int direction=-1;direction<=1;direction+=2) {
        characters[1].hit_reaction_style=style;
        characters[1].hit_impact_ticks=2;characters[1].hit_recover_ticks=2;
        wb_configure(1,48);wb_reset(direction);wb_step(16);
        while(!contact_mask)wb_step(0);
        int first=style==1?CC_HIT_RESTRAINED_FIRST:CC_HIT_EXAGGERATED_FIRST;
        assert(wb_state()[44]==first);
        while(hitstop){wb_step(0);assert(wb_state()[44]==first);}
        for(int elapsed=1;elapsed<=8;elapsed++) {
            wb_step(0);
            assert(wb_state()[75]==8-elapsed);
            if(elapsed<8)assert(wb_state()[44]==first+(elapsed<2?0:elapsed<6?1:2));
            else assert(wb_state()[44]==CC_IDLE_FIRST);
        }
        for(int total=1;total<=4;total++) {
            fighters[1].reaction_ticks=total;fighters[1].stun=total;
            characters[1].hit_impact_ticks=12;characters[1].hit_recover_ticks=12;
            for(int left=total;left;left--) {
                fighters[1].stun=left;int frame=fighter_frame(1);
                assert(frame>=first && frame<first+3);
                if(total>=3 && left==1)assert(frame==first+2);
            }
        }
        fighters[1].stun=8;fighters[1].reaction_ticks=8;
        fighters[1].crouch=true;assert(fighter_frame(1)==CC_KNEEL_FIRST);
        fighters[1].crouch=false;fighters[1].y=8;assert(fighter_frame(1)==CC_JUMP_FIRST);
        fighters[1].y=0;fighters[1].guard_stun=true;assert(fighter_frame(1)==CC_BLOCK_FIRST);
        fighters[1].guard_stun=false;characters[1].hit_reaction_style=0;
        assert(fighter_frame(1)==CC_WALK_FIRST+2);
    }
    characters[1]=characters[0];
    for(int facing=-1;facing<=1;facing+=2)for(int mode=1;mode<=2;mode++) {
        wb_configure(mode,78);wb_reset(facing);
        for(int i=0;i<45;i++)wb_step(0);
        for(int i=0;i<18;i++)wb_step(facing>0?2:1);
        for(int i=0;i<12;i++)wb_step(0);
        wb_step(16);int contact=-1,frozen=0;
        for(int i=0;i<40;i++) {
            wb_step(0);const int *s=wb_state();
            if(s[82])contact=i;
            frozen+=s[81];
            assert(s[56]==96 && s[57]==96 && s[60]==96 && s[61]==96);
            assert(s[22]==(facing<0) && s[62]==(facing>0));
        }
        assert(contact>=0 && frozen==(mode==1?characters[0].punch_hitstop_ticks:characters[0].punch_blockstop_ticks));
        assert(wb_state()[74]==100-(mode==1?characters[0].punch_damage:0));
        assert(!wb_state()[75] && wb_state()[77]==wb_state()[78]);
    }
    /* A/B isolation, repeatable contact and gap-controlled misses. */
    wb_configure(1,120);wb_reset(1);for(int i=0;i<18;i++)wb_step(2);
    for(int i=0;i<12;i++)wb_step(0);
    wb_step(16);
    for(int i=0;i<30;i++){wb_step(0);assert(!wb_state()[80] && !wb_state()[82]);}
    assert(wb_state()[74]==100);
    wb_configure(0,78);wb_reset(1);assert(!wb_state()[83]);
    puts("Animation workbench: mirrored two-fighter contact, hit/block freezes, recoil, gap-controlled misses, solo replay and native sprite draws passed.");
}
