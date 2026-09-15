#ifndef GOURAUDHSLLIGHTNESS_SCREEN_OPAQUE_BARY_BRANCHING_S4_C
#define GOURAUDHSLLIGHTNESS_SCREEN_OPAQUE_BARY_BRANCHING_S4_C

#include "graphics/int_wrap.h"
#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness_barycentric_steps.h"
#include "graphics/raster/gouraudhsllightness/gouraud_span_fill.h"
#include "graphics/raster/flat/flat_screen_edges.h"
#include "census/span_census.h"
#include "census/raster_ablate.h"
#include "census/sarea_census.h"

#include "graphics/shared_tables.h"

/**
 * Tested on Mac M4.
 *
 * This is slower than the "sorting" version on P-Cores.
 * For E-Cores, this is faster. E.g. Running in lower power mode.
 */
static inline void
draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    TORI_UNUSED(y);
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_end = x_end_ish16 >> 16;

    if( x_end >= screen_width )
        x_end = screen_width - 1;

    int x_start = x_start_ish16 >> 16;
    if( x_start < 0 )
        x_start = 0;

    if( x_start >= x_end )
        return;

    offset += x_start;
    /* Modular: the reference client relies on int wraparound here, and an edge-on
     * triangle reaches the overflow. See graphics/int_wrap.h. */
    color_hsl16_ish8 = toridraw_wrap_add(
        color_hsl16_ish8, toridraw_wrap_mul(x_start, color_step_hsl16_ish8));

    int stride = (x_end - x_start);
    TORIDRAW_SPAN_CENSUS_RECORD(
        stride, offset, 1, color_hsl16_ish8, color_step_hsl16_ish8);

    TORIDRAW_ABLATE_RETURN_AT(1);

    toridraw_gouraud_span_fill_short(
        pixel_buffer, offset, stride, color_hsl16_ish8, color_step_hsl16_ish8);
}

/**
 * Same span fill with the left/right clamps removed. The caller proves, once
 * per trapezoid, that both edges stay inside [0, screen_width) for every
 * scanline of the segment (flat_screen_fixed_edges_no_hclip), so x_start >= 0
 * and x_end < screen_width hold by construction here.
 */
static inline void
draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_start = x_start_ish16 >> 16;
    int x_end = x_end_ish16 >> 16;

    if( x_start >= x_end )
        return;

    offset += x_start;
    color_hsl16_ish8 = toridraw_wrap_add(
        color_hsl16_ish8, toridraw_wrap_mul(x_start, color_step_hsl16_ish8));

    int stride = (x_end - x_start);
    TORIDRAW_SPAN_CENSUS_RECORD(
        stride, offset, 0, color_hsl16_ish8, color_step_hsl16_ish8);

    TORIDRAW_ABLATE_RETURN_AT(1);

    toridraw_gouraud_span_fill_short(
        pixel_buffer, offset, stride, color_hsl16_ish8, color_step_hsl16_ish8);
}

