#include "frame_draw_gate.h"
#include <stdio.h>
#define CHECK(test) do { if( !(test) ) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test); return 1; } } while( 0 )
int main(void)
{
    for( int fps=15; fps<=50; fps+=35 )
    {
        struct ToriRS_DrawGate gate={0};
        int draws=0;
        for( int loop=0; loop<500; ++loop )
            draws += ToriRS_DrawGateAllow(&gate, (uint64_t)loop*20000, fps);
        CHECK(draws==fps*10);
        /* A long pause produces one current frame, never a catch-up burst. */
        CHECK(ToriRS_DrawGateAllow(&gate,20000000,fps));
        CHECK(!ToriRS_DrawGateAllow(&gate,20000000,fps));
    }
    struct ToriRS_DrawGate busy={0};
    int draws=0;
    for( int loop=0; loop<10000; ++loop )
        draws += ToriRS_DrawGateAllow(&busy,(uint64_t)loop*1000,15);
    CHECK(draws==150);
    puts("draw gate: exact 15/50 fps, busy-loop cap, no catch-up burst passed");
    return 0;
}
