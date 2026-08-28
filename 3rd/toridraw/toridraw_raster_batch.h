#ifndef TORIDRAW_RASTER_BATCH_H
#define TORIDRAW_RASTER_BATCH_H

/*
 * Is the batched raster pipeline compiled in, and is it armed right now?
 *
 * Two consumers, in two translation-unit halves that cannot see each other's
 * statics: toridraw_raster.u.c, which runs the batched walk, and
 * toridraw_render.u.c's depth sort, which fills the staging the walk reads.
 * The sort is included first, so the predicate has to live above both.
 *
 * The sort asks because the stash is FOR the batcher and nobody else. With the
 * batcher disarmed those stores are work whose result is never loaded, and an
 * A/B whose baseline carries them is measuring the batched arm against
 * something slower than the code it replaced.
 */

#include "graphics/raster/gouraudhsllightness/gouraud_tri_asm.h"
#include "graphics/raster/flat/flat_tri_asm.h"

#if defined(TORIDRAW_GOURAUD_PRESORTED_RUN) && defined(TORIDRAW_FLAT_PRESORTED_RUN)
#define TORIDRAW_RASTER_BATCH 1
#endif

#ifdef TORIDRAW_RASTER_BATCH

#include <stdlib.h>

/* TORIDRAW_RASTER_BATCH=0 puts the per-face walk back, in the same binary,
 * which is how the two are A/B'd on the box. Read once; off is one predicted
 * branch, and the sort's hot loop hoists it out. */
static inline int
toridraw_raster_batch_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_RASTER_BATCH");
        armed = (v && v[0] == '0') ? 0 : 1;
    }
    return armed;
}

#else

#define toridraw_raster_batch_armed() 0

#endif

#endif
