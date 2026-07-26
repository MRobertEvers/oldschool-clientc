/* proctex_raster.u.c — included by proctex_ops.u.c
 *
 * The `rasterizer` operation (type 29): a small vector display list — lines, cubic beziers,
 * rectangles and ellipses, each with a fill and an outline — rendered into the operation's
 * own image. Ported from rs-map-viewer's RasterizerOperation.ts, whose `Rasterizer` class is
 * itself a transcription of the client's; the fixed-point arithmetic and the midpoint
 * ellipse/circle stepping are kept verbatim rather than rewritten, because their rounding is
 * what the authored shapes were tuned against.
 *
 * Two things about the reference are semantics, not accidents, and are reproduced:
 *
 *   1. **Colours here are packed 24-bit RGB**, not the 12.4 triples every other operation
 *      deals in. A monochrome rasterizer writes those packed values straight into its
 *      monochrome plane, which is meaningless as a signal but is what the reference does and
 *      what downstream operations were authored against. The colour path unpacks them.
 *
 *   2. **Out-of-range writes are dropped, not clipped.** The reference draws into JavaScript
 *      typed arrays, where a store past either end is silently discarded, and it relies on
 *      that: the line clipper re-solves x when it clamps y and never re-clamps the result. In
 *      C the same store is memory corruption, so every raw write is range-checked. The one
 *      divergence is a *row* index out of range, which would throw in the reference (indexing
 *      the outer plain array yields undefined) and fails the whole texture; here the row is
 *      skipped and the rest of the shape still draws.
 *
 * Divergence worth naming: the reference's line clipper leaves the re-solved endpoint as a
 * JS float and lets the pixel loop truncate it. These use integer division, which truncates
 * toward zero at the point of computation instead. The two differ by at most one pixel, and
 * only on a line that leaves the image.
 */

struct proctex_raster
{
    int32_t* pixels;
    int width;
    int height;
    int width_mask;
    int height_mask;
    int start_x;
    int start_y;
    /* Scratch for the circle rasteriser's per-row inner radius. Grown, never shrunk. */
    int32_t* circle_outline;
    int circle_outline_size;
};

/** Row `y`, or NULL when it is outside the image — see note (2) above. */
static int32_t*
raster_row(struct proctex_raster* rs, int y)
{
    if( y < 0 || y >= rs->height )
        return NULL;
    return rs->pixels + (size_t)y * (size_t)rs->width;
}

static void
raster_put(struct proctex_raster* rs, int x, int y, int32_t colour)
{
    int32_t* row = raster_row(rs, y);
    if( !row || x < 0 || x >= rs->width )
        return;
    row[x] = colour;
}

static void
raster_fill_row(struct proctex_raster* rs, int y, int lo, int hi, int32_t colour)
{
    int32_t* row = raster_row(rs, y);
    if( row )
        proctex_fill_range(row, lo, hi, rs->width, colour);
}

static bool
raster_init_circle_outline(struct proctex_raster* rs, int size)
{
    if( size <= rs->circle_outline_size )
        return true;
    {
        int32_t* grown = realloc(rs->circle_outline, (size_t)size * sizeof(int32_t));
        if( !grown )
            return false;
        memset(
            grown + rs->circle_outline_size,
            0,
            (size_t)(size - rs->circle_outline_size) * sizeof(int32_t));
        rs->circle_outline = grown;
        rs->circle_outline_size = size;
    }
    return true;
}

static int32_t
raster_outline_at(struct proctex_raster* rs, int index)
{
    if( index < 0 || index >= rs->circle_outline_size )
        return 0;
    return rs->circle_outline[index];
}

static void
raster_outline_set(struct proctex_raster* rs, int index, int32_t value)
{
    if( index >= 0 && index < rs->circle_outline_size )
        rs->circle_outline[index] = value;
}

/* --- lines ---------------------------------------------------------------------------- */

static void
raster_vline0(struct proctex_raster* rs, int x0, int y0, int y1, int32_t colour)
{
    if( y1 >= y0 )
    {
        for( int y = y0; y < y1; y++ )
            raster_put(rs, x0, y, colour);
    }
    else
    {
        for( int y = y1; y < y0; y++ )
            raster_put(rs, x0, y, colour);
    }
}

static void
raster_vline(struct proctex_raster* rs, int x0, int y0, int y1, int32_t colour)
{
    if( rs->start_x <= x0 && rs->width_mask >= x0 )
    {
        y0 = proctex_clamp_i(y0, rs->start_y, rs->height_mask);
        y1 = proctex_clamp_i(y1, rs->start_y, rs->height_mask);
        raster_vline0(rs, x0, y0, y1, colour);
    }
}

static void
raster_hline0(struct proctex_raster* rs, int x0, int x1, int y0, int32_t colour)
{
    if( x1 >= x0 )
        raster_fill_row(rs, y0, x0, x1, colour);
    else
        raster_fill_row(rs, y0, x1, x0, colour);
}

static void
raster_hline(struct proctex_raster* rs, int x0, int x1, int y0, int32_t colour)
{
    if( rs->start_y <= y0 && y0 <= rs->height_mask )
    {
        x0 = proctex_clamp_i(x0, rs->start_x, rs->width_mask);
        x1 = proctex_clamp_i(x1, rs->start_x, rs->width_mask);
        raster_hline0(rs, x0, x1, y0, colour);
    }
}

/** Bresenham over the already-clipped endpoints. */
static void
raster_line0(struct proctex_raster* rs, int x0, int x1, int y0, int y1, int32_t colour)
{
    int delta_x = x1 - x0;
    int delta_y = y1 - y0;

    if( delta_x == 0 )
    {
        if( delta_y != 0 )
            raster_vline0(rs, x0, y0, y1, colour);
        return;
    }
    if( delta_y == 0 )
    {
        raster_hline0(rs, x0, x1, y0, colour);
        return;
    }

    if( delta_x < 0 )
        delta_x = -delta_x;
    if( delta_y < 0 )
        delta_y = -delta_y;

    {
        bool steep = delta_y > delta_x;
        int y, span_x, span_y, step, error;

        if( steep )
        {
            int t = x0;
            x0 = y0;
            y0 = t;
            t = x1;
            x1 = y1;
            y1 = t;
        }
        if( x1 < x0 )
        {
            int t = x0;
            x0 = x1;
            x1 = t;
            t = y0;
            y0 = y1;
            y1 = t;
        }

        y = y0;
        span_x = x1 - x0;
        span_y = y1 - y0;
        step = y1 > y0 ? 1 : -1;
        if( span_y < 0 )
            span_y = -span_y;
        error = -(span_x >> 1);

        for( int x = x0; x <= x1; x++ )
        {
            error += span_y;
            if( steep )
                raster_put(rs, y, x, colour);
            else
                raster_put(rs, x, y, colour);
            if( error > 0 )
            {
                y += step;
                error -= span_x;
            }
        }
    }
}

