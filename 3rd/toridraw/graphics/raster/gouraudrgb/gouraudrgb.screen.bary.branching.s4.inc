/*
 * Barycentric RGB-interpolated triangle template, `branching` walk.
 *
 * Included once per gate. The includer defines:
 *
 *   GRGB_FN        triangle entry point name
 *   GRGB_ORDERED   y-sorted helper name (the six-way dispatch tail calls it)
 *   GRGB_SPAN      span kernel name
 *   GRGB_SPAN_NC   span kernel name, horizontal clamps removed
 *   GRGB_ALPHA     1 = blend into the destination, 0 = overwrite it
 *
 * The walk is the `gouraudhsllightness` bary branching s4 walk, verbatim: same
 * six-way y permutation, same 16.16 edge accumulators, same once-per-trapezoid
 * horizontal-clip proof, same recolour every fourth pixel. Only the attribute
 * carried across the triangle differs - three independent 8.8 channel
 * accumulators instead of one packed HSL16 word, so nothing is resolved through
 * the palette and a gradient between two colours is the straight line between
 * them.
 *
 * Recolouring every fourth pixel rather than every pixel is inherited, not
 * incidental: it is what makes the span a run of four identical stores, and it
 * keeps the two families' banding at the same granularity so a face can switch
 * between them without the shading grain changing.
 */

/* ------------------------------------------------------------------ span */

#if GRGB_ALPHA
#define GRGB_STORE(off)                                                                            \
    do                                                                                             \
    {                                                                                              \
        pixel_buffer[(off)] = alpha_blend(alpha, pixel_buffer[(off)], rgb_color);                  \
    } while( 0 )
#else
#define GRGB_STORE(off)                                                                            \
    do                                                                                             \
    {                                                                                              \
        pixel_buffer[(off)] = rgb_color;                                                           \
    } while( 0 )
#endif

/**
 * One scanline, left/right clamped into [0, screen_width).
 */
static inline void
GRGB_SPAN(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int x_start_ish16,
    int x_end_ish16,
    int r_ish8,
    int g_ish8,
    int b_ish8,
    int step_r_ish8,
    int step_g_ish8,
    int step_b_ish8
#if GRGB_ALPHA
    ,
    int alpha
#endif
)
{
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
    r_ish8 += x_start * step_r_ish8;
    g_ish8 += x_start * step_g_ish8;
    b_ish8 += x_start * step_b_ish8;

    int stride = x_end - x_start;

    int steps = stride >> 2;
    step_r_ish8 <<= 2;
    step_g_ish8 <<= 2;
    step_b_ish8 <<= 2;

    while( steps-- > 0 )
    {
        int rgb_color = gouraudrgb_pack_ish8(r_ish8, g_ish8, b_ish8);

        GRGB_STORE(offset + 0);
        GRGB_STORE(offset + 1);
        GRGB_STORE(offset + 2);
        GRGB_STORE(offset + 3);
        offset += 4;

        r_ish8 += step_r_ish8;
        g_ish8 += step_g_ish8;
        b_ish8 += step_b_ish8;
    }

    int rgb_color = gouraudrgb_pack_ish8(r_ish8, g_ish8, b_ish8);
    switch( stride & 0x3 )
    {
    case 3:
        GRGB_STORE(offset);
        offset += 1;
        /* fallthrough */
    case 2:
        GRGB_STORE(offset);
        offset += 1;
        /* fallthrough */
    case 1:
        GRGB_STORE(offset);
    }
}

/**
 * Same span with the left/right clamps removed. The caller proves, once per
 * trapezoid, that both edges stay inside [0, screen_width) for every scanline
 * of the segment (flat_screen_fixed_edges_no_hclip), so x_start >= 0 and
 * x_end < screen_width hold by construction here.
 */
