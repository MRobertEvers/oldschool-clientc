#ifndef TORIDRAW_GOURAUD_SAREA_CENSUS_H
#define TORIDRAW_GOURAUD_SAREA_CENSUS_H

/**
 * How big is |sarea| in a real frame?
 *
 * This exists to size one table. The gouraud prologue issues five divides per
 * triangle: three edge slopes, which compute a SCREEN X and must stay exact,
 * and two barycentric colour steps, which compute a SHADE and do not. Only the
 * colour pair is a candidate for a reciprocal-multiply, and both of them divide
 * by the SAME value -- sarea -- so one table lookup serves two multiplies.
 *
 * A direct-indexed reciprocal table needs a size, and a size needs a
 * distribution, not a guess. The last reciprocal table in this file's history
 * was thrown away partly because 16 KB indexed effectively at random missed
 * more than the divide cost; picking N without knowing where the mass sits
 * would be repeating that. So: a log2 histogram of |sarea| over real frames.
 * Read the cumulative column -- it is the coverage a table of that size buys,
 * and everything above it falls back to idivl.
 *
 * Compile-time gated (-DTORIDRAW_SAREA_CENSUS=1); a shipping build has no
 * branch here. Counts triangles that reach the prologue, which is after the
 * sarea == 0 reject, so degenerate faces are excluded -- they never divide.
 */

#if defined(TORIDRAW_SAREA_CENSUS) && TORIDRAW_SAREA_CENSUS

#include <stdio.h>
#include <stdlib.h>

struct ToriDraw_SareaCensus
{
    double buckets[33]; /**< buckets[k] = count with 2^(k-1) <= |sarea| < 2^k */
    double total;
    int registered;
};

extern struct ToriDraw_SareaCensus g_toridraw_sarea_census;

void
ToriDraw_SareaCensusDump(void);

static inline void
toridraw_sarea_census_record(int sarea)
{
    /* Negated as unsigned: sarea == INT_MIN has no positive counterpart and
     * -sarea would be undefined. The magnitude is what is being bucketed and
     * the unsigned form of it is exact. */
    unsigned mag = (sarea < 0) ? (unsigned)(-(unsigned)sarea) : (unsigned)sarea;
    int k = 0;

    if( !g_toridraw_sarea_census.registered )
    {
        g_toridraw_sarea_census.registered = 1;
        atexit(ToriDraw_SareaCensusDump);
    }

    while( mag )
    {
        mag >>= 1;
        k++;
    }
    g_toridraw_sarea_census.buckets[k] += 1.0;
    g_toridraw_sarea_census.total += 1.0;
}

#define TORIDRAW_SAREA_CENSUS_RECORD(sarea) toridraw_sarea_census_record((sarea))

#else

#define TORIDRAW_SAREA_CENSUS_RECORD(sarea) ((void)0)

#endif

#endif