/** Clip to the image, then rasterise. */
static void
raster_line(struct proctex_raster* rs, int x0, int x1, int y0, int y1, int32_t colour)
{
    int delta_x = x1 - x0;
    int delta_y = y1 - y0;
    int slope, intercept;
    int begin_x, begin_y, end_x, end_y;

    if( delta_x == 0 )
    {
        if( delta_y != 0 )
            raster_vline(rs, x0, y0, y1, colour);
        return;
    }
    if( delta_y == 0 )
    {
        raster_hline(rs, x0, x1, y0, colour);
        return;
    }

    slope = (delta_y << 12) / delta_x;
    /* A slope that rounds to zero would divide by zero when a y-clamp re-solves x. It cannot
     * happen at any bake size in use — the coordinates are 12.4 of a 64 or 128 pixel image,
     * so |delta_x| stays under 2^12 and a non-zero delta_y always survives the shift — but
     * the reference would spin forever on it rather than fail, so it is refused here. */
    if( slope == 0 )
        return;
    intercept = y0 - ((x0 * slope) >> 12);

    if( x0 < rs->start_x )
    {
        begin_y = intercept + ((rs->start_x * slope) >> 12);
        begin_x = rs->start_x;
    }
    else if( rs->width_mask >= x0 )
    {
        begin_x = x0;
        begin_y = y0;
    }
    else
    {
        begin_x = rs->width_mask;
        begin_y = ((rs->width_mask * slope) >> 12) + intercept;
    }

    if( x1 < rs->start_x )
    {
        end_x = rs->start_x;
        end_y = intercept + ((rs->start_x * slope) >> 12);
    }
    else if( x1 <= rs->width_mask )
    {
        end_x = x1;
        end_y = y1;
    }
    else
    {
        end_x = rs->width_mask;
        end_y = ((slope * rs->width_mask) >> 12) + intercept;
    }

    if( rs->start_y > end_y )
    {
        end_x = ((rs->start_y - intercept) << 12) / slope;
        end_y = rs->start_y;
    }
    else if( rs->height_mask < end_y )
    {
        end_x = ((rs->height_mask - intercept) << 12) / slope;
        end_y = rs->height_mask;
    }

    if( rs->start_y > begin_y )
    {
        begin_x = ((rs->start_y - intercept) << 12) / slope;
        begin_y = rs->start_y;
    }
    else if( begin_y > rs->height_mask )
    {
        begin_y = rs->height_mask;
        begin_x = ((rs->height_mask - intercept) << 12) / slope;
    }

    raster_line0(rs, begin_x, end_x, begin_y, end_y, colour);
}

/* --- cubic bezier --------------------------------------------------------------------- */

/*
 * 32 straight segments over t in (0, 1], evaluated in 12.4. The two variants differ only in
 * whether each segment is drawn clipped; the caller picks by testing all four control points
 * against the image, so a fully-inside curve skips the per-segment clip.
 */
static void
raster_bezier0(
    struct proctex_raster* rs,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int32_t colour)
{
    int prev_x = x0;
    int prev_y = y0;
    int x0_3 = x0 * 3;
    int y0_3 = y0 * 3;
    int x1_3 = x1 * 3;
    int x2_3 = x2 * 3;
    int y1_3 = y1 * 3;
    int y2_3 = y2 * 3;
    int cx3, cy3, cx2, cy2, cx1, cy1;

    if( x0 == x1 && y0 == y1 && x2 == x3 && y2 == y3 )
    {
        raster_line0(rs, x0, x3, y0, y3, colour);
        return;
    }

    cx3 = x1_3 + x3 - x0 - x2_3;
    cy3 = y1_3 + y3 - y2_3 - y0;
    cx2 = x2_3 + x0_3 - x1_3 - x1_3;
    cy2 = y2_3 + y0_3 - y1_3 - y1_3;
    cy1 = y1_3 - y0_3;
    cx1 = x1_3 - x0_3;

    for( int t = 128; t <= 4096; t += 128 )
    {
        int t2 = (t * t) >> 12;
        int t3 = (t * t2) >> 12;
        int x = ((cx1 * t + cx2 * t2 + cx3 * t3) >> 12) + x0;
        int y = ((cy1 * t + cy2 * t2 + cy3 * t3) >> 12) + y0;
        raster_line0(rs, prev_x, x, prev_y, y, colour);
        prev_x = x;
        prev_y = y;
    }
}

static void
raster_bezier_clamped(
    struct proctex_raster* rs,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int32_t colour)
{
    int prev_x = x0;
    int prev_y = y0;
    int x0_3 = x0 * 3;
    int y1_3 = y1 * 3;
    int x1_3 = x1 * 3;
    int x2_3 = x2 * 3;
    int y0_3 = y0 * 3;
    int y2_3 = y2 * 3;
    int cx3, cy3, cx2, cy2, cx1, cy1;

    if( x1 == x0 && y1 == y0 && x2 == x3 && y2 == y3 )
    {
        raster_line(rs, x0, x3, y0, y3, colour);
        return;
    }

    cx3 = x3 + x1_3 - x0 - x2_3;
    cx2 = x0_3 + x2_3 - x1_3 - x1_3;
    cy2 = y2_3 + y0_3 - y1_3 - y1_3;
    cy3 = y1_3 + y3 - y0 - y2_3;
    cx1 = x1_3 - x0_3;
    cy1 = y1_3 - y0_3;