static inline void
GRGB_SPAN_NC(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int x_start_ish16,
    int x_end_ish16,
    int r_ish8,
    int g_ish8,
    int b_ish8,
    int step_r_ish8,
    int step_g_ish8,
    int step_b_ish8
#if GRGB_ALPHA
    ,
    int alpha
#endif
)
{
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_start = x_start_ish16 >> 16;
    int x_end = x_end_ish16 >> 16;

    if( x_start >= x_end )
        return;

    offset += x_start;
    r_ish8 += x_start * step_r_ish8;
    g_ish8 += x_start * step_g_ish8;
    b_ish8 += x_start * step_b_ish8;

    int stride = x_end - x_start;

    int steps = stride >> 2;
    step_r_ish8 <<= 2;
    step_g_ish8 <<= 2;
    step_b_ish8 <<= 2;

    while( steps-- > 0 )
    {
        int rgb_color = gouraudrgb_pack_ish8(r_ish8, g_ish8, b_ish8);

        GRGB_STORE(offset + 0);
        GRGB_STORE(offset + 1);
        GRGB_STORE(offset + 2);
        GRGB_STORE(offset + 3);
        offset += 4;

        r_ish8 += step_r_ish8;
        g_ish8 += step_g_ish8;
        b_ish8 += step_b_ish8;
    }

    int rgb_color = gouraudrgb_pack_ish8(r_ish8, g_ish8, b_ish8);
    switch( stride & 0x3 )
    {
    case 3:
        GRGB_STORE(offset);
        offset += 1;
        /* fallthrough */
    case 2:
        GRGB_STORE(offset);
        offset += 1;
        /* fallthrough */
    case 1:
        GRGB_STORE(offset);
    }
}

/* -------------------------------------------------------------- triangle */

/**
 * y0 <= y1 <= y2 already established by the caller.
 *
 *          /\      y0 (A)
 *         /  \
 *        /    \    y1 (B)
 *       /   /
 *      / /  y2 (C)
 */
