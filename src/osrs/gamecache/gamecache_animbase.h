#ifndef GAMECACHE_ANIMBASE_H
#define GAMECACHE_ANIMBASE_H

#include "gamecache_animframe.h"

/** Container for the decoded animbase frame array -- base field dropped (each frame owns its ptr). */
struct GameCacheAnimBaseFrames
{
    struct GameCacheAnimframe* frames;
    int frame_count;
};

#endif