    for( int t = 128; t <= 4096; t += 128 )
    {
        int t2 = (t * t) >> 12;
        int t3 = (t * t2) >> 12;
        int x = ((cx1 * t + cx2 * t2 + cx3 * t3) >> 12) + x0;
        int y = ((cy1 * t + cy2 * t2 + cy3 * t3) >> 12) + y0;
        raster_line(rs, prev_x, x, prev_y, y, colour);
        prev_y = y;
        prev_x = x;
    }
}

static void
raster_bezier(
    struct proctex_raster* rs,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int32_t colour)
{
    bool inside = rs->start_x <= x0 && x0 <= rs->width_mask && rs->start_x <= x1 &&
                  rs->width_mask >= x1 && rs->start_x <= x2 && rs->width_mask >= x2 &&
                  rs->start_x <= x3 && x3 <= rs->width_mask && y0 >= rs->start_y &&
                  y0 <= rs->height_mask && y1 >= rs->start_y && rs->height_mask >= y1 &&
                  y2 >= rs->start_y && rs->height_mask >= y2 && rs->start_y <= y3 &&
                  rs->height_mask >= y3;
    if( inside )
        raster_bezier0(rs, x0, y0, x1, y1, x2, y2, x3, y3, colour);
    else
        raster_bezier_clamped(rs, x0, y0, x1, y1, x2, y2, x3, y3, colour);
}

/* --- rectangles ----------------------------------------------------------------------- */

static void
raster_rect_clamped(
    struct proctex_raster* rs,
    int x0,
    int x1,
    int y0,
    int y1,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int top = proctex_clamp_i(y0, rs->start_y, rs->height_mask);
    int bottom = proctex_clamp_i(y1, rs->start_y, rs->height_mask);
    int left = proctex_clamp_i(x0, rs->start_x, rs->width_mask);
    int right = proctex_clamp_i(x1, rs->start_x, rs->width_mask);
    int inner_top = proctex_clamp_i(y0 + outline_width, rs->start_y, rs->height_mask);
    int inner_bottom = proctex_clamp_i(y1 - outline_width, rs->start_y, rs->height_mask);
    int inner_left, inner_right;

    for( int y = top; y < inner_top; y++ )
        raster_fill_row(rs, y, left, right, outline);
    for( int y = bottom; y > inner_bottom; y-- )
        raster_fill_row(rs, y, left, right, outline);

    inner_left = proctex_clamp_i(x0 + outline_width, rs->start_x, rs->width_mask);
    inner_right = proctex_clamp_i(x1 - outline_width, rs->start_x, rs->width_mask);
    for( int y = inner_top; y <= inner_bottom; y++ )
    {
        int32_t* row = raster_row(rs, y);
        if( !row )
            continue;
        proctex_fill_range(row, left, inner_left, rs->width, outline);
        proctex_fill_range(row, inner_left, inner_right, rs->width, fill);
        proctex_fill_range(row, inner_right, right, rs->width, outline);
    }
}

static void
raster_rect0(
    struct proctex_raster* rs,
    int x0,
    int x1,
    int y0,
    int y1,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int inner_top = y0 + outline_width;
    int inner_bottom = y1 - outline_width;
    int inner_left = x0 + outline_width;
    int inner_right = x1 - outline_width;

    for( int y = y0; y < inner_top; y++ )
        raster_fill_row(rs, y, x0, x1, outline);
    for( int y = y1; y > inner_bottom; y-- )
        raster_fill_row(rs, y, x0, x1, outline);
    for( int y = inner_top; y <= inner_bottom; y++ )
    {
        int32_t* row = raster_row(rs, y);
        if( !row )
            continue;
        proctex_fill_range(row, x0, inner_left, rs->width, outline);
        proctex_fill_range(row, inner_left, inner_right, rs->width, fill);
        proctex_fill_range(row, inner_right, x1, rs->width, outline);
    }
}

static void
raster_rect(
    struct proctex_raster* rs,
    int x0,
    int x1,
    int y0,
    int y1,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    if( x0 >= rs->start_x && x1 <= rs->width_mask && rs->start_y <= y0 && y1 <= rs->height_mask )
        raster_rect0(rs, x0, x1, y0, y1, fill, outline, outline_width);
    else
        raster_rect_clamped(rs, x0, x1, y0, y1, fill, outline, outline_width);
}

static void
raster_rect_fill(struct proctex_raster* rs, int x0, int x1, int y0, int y1, int32_t fill)
{
    if( rs->start_x <= x0 && x1 <= rs->width_mask && rs->start_y <= y0 && y1 <= rs->height_mask )
    {
        for( int y = y0; y <= y1; y++ )
            raster_fill_row(rs, y, x0, x1, fill);
        return;
    }
    {
        int top = proctex_clamp_i(y0, rs->start_y, rs->height_mask);
        int bottom = proctex_clamp_i(y1, rs->start_y, rs->height_mask);
        int left = proctex_clamp_i(x0, rs->start_x, rs->width_mask);
        int right = proctex_clamp_i(x1, rs->start_x, rs->width_mask);
        for( int y = top; y <= bottom; y++ )
            raster_fill_row(rs, y, left, right, fill);
    }
}

static void
raster_rect_outline_w1_clamped(
    struct proctex_raster* rs,
    int x0,
    int x1,
    int y0,
    int y1,
    int32_t outline)
{
    bool left_visible;
    bool right_visible;
    int top, bottom;

    if( rs->height_mask < y0 || rs->start_y > y1 )
        return;

    if( rs->start_x > x0 )
    {
        x0 = rs->start_x;
        left_visible = false;
    }
    else if( x0 > rs->width_mask )
    {
        x0 = rs->width_mask;
        left_visible = false;
    }
    else
        left_visible = true;

    if( x1 < rs->start_x )
    {
        x1 = rs->start_x;
        right_visible = false;
    }
    else if( rs->width_mask < x1 )
    {
        x1 = rs->width_mask;
        right_visible = false;
    }
    else
        right_visible = true;

    if( rs->start_y <= y0 )
    {
        top = y0 + 1;
        raster_fill_row(rs, y0, x0, x1, outline);
    }
    else
        top = rs->start_y;

    if( rs->height_mask < y1 )
        bottom = rs->height_mask;
    else
    {
        bottom = y1 - 1;
        raster_fill_row(rs, y1, x0, x1, outline);
    }

    for( int y = top; y <= bottom; y++ )
    {
        if( left_visible )
            raster_put(rs, x0, y, outline);
        if( right_visible )
            raster_put(rs, x1, y, outline);
    }
}

