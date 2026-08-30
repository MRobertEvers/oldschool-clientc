#ifndef TORIDRAW_DEBUG_H
#define TORIDRAW_DEBUG_H

/**
 * The compile-time gate for everything that exists only to describe a render.
 *
 * A header, and this small, for one reason: the facility itself lives in one
 * file (toridraw_debug.u.c, included from toridraw_render.u.c), but two files
 * that are NOT debug code -- triangles/toridraw_triangle_clip.u.c and
 * graphics/projection.u.c -- keep a plain counter each, and they are compiled
 * long before the render path exists. They need the gate and the counter
 * macros; they do not need, and must not pull in, the stats structs, the
 * printers or the NDJSON emitters. Those are all next door.
 *
 * GATED AT COMPILE TIME, like the censuses (graphics/proj_census.h,
 * graphics/raster/span_census.h) and for the same reason: the sites sit
 * per-model and per-face on the render path, and a runtime branch there taxes
 * the thing being measured. With the gate off nothing below is emitted, the
 * facility is not compiled, and no branch survives.
 *
 *   make -C src TORIDRAW_DEBUG_STATS=1   the counters, printers and harnesses
 *   make -C src TORIDRAW_DEBUG_NDJSON=1  the above plus the NDJSON anomaly log
 *
 * TORIDRAW_DEBUG_NDJSON and TORIDRAW_NEAR_CLIP_STATS each turn the facility on
 * by themselves -- they are features of it, not alternatives to it -- so a
 * build asking for either gets the whole thing.
 *
 * See toridraw_debug.u.c for what the facility is and for the run-time
 * switches (TORIDRAW_SORT_DEBUG, TORIDRAW_RASTER_DEBUG, TORIDRAW_DEBUG_NDJSON,
 * TORIDRAW_SPAN_RATIO, TORIDRAW_SAFE_NEAR) that select within it.
 */

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON
#undef TORIDRAW_DEBUG_STATS
#define TORIDRAW_DEBUG_STATS 1
#endif

#if defined(TORIDRAW_NEAR_CLIP_STATS)
#undef TORIDRAW_DEBUG_STATS
#define TORIDRAW_DEBUG_STATS 1
#endif

#if !defined(TORIDRAW_DEBUG_STATS)
#define TORIDRAW_DEBUG_STATS 0
#endif

#if TORIDRAW_DEBUG_STATS

/* A plain counter incremented from a hot path, and nothing else. Declared
 * through a macro so it disappears with the facility rather than costing an
 * increment in a build that can never read it. */
#define TORIDRAW_DBG_COUNTER(name) static int name = 0
#define TORIDRAW_DBG_COUNT(name)   ((name)++)
#define TORIDRAW_DBG_MAX(name, value) \
    do \
    { \
        if( (value) > (name) ) \
            (name) = (value); \
    } while( 0 )

#else

/* Declares nothing that is emitted, and eats the trailing semicolon. */
#define TORIDRAW_DBG_COUNTER(name)    extern int toridraw_dbg_absent_##name
#define TORIDRAW_DBG_COUNT(name)      ((void)0)
#define TORIDRAW_DBG_MAX(name, value) ((void)0)

#endif /* TORIDRAW_DEBUG_STATS */

#endif /* TORIDRAW_DEBUG_H */