static inline void
GRGB_ORDERED(
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
    int rgb0,
    int rgb1,
    int rgb2
#if GRGB_ALPHA
    ,
    int alpha
#endif
)
{
    if( y2 - y0 == 0 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

    int r0 = GOURAUDRGB_CHANNEL_R(rgb0);
    int g0 = GOURAUDRGB_CHANNEL_G(rgb0);
    int b0 = GOURAUDRGB_CHANNEL_B(rgb0);

    int d_r_AB = GOURAUDRGB_CHANNEL_R(rgb1) - r0;
    int d_g_AB = GOURAUDRGB_CHANNEL_G(rgb1) - g0;
    int d_b_AB = GOURAUDRGB_CHANNEL_B(rgb1) - b0;

    int d_r_AC = GOURAUDRGB_CHANNEL_R(rgb2) - r0;
    int d_g_AC = GOURAUDRGB_CHANNEL_G(rgb2) - g0;
    int d_b_AC = GOURAUDRGB_CHANNEL_B(rgb2) - b0;

    /* One plane per channel, all three sharing the triangle's signed area. */
    int step_x_r_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_r_AB * dy_AC - d_r_AC * dy_AB, sarea);
    int step_x_g_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_g_AB * dy_AC - d_g_AC * dy_AB, sarea);
    int step_x_b_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_b_AB * dy_AC - d_b_AC * dy_AB, sarea);

    int step_y_r_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_r_AC * dx_AB - d_r_AB * dx_AC, sarea);
    int step_y_g_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_g_AC * dx_AB - d_g_AB * dx_AC, sarea);
    int step_y_b_ish8 =
        gouraudrgb_barycentric_channel_step_ish8(d_b_AC * dx_AB - d_b_AB * dx_AC, sarea);

    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    /* Divides, not the reciprocal table - see the measurement note in the
     * gouraudhsllightness twin. */
    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    else
        step_edge_x_AB_ish16 = 0;

    if( y2 != y1 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
    else
        step_edge_x_BC_ish16 = 0;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    int r_ish8 = step_x_r_ish8 + (r0 << 8) - (x0 * step_x_r_ish8);
    int g_ish8 = step_x_g_ish8 + (g0 << 8) - (x0 * step_x_g_ish8);
    int b_ish8 = step_x_b_ish8 + (b0 << 8) - (x0 * step_x_b_ish8);

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        /* Only pre-step A->B if that edge is walked: rows [y0, y1) use it, so a
         * y1 at or above the viewport leaves the span empty. A near-horizontal
         * AB has a slope of hundreds of px per row, and times a distant y0 that
         * overflows the 16.16 product for a result that is then discarded. A
         * live edge cannot overflow (dy >= |y0| bounds it by dx << 16). Full
         * argument in graphics/raster/zbuffer/zbuf.screen.u.c. */
        if( y1 > 0 )
            edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        r_ish8 -= step_y_r_ish8 * y0;
        g_ish8 -= step_y_g_ish8 * y0;
        b_ish8 -= step_y_b_ish8 * y0;

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

#if GRGB_ALPHA
#define GRGB_SPAN_CALL(fn, left, right)                                                            \
    fn(pixel_buffer,                                                                               \
       offset,                                                                                     \
       screen_width,                                                                               \
       (left),                                                                                     \
       (right),                                                                                    \
       r_ish8,                                                                                     \
       g_ish8,                                                                                     \
       b_ish8,                                                                                     \
       step_x_r_ish8,                                                                              \
       step_x_g_ish8,                                                                              \
       step_x_b_ish8,                                                                              \
       alpha)
#define GRGB_SPAN_NC_CALL(fn, left, right)                                                         \
    fn(pixel_buffer,                                                                               \
       offset,                                                                                     \
       (left),                                                                                     \
       (right),                                                                                    \
       r_ish8,                                                                                     \
       g_ish8,                                                                                     \
       b_ish8,                                                                                     \
       step_x_r_ish8,                                                                              \
       step_x_g_ish8,                                                                              \
       step_x_b_ish8,                                                                              \
       alpha)
#else
#define GRGB_SPAN_CALL(fn, left, right)                                                            \
    fn(pixel_buffer,                                                                               \
       offset,                                                                                     \
       screen_width,                                                                               \
       (left),                                                                                     \
       (right),                                                                                    \
       r_ish8,                                                                                     \
       g_ish8,                                                                                     \
       b_ish8,                                                                                     \
       step_x_r_ish8,                                                                              \
       step_x_g_ish8,                                                                              \
       step_x_b_ish8)
#define GRGB_SPAN_NC_CALL(fn, left, right)                                                         \
    fn(pixel_buffer,                                                                               \
       offset,                                                                                     \
       (left),                                                                                     \
       (right),                                                                                    \
       r_ish8,                                                                                     \
       g_ish8,                                                                                     \
       b_ish8,                                                                                     \
       step_x_r_ish8,                                                                              \
       step_x_g_ish8,                                                                              \
       step_x_b_ish8)
#endif

#define GRGB_ROW(left, right, noclip)                                                              \
    do                                                                                             \
    {                                                                                              \
        if( noclip )                                                                               \
            GRGB_SPAN_NC_CALL(GRGB_SPAN_NC, (left), (right));                                      \
        else                                                                                       \
            GRGB_SPAN_CALL(GRGB_SPAN, (left), (right));                                            \
    } while( 0 )

#define GRGB_ADVANCE()                                                                             \
    do                                                                                             \
    {                                                                                              \
        r_ish8 += step_y_r_ish8;                                                                   \
        g_ish8 += step_y_g_ish8;                                                                   \
        b_ish8 += step_y_b_ish8;                                                                   \
        offset += stride;                                                                          \
    } while( 0 )

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
            GRGB_ROW(edge_x_AB_ish16, edge_x_AC_ish16, noclip_s1);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            GRGB_ADVANCE();
        }

        while( y2-- > 0 )
        {
            GRGB_ROW(edge_x_BC_ish16, edge_x_AC_ish16, noclip_s2);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            GRGB_ADVANCE();
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
            GRGB_ROW(edge_x_AC_ish16, edge_x_AB_ish16, noclip_s1);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;
            GRGB_ADVANCE();
        }

        while( y2-- > 0 )
        {
            GRGB_ROW(edge_x_AC_ish16, edge_x_BC_ish16, noclip_s2);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;
            GRGB_ADVANCE();
        }
    }

