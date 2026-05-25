#ifndef GAMECACHE_SEQUENCE_H
#define GAMECACHE_SEQUENCE_H

#include <stdbool.h>

struct CacheDatSequence;

struct GameCache_Sequence
{
    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    int* walkmerge;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
};

struct GameCache_Sequence*
gamecache_sequence_new_from_cache_dat_sequence(struct CacheDatSequence* sequence);

void
gamecache_sequence_free(struct GameCache_Sequence* sequence);

#endif
