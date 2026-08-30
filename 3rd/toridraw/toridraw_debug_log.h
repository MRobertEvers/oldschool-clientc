#ifndef TORIDRAW_DEBUG_LOG_H
#define TORIDRAW_DEBUG_LOG_H

/**
 * NDJSON anomaly log for the render and raster paths.
 *
 * One record per anomaly burst, appended as newline-delimited JSON, for
 * questions that a printf cannot answer because the interesting models are a
 * handful out of a hundred thousand: which kernel disagreed with exact
 * arithmetic, which model lost faces to the sort, which face order came out
 * of the bucket pass. The emitters that build the records live next to the
 * code they measure -- toridraw_debug_render.u.c, toridraw_debug_raster.u.c --
 * so nothing but a macro call remains at the site.
 *
 * COMPILE-TIME GATED, like the censuses next door (graphics/proj_census.h,
 * graphics/raster/span_census.h) and for the same reason: these sites sit
 * per-model and per-face on the render path, and a runtime branch there taxes
 * the thing being measured. With the gate off every macro below is a
 * constant, the emitters are not compiled, and no branch survives.
 *
 *   make -C src TORIDRAW_DEBUG_NDJSON=1     build with the log compiled in
 *
 * Then, at run time, in a build that has it:
 *
 *   TORIDRAW_DEBUG_NDJSON=1     turn the log on (off by default even here)
 *   TORIDRAW_DEBUG_LOG=<path>   where to append; default toridraw-debug.ndjson
 *                               in the working directory
 *   TORIDRAW_DEBUG_RUN=<label>  goes in every record as runId, to tell two
 *                               runs apart in one file
 *   TORIDRAW_SAFE_NEAR=0        unrelated to the log, and the reason this
 *                               header carries it: restores the camera's own
 *                               near plane, so one session can A/B whether
 *                               the raised plane is what removes near
 *                               geometry. Compiled out with the log.
 */

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** Records dropped after this many, so a wedged frame cannot fill a disk. */
#define TORIDRAW_DBG_BUDGET 6000

static inline int
toridraw_dbg_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_DEBUG_NDJSON");
        on = (v && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return on;
}

/** Relative by default: an absolute path baked in here fails to open on any
 *  host that is not the one it was written on, and every record is then
 *  dropped silently -- which reads exactly like "the instrumentation says
 *  nothing is wrong". */
static inline const char*
toridraw_dbg_log_path(void)
{
    static const char* path = NULL;
    if( !path )
    {
        const char* v = getenv("TORIDRAW_DEBUG_LOG");
        path = (v && v[0] != '\0') ? v : "toridraw-debug.ndjson";
    }
    return path;
}

static inline const char*
toridraw_dbg_run_id(void)
{
    static const char* run = NULL;
    if( !run )
    {
        const char* v = getenv("TORIDRAW_DEBUG_RUN");
        run = (v && v[0] != '\0') ? v : "run1";
    }
    return run;
}

/** Emit at most one record per anomaly burst: `state` counts calls at this
 *  site, the first few always pass, then one in `period`. */
static inline bool
toridraw_dbg_gate(int* state, int period)
{
    int const n = (*state)++;
    return n < 4 || (period > 0 && (n % period) == 0);
}

static inline void
toridraw_dbg_log(
    const char* hypothesis,
    const char* location,
    const char* message,
    const char* data_json)
{
    static int budget = TORIDRAW_DBG_BUDGET;
    FILE* f;

    if( !toridraw_dbg_enabled() || budget <= 0 )
        return;
    budget--;

    f = fopen(toridraw_dbg_log_path(), "a");
    if( !f )
        return;
    fprintf(
        f,
        "{\"runId\":\"%s\",\"hypothesisId\":\"%s\",\"location\":\"%s\","
        "\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
        toridraw_dbg_run_id(),
        hypothesis,
        location,
        message,
        data_json,
        (long long)time(NULL) * 1000);
    fclose(f);
}

/** TORIDRAW_SAFE_NEAR=0 restores the camera's own near plane. */
static inline int
toridraw_dbg_safe_near_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_SAFE_NEAR");
        on = (v && v[0] == '0') ? 0 : 1;
    }
    return on;
}

#define TORIDRAW_DBG_ENABLED()            toridraw_dbg_enabled()
#define TORIDRAW_DBG_GATE(state, period)  toridraw_dbg_gate((state), (period))
#define TORIDRAW_DBG_LOG(hyp, loc, msg, data) \
    toridraw_dbg_log((hyp), (loc), (msg), (data))
#define TORIDRAW_DBG_SAFE_NEAR_ENABLED()  toridraw_dbg_safe_near_enabled()

/* A plain counter incremented from a hot path, and nothing else. Declared
 * through a macro so it disappears with the log rather than costing an
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

/* Constants, so every guarded block folds away and the emitters below are
 * never compiled. TORIDRAW_DBG_SAFE_NEAR_ENABLED is 1 rather than 0: the
 * raised near plane is the SHIPPING behaviour, and the env knob only ever
 * took it away. */
#define TORIDRAW_DBG_ENABLED()                0
#define TORIDRAW_DBG_GATE(state, period)      0
#define TORIDRAW_DBG_LOG(hyp, loc, msg, data) ((void)0)
#define TORIDRAW_DBG_SAFE_NEAR_ENABLED()      1

/* Declares nothing that is emitted, and eats the trailing semicolon. */
#define TORIDRAW_DBG_COUNTER(name) extern int toridraw_dbg_absent_##name
#define TORIDRAW_DBG_COUNT(name)   ((void)0)
#define TORIDRAW_DBG_MAX(name, value)  ((void)0)

#endif /* TORIDRAW_DEBUG_NDJSON */

#endif /* TORIDRAW_DEBUG_LOG_H */
