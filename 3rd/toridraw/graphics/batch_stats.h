#ifndef TORIDRAW_GRAPHICS_BATCH_STATS_H
#define TORIDRAW_GRAPHICS_BATCH_STATS_H

/**
 * The compile-time gate for the batched raster walk's census, and the one
 * counter that is raised from outside it.
 *
 * Four numbers say how much of a frame reaches a presorted kernel: how many
 * models stashed a y ordering for one -- raised by both face sorts, which is
 * why that counter is here and not next to the other three -- and, per face,
 * how many were staged, how many fell through and drew, and how many fell
 * through and were culled. graphics/raster/batch/raster.batch.u.c owns those
 * three and prints all four at exit.
 *
 * GATED AT COMPILE TIME, like the render/raster facility (toridraw_debug.h) and
 * the painter's (painters/debug/painters_debug.h), and for the same reason:
 * three of the four sites sit inside the per-face loop of the very walk they
 * measure. It used to be a getenv cached in a static, which is a knob only for
 * the printing -- the increments, and a load-and-branch on that static to reach
 * two of them, stayed in every shipping build to feed a report nobody had asked
 * for. With the gate off no counter is emitted and no site survives.
 *
 *   make -C src TORIDRAW_BATCH_STATS=1 ...
 *
 * There is no run-time switch within it, unlike the two facilities above:
 * there is one report and it is the whole facility, so a build that has it
 * prints at exit.
 *
 * The counters stay file-static -- one set per translation unit, as they were
 * when only toridraw_render.u.c declared the presort one and the other two
 * fragments compiled on the accident of being pasted in below that file.
 */

#if !defined(TORIDRAW_BATCH_STATS)
#define TORIDRAW_BATCH_STATS 0
#endif

#if TORIDRAW_BATCH_STATS

#define TORIDRAW_BATCH_COUNTER(name) static long name
#define TORIDRAW_BATCH_COUNT(name) ((name)++)

#else

/* Declares nothing that is emitted, and eats the trailing semicolon. */
#define TORIDRAW_BATCH_COUNTER(name) extern int toridraw_batch_absent_##name
#define TORIDRAW_BATCH_COUNT(name) ((void)0)

#endif /* TORIDRAW_BATCH_STATS */

/* Models whose sort stashed the y ordering for the batched raster walk. A GPU
 * lane must show zero here: it sorts for the GPU and never reads the stash, so
 * a non-zero count is exactly the regression the stash/no-stash split exists to
 * prevent. */
TORIDRAW_BATCH_COUNTER(g_toridraw_presort_models);

#endif /* TORIDRAW_GRAPHICS_BATCH_STATS_H */
