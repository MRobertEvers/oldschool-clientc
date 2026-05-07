#ifndef GAMECACHE_SEQUENCE_H
#define GAMECACHE_SEQUENCE_H

/** Standalone GameCache sequence type -- fields read by world/interface animation paths and
 * player held-item override during primary sequences. */
struct GameCacheSequence
{
    int frame_count;
    int* frames;
    int* delay;
    /** Client SeqType.iframes — secondary morph animframe id per keyframe (-1 = none). */
    int* iframes;
    /** Client.ts SeqType.loops — frame rewind when animFrame reaches numFrames (interface MODEL). */
    int loops;
    /** -1 = no override for appearance slot 5 when building player mesh during primary anim. */
    int replaceheldleft;
    /** -1 = no override for appearance slot 3. */
    int replaceheldright;
    /** Client.ts SeqType.priority (default 5 when unset in seq.dat). */
    int priority;
    /** Client.ts RestartMode / duplicatebehaviour: 1 RESET, 2 RESETLOOP. */
    int duplicate_behavior;
    /** Client.ts SeqType.stretches — forward draw padding in World.addDynamic. */
    bool stretches;
};

#endif
