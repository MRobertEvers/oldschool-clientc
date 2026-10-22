/*
 * The rev-241 depth-buffered triangle walk (class162.method5165 / 5169 / 5170 /
 * 5171), included once per shading mode. The four setups are the same three-arm
 * walk -- which corner is on top, then which of the other two comes next --
 * and differ only in what they carry down each row besides the two edges and
 * the depth. That is what the includer supplies:
 *
 *   RZT_FN                     entry point name
 *   RZT_PARAMS                 the mode's own parameters, after the corners
 *   RZT_SETUP                  mode setup on the rounded corners; may return
 *   RZT_ROW_DECLS              the mode's per-row values
 *   RZT_ROW_START(i, x_top)    start them at corner i (0..2), rounded x
 *   RZT_ROW_PRESTEP(y_top)     move them down to row 0 from a negative y
 *   RZT_ROWS_BEGIN(y_top)      anything that needs the clipped top row
 *   RZT_SPAN(row, left, right) one scanline; `key_row` and `key_step_x` are live
 *   RZT_ROW_ADVANCE()          step them one row
 *
 * The corners arrive as floats: the 241 projection keeps sub-pixel positions.
 * Edges and rows are walked from the corners rounded to whole pixels; the depth
 * plane is solved from the exact ones. Arm for arm and branch for branch this
 * is the deob's control flow -- including the two different left/right tests,
 * which decide which edge bounds a span and so which pixels a seam lands on.
 */

#ifndef RZT_ROW_DECLS
#define RZT_ROW_DECLS
#endif

/* Two trapezoids down from a clipped top: `rows_first` rows bounded by the
 * first pair of edges, then the rest to the bottom by the second. */
#define RZT_TRAPEZOIDS(y_top, y_mid, y_bottom, LEFT1, RIGHT1, STEP1, LEFT2, RIGHT2, STEP2)         \
    do                                                                                             \
    {                                                                                              \
        float const first_row = (float)rszbuf_java_int((y_top) + 0.5f);                            \
        float const mid_row = (float)rszbuf_java_int((y_mid) + 0.5f);                              \
        float rows_second = (float)rszbuf_java_int((y_bottom) + 0.5f) - mid_row;                   \
        float rows_first = mid_row - first_row;                                                    \
        int row = toridraw_wrap_mul(rszbuf_java_int(first_row), r->stride);                        \
        for( ; --rows_first >= 0.0f; key_row += key_step_y )                                      \
        {                                                                                          \
            RZT_SPAN(row, rszbuf_java_int(LEFT1), rszbuf_java_int(RIGHT1));                        \
            STEP1;                                                                                 \
            RZT_ROW_ADVANCE();                                                                     \
            row += r->stride;                                                                      \
        }                                                                                          \
        while( --rows_second >= 0.0f )                                                             \
        {                                                                                          \
            RZT_SPAN(row, rszbuf_java_int(LEFT2), rszbuf_java_int(RIGHT2));                        \
            STEP2;                                                                                 \
            RZT_ROW_ADVANCE();                                                                     \
            row += r->stride;                                                                      \
            key_row += key_step_y;                                                                 \
        }                                                                                          \
    } while( 0 )

