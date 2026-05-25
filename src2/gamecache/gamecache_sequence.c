#include "gamecache_sequence.h"

#include "osrs/rscache/tables/config_sequence.h"

#include <stdlib.h>
#include <string.h>

struct GameCache_Sequence*
gamecache_sequence_new_from_cache_dat_sequence(struct CacheDatSequence* sequence)
{
    if( !sequence )
        return NULL;

    struct GameCache_Sequence* seq = malloc(sizeof(struct GameCache_Sequence));
    if( !seq )
        return NULL;

    memset(seq, 0, sizeof(struct GameCache_Sequence));
    seq->frame_count = sequence->frame_count;
    seq->frames = sequence->frames;
    seq->iframes = sequence->iframes;
    seq->delay = sequence->delay;
    seq->loops = sequence->loops;
    seq->walkmerge = sequence->walkmerge;
    seq->stretches = sequence->stretches;
    seq->priority = sequence->priority;
    seq->replaceheldleft = sequence->replaceheldleft;
    seq->replaceheldright = sequence->replaceheldright;
    seq->maxloops = sequence->maxloops;
    seq->preanim_move = sequence->preanim_move;
    seq->postanim_move = sequence->postanim_move;
    seq->duplicate_behavior = sequence->duplicate_behavior;

    sequence->frames = NULL;
    sequence->iframes = NULL;
    sequence->delay = NULL;
    sequence->walkmerge = NULL;
    sequence->frame_count = 0;

    free(sequence);
    return seq;
}

void
gamecache_sequence_free(struct GameCache_Sequence* sequence)
{
    if( !sequence )
        return;

    free(sequence->frames);
    free(sequence->iframes);
    free(sequence->delay);
    free(sequence->walkmerge);
    free(sequence);
}
