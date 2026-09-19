#include "torirs_polygon.h"

#include <assert.h>

void
ToriRS_PolygonFillConvex(
    const int* xs,
    const int* ys,
    int count,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    ToriRS_PolygonSpanFn span,
    void* user_data)
{
    int top;
    int bottom;

    assert(xs);
    assert(ys);
    assert(span);
    assert(count >= 0);

    if( count < 3 || clip_w <= 0 || clip_h <= 0 )
        return;

    top = ys[0];
    bottom = ys[0];
    for( int i = 1; i < count; i++ )
    {
        if( ys[i] < top )
            top = ys[i];
        if( ys[i] > bottom )
            bottom = ys[i];
    }

    if( top < clip_y )
        top = clip_y;
    if( bottom >= clip_y + clip_h )
        bottom = clip_y + clip_h - 1;

    for( int y = top; y <= bottom; y++ )
    {
        /*
         * The row's extent, taken as the min and max of every edge crossing.
         *
         * For a convex polygon a scanline crosses the boundary exactly twice,
         * so min and max ARE the span — there is no need to collect and sort
         * crossings the way a general fill must. That is the whole reason this
         * is restricted to convex input.
         */
        long long left = 0;
        long long right = 0;
        int have = 0;

        for( int i = 0; i < count; i++ )
        {
            int const j = (i + 1) % count;
            int y0 = ys[i];
            int y1 = ys[j];
            int x0 = xs[i];
            int x1 = xs[j];
            long long x;

            if( y0 == y1 )
                continue; /* Horizontal edges contribute no crossing. */
            if( y0 > y1 )
            {
                int const ty = y0;
                int const tx = x0;
                y0 = y1;
                x0 = x1;
                y1 = ty;
                x1 = tx;
            }
            /* Half-open in y: a vertex shared by two edges is counted once, so
             * the two edges meeting at it do not both push the span outward. */
            if( y < y0 || y >= y1 )
                continue;

            x = (long long)x0 +
                (long long)(x1 - x0) * (long long)(y - y0) / (long long)(y1 - y0);

            if( !have )
            {
                left = x;
                right = x;
                have = 1;
            }
            else
            {
                if( x < left )
                    left = x;
                if( x > right )
                    right = x;
            }
        }

        if( !have )
            continue;

        if( left < clip_x )
            left = clip_x;
        if( right > clip_x + clip_w - 1 )
            right = clip_x + clip_w - 1;
        if( right < left )
            continue;

        span(user_data, (int)left, y, (int)(right - left + 1));
    }
}