static inline void
RZT_FN(
    struct ToriDraw_RsZbufRaster* r,
    float y_a,
    float y_b,
    float y_c,
    float x_a,
    float x_b,
    float x_c,
    float key_a,
    float key_b,
    float key_c RZT_PARAMS)
{
    RSZBUF_NO_FMA
    float x_a_px = (float)rszbuf_java_int(x_a + 0.5f);
    float x_b_px = (float)rszbuf_java_int(x_b + 0.5f);
    float x_c_px = (float)rszbuf_java_int(x_c + 0.5f);
    float y_a_px = (float)rszbuf_java_int(y_a + 0.5f);
    float y_b_px = (float)rszbuf_java_int(y_b + 0.5f);
    float y_c_px = (float)rszbuf_java_int(y_c + 0.5f);
    float const dx_ab_px = x_b_px - x_a_px;
    float const dy_ab_px = y_b_px - y_a_px;
    float const dx_ac_px = x_c_px - x_a_px;
    float const dy_ac_px = y_c_px - y_a_px;

    RZT_SETUP

    /* The depth plane, from the exact corners. */
    float const dx_ab = x_b - x_a;
    float const dy_ab = y_b - y_a;
    float const dx_ac = x_c - x_a;
    float const dy_ac = y_c - y_a;
    float const area = dx_ab * dy_ac - dy_ab * dx_ac;
    if( area == 0.0f )
        return;

    float const dkey_ab = key_b - key_a;
    float const dkey_ac = key_c - key_a;
    float const key_step_x = (dy_ac * dkey_ab - dy_ab * dkey_ac) / area;
    float const key_step_y = (dx_ab * dkey_ac - dx_ac * dkey_ab) / area;

    /* Edge slopes, from the rounded corners. */
    float const slope_bc = y_b_px == y_c_px ? 0.0f : (x_c_px - x_b_px) / (y_c_px - y_b_px);
    float const slope_ab = y_a_px == y_b_px ? 0.0f : dx_ab_px / dy_ab_px;
    float const slope_ac = y_a_px == y_c_px ? 0.0f : dx_ac_px / dy_ac_px;

    float const clip_height = (float)r->clip_height;
    float key_row;
    RZT_ROW_DECLS

    if( y_a_px <= y_b_px && y_a_px <= y_c_px )
    {
        /* A on top. */
        if( y_a_px >= clip_height )
            return;
        if( y_b_px > clip_height )
            y_b_px = clip_height;
        if( y_c_px > clip_height )
            y_c_px = clip_height;
        RZT_ROW_START(0, x_a_px);
        key_row = rszbuf_row_start_depth(x_a, y_a, key_a, key_step_x, key_step_y);

        float edge_ac = x_a_px;
        if( y_b_px < y_c_px )
        {
            /* then B, then C */
            if( y_a_px < 0.0f )
            {
                edge_ac = x_a_px - y_a_px * slope_ac;
                x_a_px -= y_a_px * slope_ab;
                RZT_ROW_PRESTEP(y_a_px);
                y_a_px = 0.0f;
            }
            if( y_b_px < 0.0f )
            {
                x_b_px -= y_b_px * slope_bc;
                y_b_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_a_px);
            if( (y_a_px == y_b_px || !(slope_ac < slope_ab)) &&
                (y_a_px != y_b_px || !(slope_ac > slope_bc)) )
                RZT_TRAPEZOIDS(
                    y_a_px, y_b_px, y_c_px,
                    x_a_px, edge_ac, (edge_ac += slope_ac, x_a_px += slope_ab),
                    x_b_px, edge_ac, (edge_ac += slope_ac, x_b_px += slope_bc));
            else
                RZT_TRAPEZOIDS(
                    y_a_px, y_b_px, y_c_px,
                    edge_ac, x_a_px, (edge_ac += slope_ac, x_a_px += slope_ab),
                    edge_ac, x_b_px, (edge_ac += slope_ac, x_b_px += slope_bc));
        }
        else
        {
            /* then C, then B */
            if( y_a_px < 0.0f )
            {
                edge_ac = x_a_px - y_a_px * slope_ac;
                x_a_px -= y_a_px * slope_ab;
                RZT_ROW_PRESTEP(y_a_px);
                y_a_px = 0.0f;
            }
            if( y_c_px < 0.0f )
            {
                x_c_px -= y_c_px * slope_bc;
                y_c_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_a_px);
            if( (y_a_px == y_c_px || !(slope_ac < slope_ab)) &&
                (y_a_px != y_c_px || !(slope_bc > slope_ab)) )
                RZT_TRAPEZOIDS(
                    y_a_px, y_c_px, y_b_px,
                    x_a_px, edge_ac, (edge_ac += slope_ac, x_a_px += slope_ab),
                    x_a_px, x_c_px, (x_c_px += slope_bc, x_a_px += slope_ab));
            else
                RZT_TRAPEZOIDS(
                    y_a_px, y_c_px, y_b_px,
                    edge_ac, x_a_px, (edge_ac += slope_ac, x_a_px += slope_ab),
                    x_c_px, x_a_px, (x_c_px += slope_bc, x_a_px += slope_ab));
        }
    }
    else if( y_b_px <= y_c_px )
    {
        /* B on top. */
        if( y_b_px >= clip_height )
            return;
        if( y_c_px > clip_height )
            y_c_px = clip_height;
        if( y_a_px > clip_height )
            y_a_px = clip_height;
        RZT_ROW_START(1, x_b_px);
        key_row = rszbuf_row_start_depth(x_b, y_b, key_b, key_step_x, key_step_y);

        float edge_ba = x_b_px;
        if( y_c_px < y_a_px )
        {
            /* then C, then A */
            if( y_b_px < 0.0f )
            {
                edge_ba = x_b_px - y_b_px * slope_ab;
                x_b_px -= y_b_px * slope_bc;
                RZT_ROW_PRESTEP(y_b_px);
                y_b_px = 0.0f;
            }
            if( y_c_px < 0.0f )
            {
                x_c_px -= y_c_px * slope_ac;
                y_c_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_b_px);
            if( (y_b_px == y_c_px || !(slope_ab < slope_bc)) &&
                (y_b_px != y_c_px || !(slope_ab > slope_ac)) )
                RZT_TRAPEZOIDS(
                    y_b_px, y_c_px, y_a_px,
                    x_b_px, edge_ba, (edge_ba += slope_ab, x_b_px += slope_bc),
                    x_c_px, edge_ba, (edge_ba += slope_ab, x_c_px += slope_ac));
            else
                RZT_TRAPEZOIDS(
                    y_b_px, y_c_px, y_a_px,
                    edge_ba, x_b_px, (edge_ba += slope_ab, x_b_px += slope_bc),
                    edge_ba, x_c_px, (edge_ba += slope_ab, x_c_px += slope_ac));
        }
        else
        {
            /* then A, then C */
            if( y_b_px < 0.0f )
            {
                edge_ba = x_b_px - y_b_px * slope_ab;
                x_b_px -= y_b_px * slope_bc;
                RZT_ROW_PRESTEP(y_b_px);
                y_b_px = 0.0f;
            }
            if( y_a_px < 0.0f )
            {
                x_a_px -= y_a_px * slope_ac;
                y_a_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_b_px);
            if( slope_ab < slope_bc )
                RZT_TRAPEZOIDS(
                    y_b_px, y_a_px, y_c_px,
                    edge_ba, x_b_px, (edge_ba += slope_ab, x_b_px += slope_bc),
                    x_a_px, x_b_px, (x_a_px += slope_ac, x_b_px += slope_bc));
            else
                RZT_TRAPEZOIDS(
                    y_b_px, y_a_px, y_c_px,
                    x_b_px, edge_ba, (edge_ba += slope_ab, x_b_px += slope_bc),
                    x_b_px, x_a_px, (x_a_px += slope_ac, x_b_px += slope_bc));
        }
    }
    else
    {
        /* C on top. */
        if( y_c_px >= clip_height )
            return;
        if( y_a_px > clip_height )
            y_a_px = clip_height;
        if( y_b_px > clip_height )
            y_b_px = clip_height;
        RZT_ROW_START(2, x_c_px);
        key_row = rszbuf_row_start_depth(x_c, y_c, key_c, key_step_x, key_step_y);

        float edge_cb = x_c_px;
        if( y_a_px < y_b_px )
        {
            /* then A, then B */
            if( y_c_px < 0.0f )
            {
                edge_cb = x_c_px - y_c_px * slope_bc;
                x_c_px -= y_c_px * slope_ac;
                RZT_ROW_PRESTEP(y_c_px);
                y_c_px = 0.0f;
            }
            if( y_a_px < 0.0f )
            {
                x_a_px -= y_a_px * slope_ab;
                y_a_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_c_px);
            if( slope_bc < slope_ac )
                RZT_TRAPEZOIDS(
                    y_c_px, y_a_px, y_b_px,
                    edge_cb, x_c_px, (edge_cb += slope_bc, x_c_px += slope_ac),
                    edge_cb, x_a_px, (edge_cb += slope_bc, x_a_px += slope_ab));
            else
                RZT_TRAPEZOIDS(
                    y_c_px, y_a_px, y_b_px,
                    x_c_px, edge_cb, (edge_cb += slope_bc, x_c_px += slope_ac),
                    x_a_px, edge_cb, (edge_cb += slope_bc, x_a_px += slope_ab));
        }
        else
        {
            /* then B, then A */
            if( y_c_px < 0.0f )
            {
                edge_cb = x_c_px - y_c_px * slope_bc;
                x_c_px -= y_c_px * slope_ac;
                RZT_ROW_PRESTEP(y_c_px);
                y_c_px = 0.0f;
            }
            if( y_b_px < 0.0f )
            {
                x_b_px -= y_b_px * slope_ab;
                y_b_px = 0.0f;
            }
            RZT_ROWS_BEGIN(y_c_px);
            if( slope_bc < slope_ac )
                RZT_TRAPEZOIDS(
                    y_c_px, y_b_px, y_a_px,
                    edge_cb, x_c_px, (edge_cb += slope_bc, x_c_px += slope_ac),
                    x_b_px, x_c_px, (x_b_px += slope_ab, x_c_px += slope_ac));
            else
                RZT_TRAPEZOIDS(
                    y_c_px, y_b_px, y_a_px,
                    x_c_px, edge_cb, (edge_cb += slope_bc, x_c_px += slope_ac),
                    x_c_px, x_b_px, (x_b_px += slope_ab, x_c_px += slope_ac));
        }
    }
}

#undef RZT_TRAPEZOIDS
#undef RZT_FN
#undef RZT_PARAMS
#undef RZT_SETUP
#undef RZT_ROW_DECLS
#undef RZT_ROW_START
#undef RZT_ROW_PRESTEP
#undef RZT_ROWS_BEGIN
#undef RZT_SPAN
#undef RZT_ROW_ADVANCE
