#ifndef GAMECACHE_SEQUENCE_H
#define GAMECACHE_SEQUENCE_H

/** Standalone GameCache sequence type -- only fields read by world/interface animation paths. */
struct GameCacheSequence
{
    int frame_count;
    int* frames;
    int* delay;
};

#endif
