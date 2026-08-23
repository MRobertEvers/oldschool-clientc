#ifndef TORIDRAW_RASTER_ABLATE_H
#define TORIDRAW_RASTER_ABLATE_H

/**
 * Where does a rasterizer's time actually go?
 *
 * A profiler attributes the whole triangle function to one symbol, so
 * "gouraud fill is 13% of the frame" is a claim about a function that does
 * three separable things: a per-triangle prologue (five divides, the clip
 * arithmetic, the two no-hclip proofs), a per-scanline walk that steps the
 * edges and dispatches a span, and the span fill itself. Only the third is
 * something a vector kernel can help, and nothing so far has measured its
 * share -- the instruction-count argument for hand-SSE2 counted the whole
 * function.
 *
 * This cuts the function short at each boundary so the differences name the
 * pieces:
 *
 *   0  everything (the shipping path)
 *   1  no span fill -- prologue and scanline walk still run
 *   2  no scanline walk either -- per-triangle prologue only
 *
 * Level 0 minus level 1 is the fill. Level 1 minus level 2 is the walk and the
 * per-span prologue. Level 2 is the per-triangle cost. The frames drawn at
 * levels 1 and 2 are wrong, deliberately: this is a stopwatch, not a feature.
 *
 * The texture span gets its own switch rather than a rung on this ladder,
 * because the two fills are independent axes: the ladder can only say "both
 * off", and "both off" cannot be split into a gouraud share and a texture
 * share afterwards.
 *
 *   TORIDRAW_ABLATE_TEX=1   drop the perspective texture span's fill, keeping
 *                           its prologue -- the same cut TORIDRAW_ABLATE=1
 *                           makes in the gouraud span.
 *
 * That fill is itself three paths, and the census says they divide the pixels
 * very unevenly (73.7% / 23.6% / 2.6%). Pixel share is not time share when one
 * path spends a divide per pixel and another spends one per eight, so each gets
 * its own mode rather than a modelled multiplier:
 *
 *   TORIDRAW_ABLATE_TEX=2   drop only the `steps & 7` scalar tail
 *   TORIDRAW_ABLATE_TEX=3   drop only tex_span_exact_block, the per-pixel path
 *   TORIDRAW_ABLATE_TEX=4   drop only the eight-pixel SSE2 linear blocks
 *
 * Modes 3 and 4 reach the affine spans too, which share those helpers; the
 * perspective spans are the overwhelming majority of both.
 *
 * Compile-time gated -- build with -DTORIDRAW_ABLATE=1 and set
 * TORIDRAW_ABLATE=<level>. The shipping build contains none of it.
 */

#if defined(TORIDRAW_ABLATE) && TORIDRAW_ABLATE

#include <stdlib.h>

extern int g_toridraw_ablate;

/** Read once; the env lookup must not land in the loop being measured. */
static inline int
toridraw_ablate_level(void)
{
    if( g_toridraw_ablate < 0 )
    {
        char const* s = getenv("TORIDRAW_ABLATE");
        g_toridraw_ablate = s ? atoi(s) : 0;
    }
    return g_toridraw_ablate;
}

#define TORIDRAW_ABLATE_RETURN_AT(level)                                                  \
    do                                                                                    \
    {                                                                                     \
        if( toridraw_ablate_level() >= (level) )                                           \
            return;                                                                       \
    } while( 0 )

extern int g_toridraw_ablate_tex;

static inline int
toridraw_ablate_tex(void)
{
    if( g_toridraw_ablate_tex < 0 )
    {
        char const* s = getenv("TORIDRAW_ABLATE_TEX");
        g_toridraw_ablate_tex = s ? atoi(s) : 0;
    }
    return g_toridraw_ablate_tex;
}

#define TORIDRAW_ABLATE_TEX_RETURN()                                                      \
    do                                                                                    \
    {                                                                                     \
        if( toridraw_ablate_tex() == 1 )                                                   \
            return;                                                                       \
    } while( 0 )

/** Skip one of the three fill paths, so each can be subtracted on its own. */
#define TORIDRAW_ABLATE_TEX_RETURN_IF(mode)                                               \
    do                                                                                    \
    {                                                                                     \
        if( toridraw_ablate_tex() == (mode) )                                              \
            return;                                                                       \
    } while( 0 )

#else

#define TORIDRAW_ABLATE_RETURN_AT(level) ((void)0)
#define TORIDRAW_ABLATE_TEX_RETURN() ((void)0)
#define TORIDRAW_ABLATE_TEX_RETURN_IF(mode) ((void)0)

#endif

#endif