static void
raster_rect_outline(
    struct proctex_raster* rs,
    int x0,
    int x1,
    int y0,
    int y1,
    int32_t outline,
    int outline_width)
{
    bool inside =
        x0 >= rs->start_x && rs->width_mask >= x1 && y0 >= rs->start_y && rs->height_mask >= y1;

    if( inside && outline_width == 1 )
    {
        raster_fill_row(rs, y0, x0, x1, outline);
        raster_fill_row(rs, y1, x0, x1, outline);
        for( int y = y0 + 1; y <= y1 - 1; y++ )
        {
            raster_put(rs, x0, y, outline);
            raster_put(rs, x1, y, outline);
        }
        return;
    }
    if( inside )
    {
        int inner_top = outline_width + y0;
        int inner_bottom = y1 - outline_width;
        int inner_left = outline_width + x0;
        int inner_right = x1 - outline_width;
        for( int y = y0; y < inner_top; y++ )
            raster_fill_row(rs, y, x0, x1, outline);
        for( int y = y1; y > inner_bottom; y-- )
            raster_fill_row(rs, y, x0, x1, outline);
        for( int y = inner_top; y <= inner_bottom; y++ )
        {
            int32_t* row = raster_row(rs, y);
            if( !row )
                continue;
            proctex_fill_range(row, x0, inner_left, rs->width, outline);
            proctex_fill_range(row, inner_right, x1, rs->width, outline);
        }
        return;
    }
    if( outline_width == 1 )
    {
        raster_rect_outline_w1_clamped(rs, x0, x1, y0, y1, outline);
        return;
    }
    {
        int top = proctex_clamp_i(y0, rs->start_y, rs->height_mask);
        int bottom = proctex_clamp_i(y1, rs->start_y, rs->height_mask);
        int left = proctex_clamp_i(x0, rs->start_x, rs->width_mask);
        int right = proctex_clamp_i(x1, rs->start_x, rs->width_mask);
        int inner_top = proctex_clamp_i(outline_width + y0, rs->start_y, rs->height_mask);
        int inner_bottom = proctex_clamp_i(y1 - outline_width, rs->start_y, rs->height_mask);
        int inner_left = proctex_clamp_i(x0 + outline_width, rs->start_x, rs->width_mask);
        int inner_right = proctex_clamp_i(x1 - outline_width, rs->start_x, rs->width_mask);
        for( int y = top; y < inner_top; y++ )
            raster_fill_row(rs, y, left, right, outline);
        for( int y = bottom; y > inner_bottom; y-- )
            raster_fill_row(rs, y, left, right, outline);
        for( int y = inner_top; y <= inner_bottom; y++ )
        {
            int32_t* row = raster_row(rs, y);
            if( !row )
                continue;
            proctex_fill_range(row, left, inner_left, rs->width, outline);
            proctex_fill_range(row, inner_right, right, rs->width, outline);
        }
    }
}

/* --- circles and ellipses --------------------------------------------------------------- */

/*
 * Midpoint stepping over two conics at once: the outer edge and the inner edge that the
 * outline width leaves behind. Every `local*` name in the reference is an incremental error
 * or increment term; they are transcribed with their arithmetic intact and only grouped by
 * which conic they belong to, since re-deriving them would change the rounding.
 */
static void
raster_circle0(
    struct proctex_raster* rs,
    int x,
    int y,
    int size,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int step = 0;
    int err_outer = -size;
    int inner = size - outline_width;
    int radius = size;
    int inc_inner = -1;
    int inc_outer = -1;
    int cursor;
    int err_inner;

    if( !raster_init_circle_outline(rs, size > 0 ? size : 1) )
        return;
    if( inner < 0 )
        inner = 0;
    cursor = inner;

    raster_fill_row(rs, y, x - size, x - inner, outline);
    raster_fill_row(rs, y, x - inner, inner + x, fill);
    raster_fill_row(rs, y, inner + x, x + size, outline);
    err_inner = -inner;

    while( radius > step )
    {
        inc_outer += 2;
        err_outer += inc_outer;
        inc_inner += 2;
        err_inner += inc_inner;
        if( err_inner >= 0 && cursor >= 1 )
        {
            raster_outline_set(rs, cursor, step);
            cursor--;
            err_inner -= cursor << 1;
        }
        step++;
        if( err_outer >= 0 )
        {
            radius--;
            if( radius >= inner )
            {
                raster_fill_row(rs, y + radius, x - step, x + step, outline);
                raster_fill_row(rs, y - radius, x - step, x + step, outline);
            }
            else
            {
                int edge = raster_outline_at(rs, radius);
                raster_fill_row(rs, y + radius, x - step, x - edge, outline);
                raster_fill_row(rs, y + radius, x - edge, edge + x, fill);
                raster_fill_row(rs, y + radius, edge + x, step + x, outline);
                raster_fill_row(rs, y - radius, x - step, x - edge, outline);
                raster_fill_row(rs, y - radius, x - edge, edge + x, fill);
                raster_fill_row(rs, y - radius, edge + x, step + x, outline);
            }
            err_outer -= radius << 1;
        }

        if( inner <= step )
        {
            raster_fill_row(rs, y + step, x - radius, radius + x, outline);
            raster_fill_row(rs, y - step, x - radius, radius + x, outline);
        }
        else
        {
            int edge = step > cursor ? raster_outline_at(rs, step) : cursor;
            raster_fill_row(rs, y + step, x - radius, x - edge, outline);
            raster_fill_row(rs, y + step, x - edge, edge + x, fill);
            raster_fill_row(rs, y + step, edge + x, radius + x, outline);
            raster_fill_row(rs, y - step, x - radius, x - edge, outline);
            raster_fill_row(rs, y - step, x - edge, edge + x, fill);
            raster_fill_row(rs, y - step, edge + x, radius + x, outline);
        }
    }
}