static inline void
raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    /* Level 3 is the rung the original ladder lacked. Level 2 returns *after*
     * the prologue, so base-minus-2 is fill+walk and the per-triangle prologue
     * -- five divides, the barycentric steps, the y clamps, the two no-hclip
     * proofs -- never appears in any difference. With 11,570 gouraud triangles
     * a frame and only 2.95 spans each, that prologue is the term most likely
     * to dominate, and it was the one term nobody had measured. */
    TORIDRAW_ABLATE_RETURN_AT(3);

    if( y2 - y0 == 0 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

    TORIDRAW_SAREA_CENSUS_RECORD(sarea);

    int d_hsl_AB = color1_hsl16 - color0_hsl16;
    int d_hsl_AC = color2_hsl16 - color0_hsl16;

    /**
     * This is derived from a barycentric coordinate.
     */
    gouraudhsllightness_recip_t recip_sarea = gouraudhsllightness_barycentric_recip(sarea);
    int step_x_hsl_ish8 =
        gouraudhsllightness_barycentric_hsl_step_ish8(d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, recip_sarea);
    int step_y_hsl_ish8 =
        gouraudhsllightness_barycentric_hsl_step_ish8(d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, recip_sarea);

    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    /**
     * Attention! The commented-out reciprocal-table form below was measured on
     * an M4 (2026-07-28): it is both SLOWER than the divide and not
     * bit-identical to it. g_reciprocal16 is 16 KB and the dy index is
     * effectively random per triangle, so the load misses more than the divide
     * costs; the truncated reciprocal also shifted ~3% of drawn pixels. Keep
     * the divides.
     *
     * Second measurement, win32/pentium4 lane, 2026-08-23. The whole question
     * of whether these divides are worth removing is settled, and the answer is
     * no. This prologue issues five divides per triangle -- three edge slopes
     * and two barycentric colour steps -- which the osrs239 steady state runs
     * 9,605 times a frame, so about 48,000 divides per frame. Adding a second,
     * identical set of five (same divisors, perturbed numerators, volatile sink,
     * output bit-identical) cost 29.8 us on r_model and 37.3 us on frame, of
     * 3,338 us and 5,958 us. So the entire divide population is ~0.5% of the
     * frame -- roughly 2.7 cycles apiece, not the ~26 an idiv latency table
     * suggests, because they are independent and the core overlaps them.
     *
     * Corollaries, so nobody re-derives them:
     *   - A dy == 1 fast path is exact and looks tempting (33.1% of edge
     *     divides have dy == 1, censused over 1000 frames). It is worth about
     *     7 us. It was built, measured, and thrown away.
     *   - An exact reciprocal table for dy <= 64 would reach ~89% of edge
     *     divides and cannot beat ~0.3% of frame. Not worth the exactness
     *     proof.
     *   - GCC deletes a `if (dy == 1) return dx << 16;` guard anyway: it proves
     *     the arm equals the divide and cross-jumps it away. It survives only
     *     behind an asm barrier. The value-identity that makes such a guard
     *     safe is the same property that makes the compiler discard it.
     */
    if( dy_AC > 0 )
    {
        // step_edge_x_AC_ish16 = (dx_AC)*g_reciprocal16[dy_AC];
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    }
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
    {
        // step_edge_x_AB_ish16 = (dx_AB)*g_reciprocal16[dy_AB];
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    }
    else
        step_edge_x_AB_ish16 = 0;

    if( y2 != y1 )
    {
        // step_edge_x_BC_ish16 = ((x2 - x1)) * g_reciprocal16[y2 - y1];
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
    }
    else
        step_edge_x_BC_ish16 = 0;

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    int hsl_ish8 = toridraw_wrap_sub(
        toridraw_wrap_add(step_x_hsl_ish8, color0_hsl16 << 8),
        toridraw_wrap_mul(x0, step_x_hsl_ish8));

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /* Only pre-step A->B if that edge is walked: rows [y0, y1) use it, so
         * a y1 at or above the viewport leaves the span empty. A near-horizontal
         * AB has a slope of hundreds of px per row, and times a distant y0 that
         * overflows the 16.16 product for a result that is then discarded. A live
         * edge cannot overflow (dy >= |y0| bounds it by dx << 16). Full argument
         * in graphics/raster/zbuffer/zbuf.screen.u.c. */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        hsl_ish8 = toridraw_wrap_sub(hsl_ish8, toridraw_wrap_mul(step_y_hsl_ish8, y0));

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    int offset = y0 * stride;

    if( y1 > screen_height )
    {
        y1 = screen_height;
        y2 = screen_height;
    }
    else if( y2 > screen_height )
    {
        y2 = screen_height;
    }

    TORIDRAW_ABLATE_RETURN_AT(2);

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        /* Prove the horizontal clamps redundant once per trapezoid instead of
         * re-testing them on every scanline. Mirrors the flat rasterizer. */
        int seg1_count = y1 - y0;
        int seg2_count = y2 - y1;
        if( seg1_count < 0 )
            seg1_count = 0;
        if( seg2_count < 0 )
            seg2_count = 0;
        int noclip_s1 = flat_screen_fixed_edges_no_hclip(
            edge_x_AB_ish16,
            step_edge_x_AB_ish16,
            edge_x_AC_ish16,
            step_edge_x_AC_ish16,
            seg1_count,
            screen_width);
        /* BC is not advanced during the first trapezoid; AC is. */
        int noclip_s2 = flat_screen_fixed_edges_no_hclip(
            edge_x_BC_ish16,
            step_edge_x_BC_ish16,
            edge_x_AC_ish16 + seg1_count * step_edge_x_AC_ish16,
            step_edge_x_AC_ish16,
            seg2_count,
            screen_width);

        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            if( noclip_s1 )
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer,
                    offset,
                    edge_x_AB_ish16,
                    edge_x_AC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }
            else
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer,
                    offset,
                    screen_width,
                    0,
                    edge_x_AB_ish16,
                    edge_x_AC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);

            offset += stride;
        }

        while( y2-- > 0 )
        {
            if( noclip_s2 )
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer,
                    offset,
                    edge_x_BC_ish16,
                    edge_x_AC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }
            else
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer,
                    offset,
                    screen_width,
                    0,
                    edge_x_BC_ish16,
                    edge_x_AC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);
            offset += stride;
        }
    }
    else
    {
        int seg1_count = y1 - y0;
        int seg2_count = y2 - y1;
        if( seg1_count < 0 )
            seg1_count = 0;
        if( seg2_count < 0 )
            seg2_count = 0;
        int noclip_s1 = flat_screen_fixed_edges_no_hclip(
            edge_x_AC_ish16,
            step_edge_x_AC_ish16,
            edge_x_AB_ish16,
            step_edge_x_AB_ish16,
            seg1_count,
            screen_width);
        int noclip_s2 = flat_screen_fixed_edges_no_hclip(
            edge_x_AC_ish16 + seg1_count * step_edge_x_AC_ish16,
            step_edge_x_AC_ish16,
            edge_x_BC_ish16,
            step_edge_x_BC_ish16,
            seg2_count,
            screen_width);

        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            if( noclip_s1 )
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer,
                    offset,
                    edge_x_AC_ish16,
                    edge_x_AB_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }
            else
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer,
                    offset,
                    screen_width,
                    0,
                    edge_x_AC_ish16,
                    edge_x_AB_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);
            offset += stride;
        }

        while( y2-- > 0 )
        {
            if( noclip_s2 )
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered_noclip(
                    pixel_buffer,
                    offset,
                    edge_x_AC_ish16,
                    edge_x_BC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }
            else
            {
                draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                    pixel_buffer,
                    offset,
                    screen_width,
                    0,
                    edge_x_AC_ish16,
                    edge_x_BC_ish16,
                    hsl_ish8,
                    step_x_hsl_ish8);
            }

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 = toridraw_wrap_add(hsl_ish8, step_y_hsl_ish8);
            offset += stride;
        }
    }
}
static inline void
raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    // either.
    // y0, y1, y2,
    // y0, y2, y1,
    // y1, y0, y2,
    // y1, y2, y0,
    // y2, y0, y1,
    // y2, y1, y0,
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 > screen_height )
            return;

        // y0, y1, y2,
        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x0,
                x1,
                x2,
                y0,
                y1,
                y2,
                color0_hsl16,
                color1_hsl16,
                color2_hsl16);
        }
        // y0, y2, y1,
        else
        {
            if( y1 < 0 || y0 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x0,
                x2,
                x1,
                y0,
                y2,
                y1,
                color0_hsl16,
                color2_hsl16,
                color1_hsl16);
        }
    }
    else if( y1 <= y2 )
    {
        if( y1 > screen_height )
            return;

        // y1, y2, y0
        if( y2 <= y0 )
        {
            if( y0 < 0 || y1 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x1,
                x2,
                x0,
                y1,
                y2,
                y0,
                color1_hsl16,
                color2_hsl16,
                color0_hsl16);
        }
        // y1, y0, y2,
        else
        {
            if( y2 < 0 || y1 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x1,
                x0,
                x2,
                y1,
                y0,
                y2,
                color1_hsl16,
                color0_hsl16,
                color2_hsl16);
        }
    }
    else
    {
        if( y2 > screen_height )
            return;

        // y2, y0, y1,
        if( y0 <= y1 )
        {
            if( y1 < 0 || y2 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x2,
                x0,
                x1,
                y2,
                y0,
                y1,
                color2_hsl16,
                color0_hsl16,
                color1_hsl16);
        }
        // y2, y1, y0,
        else
        {
            if( y0 < 0 || y2 > screen_height )
                return;

            raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x2,
                x1,
                x0,
                y2,
                y1,
                y0,
                color2_hsl16,
                color1_hsl16,
                color0_hsl16);
        }
    }
}

#endif