#undef GRGB_ROW
#undef GRGB_ADVANCE
#undef GRGB_SPAN_CALL
#undef GRGB_SPAN_NC_CALL
}

/**
 * Six-way y permutation dispatch, so the ordered walker never sorts.
 */
static inline void
GRGB_FN(
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
    int rgb0,
    int rgb1,
    int rgb2
#if GRGB_ALPHA
    ,
    int alpha
#endif
)
{
#if GRGB_ALPHA
#define GRGB_ORDERED_CALL(xa, xb, xc, ya, yb, yc, ca, cb, cc)                                      \
    GRGB_ORDERED(                                                                                  \
        pixel_buffer,                                                                              \
        stride,                                                                                    \
        screen_width,                                                                              \
        screen_height,                                                                             \
        (xa),                                                                                      \
        (xb),                                                                                      \
        (xc),                                                                                      \
        (ya),                                                                                      \
        (yb),                                                                                      \
        (yc),                                                                                      \
        (ca),                                                                                      \
        (cb),                                                                                      \
        (cc),                                                                                      \
        alpha)
#else
#define GRGB_ORDERED_CALL(xa, xb, xc, ya, yb, yc, ca, cb, cc)                                      \
    GRGB_ORDERED(                                                                                  \
        pixel_buffer,                                                                              \
        stride,                                                                                    \
        screen_width,                                                                              \
        screen_height,                                                                             \
        (xa),                                                                                      \
        (xb),                                                                                      \
        (xc),                                                                                      \
        (ya),                                                                                      \
        (yb),                                                                                      \
        (yc),                                                                                      \
        (ca),                                                                                      \
        (cb),                                                                                      \
        (cc))
#endif

    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 > screen_height )
            return;

        if( y1 <= y2 )
        {
            if( y2 < 0 )
                return;
            GRGB_ORDERED_CALL(x0, x1, x2, y0, y1, y2, rgb0, rgb1, rgb2);
        }
        else
        {
            if( y1 < 0 )
                return;
            GRGB_ORDERED_CALL(x0, x2, x1, y0, y2, y1, rgb0, rgb2, rgb1);
        }
    }
    else if( y1 <= y2 )
    {
        if( y1 > screen_height )
            return;

        if( y2 <= y0 )
        {
            if( y0 < 0 )
                return;
            GRGB_ORDERED_CALL(x1, x2, x0, y1, y2, y0, rgb1, rgb2, rgb0);
        }
        else
        {
            if( y2 < 0 )
                return;
            GRGB_ORDERED_CALL(x1, x0, x2, y1, y0, y2, rgb1, rgb0, rgb2);
        }
    }
    else
    {
        if( y2 > screen_height )
            return;

        if( y0 <= y1 )
        {
            if( y1 < 0 )
                return;
            GRGB_ORDERED_CALL(x2, x0, x1, y2, y0, y1, rgb2, rgb0, rgb1);
        }
        else
        {
            if( y0 < 0 )
                return;
            GRGB_ORDERED_CALL(x2, x1, x0, y2, y1, y0, rgb2, rgb1, rgb0);
        }
    }

#undef GRGB_ORDERED_CALL
}

#undef GRGB_STORE

#undef GRGB_FN
#undef GRGB_ORDERED
#undef GRGB_SPAN
#undef GRGB_SPAN_NC
#undef GRGB_ALPHA