static void
raster_circle_clamped(
    struct proctex_raster* rs,
    int x,
    int y,
    int size,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int inner = size - outline_width;
    int radius = size;
    int step = 0;
    int err_outer = -size;
    int cursor;
    int err_inner;
    int inc_inner = -1;
    int inc_outer = -1;

    if( !raster_init_circle_outline(rs, size > 0 ? size : 1) )
        return;
    if( inner < 0 )
        inner = 0;
    cursor = inner;

    if( rs->start_y <= y && y <= rs->height_mask )
    {
        int left = proctex_clamp_i(x - size, rs->start_x, rs->width_mask);
        int right = proctex_clamp_i(size + x, rs->start_x, rs->width_mask);
        int inner_left = proctex_clamp_i(x - inner, rs->start_x, rs->width_mask);
        int inner_right = proctex_clamp_i(x + inner, rs->start_x, rs->width_mask);
        raster_fill_row(rs, y, left, inner_left, outline);
        raster_fill_row(rs, y, inner_left, inner_right, fill);
        raster_fill_row(rs, y, inner_right, right, outline);
    }
    err_inner = -inner;

    while( step < radius )
    {
        inc_inner += 2;
        err_inner += inc_inner;
        inc_outer += 2;
        if( err_inner >= 0 && cursor >= 1 )
        {
            cursor--;
            err_inner -= cursor << 1;
            raster_outline_set(rs, cursor, step);
        }
        step++;
        err_outer += inc_outer;
        if( err_outer >= 0 )
        {
            radius--;
            err_outer -= radius << 1;
            {
                int top = y - radius;
                int bottom = y + radius;
                if( rs->start_y <= bottom && top <= rs->height_mask )
                {
                    if( radius >= inner )
                    {
                        int right = proctex_clamp_i(x + step, rs->start_x, rs->width_mask);
                        int left = proctex_clamp_i(x - step, rs->start_x, rs->width_mask);
                        if( rs->height_mask >= bottom )
                            raster_fill_row(rs, bottom, left, right, outline);
                        if( top >= rs->start_y )
                            raster_fill_row(rs, top, left, right, outline);
                    }
                    else
                    {
                        int edge = raster_outline_at(rs, radius);
                        int right = proctex_clamp_i(x + step, rs->start_x, rs->width_mask);
                        int left = proctex_clamp_i(x - step, rs->start_x, rs->width_mask);
                        int inner_right = proctex_clamp_i(x + edge, rs->start_x, rs->width_mask);
                        int inner_left = proctex_clamp_i(x - edge, rs->start_x, rs->width_mask);
                        if( rs->height_mask >= bottom )
                        {
                            raster_fill_row(rs, bottom, left, inner_left, outline);
                            raster_fill_row(rs, bottom, inner_left, inner_right, fill);
                            raster_fill_row(rs, bottom, inner_right, right, outline);
                        }
                        if( rs->start_y <= top )
                        {
                            raster_fill_row(rs, top, left, inner_left, outline);
                            raster_fill_row(rs, top, inner_left, inner_right, fill);
                            raster_fill_row(rs, top, inner_right, right, outline);
                        }
                    }
                }
            }
        }

        {
            int bottom = y + step;
            int top = y - step;
            if( rs->start_y <= bottom && rs->height_mask >= top )
            {
                int right_raw = radius + x;
                int left_raw = x - radius;
                if( right_raw >= rs->start_x && rs->width_mask >= left_raw )
                {
                    int right = proctex_clamp_i(right_raw, rs->start_x, rs->width_mask);
                    int left = proctex_clamp_i(left_raw, rs->start_x, rs->width_mask);
                    if( step >= inner )
                    {
                        if( rs->height_mask >= bottom )
                            raster_fill_row(rs, bottom, left, right, outline);
                        if( rs->start_y <= top )
                            raster_fill_row(rs, top, left, right, outline);
                    }
                    else
                    {
                        int edge = cursor >= step ? cursor : raster_outline_at(rs, step);
                        int inner_right = proctex_clamp_i(x + edge, rs->start_x, rs->width_mask);
                        int inner_left = proctex_clamp_i(x - edge, rs->start_x, rs->width_mask);
                        if( rs->height_mask >= bottom )
                        {
                            raster_fill_row(rs, bottom, left, inner_left, outline);
                            raster_fill_row(rs, bottom, inner_left, inner_right, fill);
                            raster_fill_row(rs, bottom, inner_right, right, outline);
                        }
                        if( rs->start_y <= top )
                        {
                            raster_fill_row(rs, top, left, inner_left, outline);
                            raster_fill_row(rs, top, inner_left, inner_right, fill);
                            raster_fill_row(rs, top, inner_right, right, outline);
                        }
                    }
                }
            }
        }
    }
}

static void
raster_circle(
    struct proctex_raster* rs,
    int x,
    int y,
    int size,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    if( rs->start_x <= x - size && size + x <= rs->width_mask && y - size >= rs->start_y &&
        rs->height_mask >= size + y )
        raster_circle0(rs, x, y, size, fill, outline, outline_width);
    else
        raster_circle_clamped(rs, x, y, size, fill, outline, outline_width);
}

