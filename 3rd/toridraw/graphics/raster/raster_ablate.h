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
 *   3  the gouraud triangle returns at its first statement -- nothing at all
 *
 * Level 0 minus level 1 is the fill. Level 1 minus level 2 is the walk and the
 * per-span prologue. Level 2 minus level 3 is the per-triangle prologue. The
 * frames drawn at levels 1..3 are wrong, deliberately: this is a stopwatch, not
 * a feature.
 *
 * Level 3 was added after the face census (2026-08-24) showed gouraud is 82.5%
 * of drawn faces and 71% of drawn area while carrying no vector kernel at all.
 * Without it the ladder can say what the fill and the walk cost but not what
 * the prologue costs, and "level 2 is the per-triangle cost" was never true as
 * written -- level 2 is an absolute wall time, not a difference, so it also
 * contains every other phase of the frame. Only 2 minus 3 isolates it.
 *
 * Mind exactly where the level-2 cut sits: it is after the y clamps but BEFORE
 * the two flat_screen_fixed_edges_no_hclip proofs. Those are per-triangle work
 * that lands in the level 1 minus level 2 bucket along with the per-row walk.
 * At 2.95 spans per gouraud face that bucket is not "the row loop" -- it is two
 * proofs plus about three rows, and the two are the same order of magnitude.
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
 * None of 1..4 touches the texture triangle's own scanline walk, so what mode 1
 * leaves behind is still "walk + per-triangle prologue" mixed together -- the
 * texture axis had no rung matching TORIDRAW_ABLATE=2. Mode 5 is that rung:
 *
 *   TORIDRAW_ABLATE_TEX=5   cut the texture triangle at the same boundary
 *                           TORIDRAW_ABLATE=2 uses in the gouraud triangle --
 *                           after the y clamps, before the trapezoid walk.
 *                           Per-triangle prologue only.
 *
 * Mode 5 subsumes mode 1's cut: returning before the walk means no span runs.
 * So mode 1 minus mode 5 is the texture scanline walk plus its per-span
 * prologue, and mode 5 is the per-triangle cost. The fill macro fires at 5 too,
 * so a walker with no mode-5 cut loses its fill rather than quietly counting as
 * walk time.
 *
 * The XP ladder then showed the fill costs 6.615 ms/frame while modes 2, 3 and
 * 4 together account for only 2.582 of it. The missing 4 ms is in no leaf, and
 * it is not the per-span prologue either -- mode 1 cuts after that. It is the
 * scaffolding the block loop wraps around the leaves: one `1.0f/(float)w` for
 * the next block, two uv quotients, and the lerp8 fit test, once per eight
 * pixels. That number was a subtraction of three separate runs on a box whose
 * two modes are 4 ms apart, which is too shaky to rewrite divides against, so
 * it gets its own rung:
 *
 *   TORIDRAW_ABLATE_TEX=6   run the whole block loop -- every divide, quotient
 *                           and fit test -- but take none of the three leaves,
 *                           so no pixel is written.
 *
 * base minus mode 6 is then all the pixel work, and the fill (base minus mode
 * 1) minus that is the scaffolding, from one pair of runs instead of four.
 * Mind that mode 6 also removes the texel loads, so the scaffolding it isolates
 * is arithmetic with the texture cache misses charged to the leaves. That is
 * the right split for deciding whether to attack the divides, which is what the
 * rung is for.
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

#define TORIDRAW_ABLATE_TEX_RETURN()                                                  \
    do                                                                                \
    {                                                                                 \
        int const _tex_ablate = toridraw_ablate_tex();                                \
        if( _tex_ablate == 1 || _tex_ablate == 5 )                                    \
            return;                                                                   \
    } while( 0 )

/** The texture triangle's level-2 cut: prologue kept, trapezoid walk dropped. */
#define TORIDRAW_ABLATE_TEX_WALK_RETURN()                                             \
    do                                                                                \
    {                                                                                 \
        if( toridraw_ablate_tex() == 5 )                                              \
            return;                                                                   \
    } while( 0 )

/** Skip one of the three fill paths, so each can be subtracted on its own.
 *  Mode 6 fires for all three at once, leaving the block loop's arithmetic
 *  running with nothing drawn. */
#define TORIDRAW_ABLATE_TEX_RETURN_IF(mode)                                               \
    do                                                                                    \
    {                                                                                     \
        int const _leaf_ablate = toridraw_ablate_tex();                                   \
        if( _leaf_ablate == (mode) || _leaf_ablate == 6 )                                  \
            return;                                                                       \
    } while( 0 )

#else

#define TORIDRAW_ABLATE_RETURN_AT(level) ((void)0)
#define TORIDRAW_ABLATE_TEX_RETURN() ((void)0)
#define TORIDRAW_ABLATE_TEX_WALK_RETURN() ((void)0)
#define TORIDRAW_ABLATE_TEX_RETURN_IF(mode) ((void)0)

#endif

#endif
