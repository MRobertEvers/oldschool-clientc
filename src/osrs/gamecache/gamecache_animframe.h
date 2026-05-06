#ifndef GAMECACHE_ANIMFRAME_H
#define GAMECACHE_ANIMFRAME_H

#include <stdint.h>

/** Standalone GameCache animbase -- same 4 fields as CacheAnimBase, no rscache dependency. */
struct GameCacheAnimBase
{
    int length;
    uint8_t* types;
    uint8_t** labels;
    uint16_t* label_counts;
};

/** Standalone GameCache animframe -- all 8 fields kept; base points to GameCacheAnimBase. */
struct GameCacheAnimframe
{
    int id;
    struct GameCacheAnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

#endif