static void
raster_ellipse0(
    struct proctex_raster* rs,
    int x,
    int y,
    int size_x,
    int size_y,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int step_outer = 0;
    int row = size_y;
    int inner_x = size_x - outline_width;
    int step_inner = 0;
    int inner_y = size_y - outline_width;
    int ax2 = size_x * size_x;
    int by2 = size_y * size_y;
    int aix2 = inner_x * inner_x;
    int biy2 = inner_y * inner_y;
    int by2_2 = by2 << 1;
    int biy2_2 = biy2 << 1;
    int ax2_2 = ax2 << 1;
    int aix2_2 = aix2 << 1;
    int size_y_2 = size_y << 1;
    int inner_y_2 = inner_y << 1;
    int err_outer_a = ax2 * (1 - size_y_2) + by2_2;
    int err_outer_b = by2 - ax2_2 * (size_y_2 - 1);
    int err_inner_a = biy2_2 + (1 - inner_y_2) * aix2;
    int err_inner_b = biy2 - aix2_2 * (inner_y_2 - 1);
    int ax2_4 = ax2 << 2;
    int by2_4 = by2 << 2;
    int biy2_4 = biy2 << 2;
    int aix2_4 = aix2 << 2;
    int d_outer_a = by2_2 * 3;
    int d_outer_b = ax2_2 * (size_y_2 - 3);
    int d_inner_a = biy2_2 * 3;
    int d_outer_c = by2_4;
    int d_inner_b = (inner_y_2 - 3) * aix2_2;
    int d_inner_c = biy2_4;
    int d_outer_d = (size_y - 1) * ax2_4;
    int d_inner_d = aix2_4 * (inner_y - 1);

    raster_fill_row(rs, y, x - size_x, x - inner_x, outline);
    raster_fill_row(rs, y, x - inner_x, inner_x + x, fill);
    raster_fill_row(rs, y, inner_x + x, x + size_x, outline);

    while( row > 0 )
    {
        int left, right, bottom, top;
        bool bevelled;

        while( err_outer_a < 0 )
        {
            err_outer_a += d_outer_a;
            d_outer_a += by2_4;
            step_outer++;
            err_outer_b += d_outer_c;
            d_outer_c += by2_4;
        }
        if( err_outer_b < 0 )
        {
            err_outer_a += d_outer_a;
            err_outer_b += d_outer_c;
            d_outer_a += by2_4;
            step_outer++;
            d_outer_c += by2_4;
        }
        err_outer_a += -d_outer_d;
        left = x - step_outer;
        bevelled = inner_y >= row;
        right = x + step_outer;
        d_outer_d -= ax2_4;
        row--;
        err_outer_b += -d_outer_b;
        bottom = row + y;
        d_outer_b -= ax2_4;

        if( bevelled )
        {
            while( err_inner_a < 0 )
            {
                step_inner++;
                err_inner_b += d_inner_c;
                err_inner_a += d_inner_a;
                d_inner_c += biy2_4;
                d_inner_a += biy2_4;
            }
            if( err_inner_b < 0 )
            {
                err_inner_a += d_inner_a;
                d_inner_a += biy2_4;
                step_inner++;
                err_inner_b += d_inner_c;
                d_inner_c += biy2_4;
            }
            err_inner_b += -d_inner_b;
            err_inner_a += -d_inner_d;
            d_inner_d -= aix2_4;
            d_inner_b -= aix2_4;
        }

        top = y - row;
        if( bevelled )
        {
            int inner_left = x - step_inner;
            int inner_right = x + step_inner;
            raster_fill_row(rs, top, left, inner_left, outline);
            raster_fill_row(rs, top, inner_left, inner_right, fill);
            raster_fill_row(rs, top, inner_right, right, outline);
            raster_fill_row(rs, bottom, left, inner_left, outline);
            raster_fill_row(rs, bottom, inner_left, inner_right, fill);
            raster_fill_row(rs, bottom, inner_right, right, outline);
        }
        else
        {
            raster_fill_row(rs, top, left, right, outline);
            raster_fill_row(rs, bottom, left, right, outline);
        }
    }
}

static void
raster_ellipse_clamped(
    struct proctex_raster* rs,
    int x,
    int y,
    int size_x,
    int size_y,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    int step_outer = 0;
    int inner_y = size_y - outline_width;
    int row = size_y;
    int step_inner = 0;
    int inner_x = size_x - outline_width;
    int ax2 = size_x * size_x;
    int by2 = size_y * size_y;
    int aix2 = inner_x * inner_x;
    int by2_2 = by2 << 1;
    int biy2 = inner_y * inner_y;
    int ax2_2 = ax2 << 1;
    int biy2_2 = biy2 << 1;
    int aix2_2 = aix2 << 1;
    int size_y_2 = size_y << 1;
    int inner_y_2 = inner_y << 1;
    int err_outer_a = by2_2 + (1 - size_y_2) * ax2;
    int err_outer_b = by2 - (size_y_2 - 1) * ax2_2;
    int err_inner_a = aix2 * (1 - inner_y_2) + biy2_2;
    int ax2_4 = ax2 << 2;
    int err_inner_b = biy2 - aix2_2 * (inner_y_2 - 1);
    int by2_4 = by2 << 2;
    int aix2_4 = aix2 << 2;
    int biy2_4 = biy2 << 2;
    int d_outer_a = by2_2 * 3;
    int d_inner_a = biy2_2 * 3;
    int d_outer_b = ax2_2 * (size_y_2 - 3);
    int d_outer_c = by2_4;
    int d_inner_b = (inner_y_2 - 3) * aix2_2;
    int d_outer_d = ax2_4 * (size_y - 1);
    int d_inner_c = biy2_4;
    int d_inner_d = (inner_y - 1) * aix2_4;

    if( y >= rs->start_y && rs->height_mask >= y )
    {
        int left = proctex_clamp_i(x - size_x, rs->start_x, rs->width_mask);
        int right = proctex_clamp_i(x + size_x, rs->start_x, rs->width_mask);
        int inner_left = proctex_clamp_i(x - inner_x, rs->start_x, rs->width_mask);
        int inner_right = proctex_clamp_i(x + inner_x, rs->start_x, rs->width_mask);
        raster_fill_row(rs, y, left, inner_left, outline);
        raster_fill_row(rs, y, inner_left, inner_right, fill);
        raster_fill_row(rs, y, inner_right, right, outline);
    }

    while( row > 0 )
    {
        bool bevelled;
        int bottom, top;

        while( err_outer_a < 0 )
        {
            err_outer_a += d_outer_a;
            step_outer++;
            d_outer_a += by2_4;
            err_outer_b += d_outer_c;
            d_outer_c += by2_4;
        }
        if( err_outer_b < 0 )
        {
            step_outer++;
            err_outer_a += d_outer_a;
            d_outer_a += by2_4;
            err_outer_b += d_outer_c;
            d_outer_c += by2_4;
        }
        bevelled = inner_y >= row;
        err_outer_b += -d_outer_b;
        row--;
        if( bevelled )
        {
            while( err_inner_a < 0 )
            {
                err_inner_a += d_inner_a;
                err_inner_b += d_inner_c;
                d_inner_a += biy2_4;
                d_inner_c += biy2_4;
                step_inner++;
            }
            if( err_inner_b < 0 )
            {
                err_inner_a += d_inner_a;
                d_inner_a += biy2_4;
                err_inner_b += d_inner_c;
                d_inner_c += biy2_4;
                step_inner++;
            }
            err_inner_b += -d_inner_b;
            d_inner_b -= aix2_4;
            err_inner_a += -d_inner_d;
            d_inner_d -= aix2_4;
        }
        err_outer_a += -d_outer_d;
        bottom = row + y;
        top = y - row;
        d_outer_d -= ax2_4;
        d_outer_b -= ax2_4;

        if( bottom >= rs->start_y && rs->height_mask >= top )
        {
            int right = proctex_clamp_i(step_outer + x, rs->start_x, rs->width_mask);
            int left = proctex_clamp_i(x - step_outer, rs->start_x, rs->width_mask);
            if( bevelled )
            {
                int inner_right = proctex_clamp_i(x + step_inner, rs->start_x, rs->width_mask);
                int inner_left = proctex_clamp_i(x - step_inner, rs->start_x, rs->width_mask);
                if( rs->start_y <= top )
                {
                    raster_fill_row(rs, top, left, inner_left, outline);
                    raster_fill_row(rs, top, inner_left, inner_right, fill);
                    raster_fill_row(rs, top, inner_right, right, outline);
                }
                if( rs->height_mask >= bottom )
                {
                    raster_fill_row(rs, bottom, left, inner_left, outline);
                    raster_fill_row(rs, bottom, inner_left, inner_right, fill);
                    raster_fill_row(rs, bottom, inner_right, right, outline);
                }
            }
            else
            {
                if( top >= rs->start_y )
                    raster_fill_row(rs, top, left, right, outline);
                if( rs->height_mask >= bottom )
                    raster_fill_row(rs, bottom, left, right, outline);
            }
        }
    }
}

static void
raster_ellipse(
    struct proctex_raster* rs,
    int x,
    int y,
    int size_x,
    int size_y,
    int32_t fill,
    int32_t outline,
    int outline_width)
{
    if( size_x == size_y )
        raster_circle(rs, x, y, size_x, fill, outline, outline_width);
    else if(
        x - size_x >= rs->start_x && rs->width_mask >= size_x + x && y - size_y >= rs->start_y &&
        size_y + y <= rs->height_mask )
        raster_ellipse0(rs, x, y, size_x, size_y, fill, outline, outline_width);
    else
        raster_ellipse_clamped(rs, x, y, size_x, size_y, fill, outline, outline_width);
}

static void
raster_circle_fill(struct proctex_raster* rs, int x, int y, int size, int32_t fill)
{
    bool inside = x - size >= rs->start_x && rs->width_mask >= x + size && y - size >= rs->start_y &&
                  y + size <= rs->height_mask;
    int step = 0;
    int radius = size;
    int err = -size;
    int inc = -1;

    if( inside )
    {
        raster_fill_row(rs, y, x - size, size + x, fill);
        while( radius > step )
        {
            inc += 2;
            step++;
            err += inc;
            if( err >= 0 )
            {
                radius--;
                err -= radius << 1;
                raster_fill_row(rs, y - radius, x - step, x + step, fill);
                raster_fill_row(rs, y + radius, x - step, x + step, fill);
            }
            raster_fill_row(rs, y + step, x - radius, x + radius, fill);
            raster_fill_row(rs, y - step, x - radius, x + radius, fill);
        }
        return;
    }

    {
        int right = proctex_clamp_i(size + x, rs->start_x, rs->width_mask);
        int left = proctex_clamp_i(x - size, rs->start_x, rs->width_mask);
        raster_fill_row(rs, y, left, right, fill);
    }
    while( radius > step )
    {
        inc += 2;
        err += inc;
        /* `> 0` here, against `>= 0` in the unclipped path: the reference's asymmetry,
         * which shifts the clipped circle's stepping by one row. */
        if( err > 0 )
        {
            radius--;
            err -= radius << 1;
            {
                int top = y - radius;
                int bottom = radius + y;
                if( bottom >= rs->start_y && top <= rs->height_mask )
                {
                    int r = proctex_clamp_i(x + step, rs->start_x, rs->width_mask);
                    int l = proctex_clamp_i(x - step, rs->start_x, rs->width_mask);
                    if( rs->height_mask >= bottom )
                        raster_fill_row(rs, bottom, l, r, fill);
                    if( rs->start_y <= top )
                        raster_fill_row(rs, top, l, r, fill);
                }
            }
        }
        step++;
        {
            int top = y - step;
            int bottom = y + step;
            if( rs->start_y <= bottom && top <= rs->height_mask )
            {
                int r = proctex_clamp_i(x + radius, rs->start_x, rs->width_mask);
                int l = proctex_clamp_i(x - radius, rs->start_x, rs->width_mask);
                if( bottom <= rs->height_mask )
                    raster_fill_row(rs, bottom, l, r, fill);
                if( rs->start_y <= top )
                    raster_fill_row(rs, top, l, r, fill);
            }
        }
    }
}

static void
raster_ellipse_fill(
    struct proctex_raster* rs,
    int x,
    int y,
    int size_x,
    int size_y,
    int32_t fill)
{
    int step = 0;
    int row = size_y;
    int ax2 = size_x * size_x;
    int by2 = size_y * size_y;
    int ax2_2 = ax2 << 1;
    int size_y_2 = size_y << 1;
    int by2_2 = by2 << 1;
    int err_b = by2 - ax2_2 * (size_y_2 - 1);
    int err_a = by2_2 + (1 - size_y_2) * ax2;
    int ax2_4 = ax2 << 2;
    int by2_4 = by2 << 2;
    int d_a = by2_2 * 3;
    int d_b = ax2_2 * ((size_y << 1) - 3);
    int d_c = by2_4;
    int d_d = (size_y - 1) * ax2_4;
    bool inside;

    if( size_x == size_y )
    {
        raster_circle_fill(rs, x, y, size_x, fill);
        return;
    }

    inside = rs->start_x <= x - size_x && rs->width_mask >= x + size_x && y - size_y >= rs->start_y &&
             rs->height_mask >= y + size_y;

    if( inside )
        raster_fill_row(rs, y, x - size_x, size_x + x, fill);
    else if( y >= rs->start_y && rs->height_mask >= y )
    {
        int right = proctex_clamp_i(x + size_x, rs->start_x, rs->width_mask);
        int left = proctex_clamp_i(x - size_x, rs->start_x, rs->width_mask);
        raster_fill_row(rs, y, left, right, fill);
    }

    while( row > 0 )
    {
        int top, bottom;

        while( err_a < 0 )
        {
            step++;
            err_a += d_a;
            err_b += d_c;
            d_c += by2_4;
            d_a += by2_4;
        }
        row--;
        if( err_b < 0 )
        {
            err_b += d_c;
            err_a += d_a;
            d_a += by2_4;
            d_c += by2_4;
            step++;
        }
        top = y - row;
        err_a += -d_d;
        d_d -= ax2_4;
        bottom = row + y;
        err_b += -d_b;
        d_b -= ax2_4;

        if( inside )
        {
            raster_fill_row(rs, top, x - step, x + step, fill);
            raster_fill_row(rs, bottom, x - step, x + step, fill);
        }
        else if( bottom >= rs->start_y && rs->height_mask >= top )
        {
            int right = proctex_clamp_i(step + x, rs->start_x, rs->width_mask);
            int left = proctex_clamp_i(x - step, rs->start_x, rs->width_mask);
            if( rs->start_y <= top )
                raster_fill_row(rs, top, left, right, fill);
            if( bottom <= rs->height_mask )
                raster_fill_row(rs, bottom, left, right, fill);
        }
    }
}

/* --- the operation --------------------------------------------------------------------- */

/*
 * Shape coordinates are 12.4 of the texture's own size, so a shape is resolution-independent
 * and `(coord * width) >> 12` places it. A shape with no fill draws only its outline and one
 * with no outline only its fill, which is how a line (a single colour, assigned to the
 * outline at decode) and a rectangle (both) end up in different paths.
 */
static void
proctex_raster_draw(struct proctex_raster* rs, const struct RSCache_ProcTexShape* shape)
{
    int width = rs->width;
    int height = rs->height;
    bool has_fill = shape->fill_colour >= 0;
    bool has_outline = shape->outline_colour >= 0;

    switch( shape->kind )
    {
    case 0: /* line — outline only */
        if( has_outline )
            raster_line(
                rs,
                (shape->x[0] * width) >> 12,
                (shape->x[1] * width) >> 12,
                (shape->y[0] * height) >> 12,
                (shape->y[1] * height) >> 12,
                shape->outline_colour);
        break;

    case 1: /* cubic bezier — outline only */
        if( has_outline )
            raster_bezier(
                rs,
                (width * shape->x[0]) >> 12,
                (height * shape->y[0]) >> 12,
                (width * shape->x[1]) >> 12,
                (height * shape->y[1]) >> 12,
                (width * shape->x[2]) >> 12,
                (height * shape->y[2]) >> 12,
                (width * shape->x[3]) >> 12,
                (height * shape->y[3]) >> 12,
                shape->outline_colour);
        break;

    case 2: /* rectangle */
    {
        int x0 = (shape->x[0] * width) >> 12;
        int x1 = (shape->x[1] * width) >> 12;
        int y0 = (shape->y[0] * height) >> 12;
        int y1 = (shape->y[1] * height) >> 12;
        if( has_fill && has_outline )
            raster_rect(
                rs, x0, x1, y0, y1, shape->fill_colour, shape->outline_colour,
                shape->outline_width);
        else if( has_fill )
            raster_rect_fill(rs, x0, x1, y0, y1, shape->fill_colour);
        else if( has_outline )
            raster_rect_outline(
                rs, x0, x1, y0, y1, shape->outline_colour, shape->outline_width);
        break;
    }

    case 3: /* ellipse — centre plus radii; an outline without a fill draws nothing, as in
             * the reference, whose renderOutline for this shape is empty */
    {
        int cx = (shape->x[0] * width) >> 12;
        int cy = (shape->y[0] * height) >> 12;
        int rx = (shape->x[1] * width) >> 12;
        int ry = (shape->y[1] * height) >> 12;
        if( has_fill && has_outline )
            raster_ellipse(
                rs, cx, cy, rx, ry, shape->fill_colour, shape->outline_colour,
                shape->outline_width);
        else if( has_fill )
            raster_ellipse_fill(rs, cx, cy, rx, ry, shape->fill_colour);
        break;
    }

    default:
        break;
    }
}

bool
proctex_op_rasterizer(struct ProcTexGenerator* gen, int op_index, int line)
{
    const struct RSCache_ProcTexOperation* op = &gen->tex->operations[op_index];
    struct proctex_cache* cache = &gen->caches[op_index];
    int width = gen->width;
    int height = gen->height;
    struct proctex_raster rs;
    int32_t* packed = NULL;

    (void)line;

    rs.width = width;
    rs.height = height;
    rs.width_mask = gen->width_mask;
    rs.height_mask = gen->height_mask;
    rs.start_x = 0;
    rs.start_y = 0;
    rs.circle_outline = NULL;
    rs.circle_outline_size = 0;

    if( op->is_monochrome )
    {
        /* The reference draws packed RGB straight into the monochrome plane. It is not a
         * meaningful monochrome signal, but downstream operations were authored against it,
         * so it is reproduced rather than converted. */
        rs.pixels = cache->plane[0];
    }
    else
    {
        packed = calloc((size_t)width * (size_t)height, sizeof(int32_t));
        if( !packed )
            return false;
        rs.pixels = packed;
    }

    for( int i = 0; i < op->u.rasterizer.shape_count; i++ )
        proctex_raster_draw(&rs, &op->u.rasterizer.shapes[i]);

    if( packed )
    {
        for( int y = 0; y < height; y++ )
        {
            size_t offset = (size_t)y * (size_t)width;
            int32_t* out_r = cache->plane[0] + offset;
            int32_t* out_g = cache->plane[1] + offset;
            int32_t* out_b = cache->plane[2] + offset;
            for( int x = 0; x < width; x++ )
            {
                int32_t rgb = packed[offset + (size_t)x];
                out_r[x] = (rgb >> 12) & 0xFF0;
                out_g[x] = (rgb >> 4) & 0xFF0;
                out_b[x] = (rgb & 0xFF) << 4;
            }
        }
        free(packed);
    }

    free(rs.circle_outline);
    proctex_mark_all_done(cache, height);
    return true;
}
