#include "render/torirs_silhouette.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void
ToriRS_SilhouetteInit(struct ToriRS_Silhouette* mask,
                     int x, int y, int width, int height)
{
    size_t count;
    assert(mask);
    assert(width > 0);
    assert(height > 0);
    assert((int64_t)x + width <= INT_MAX);
    assert((int64_t)y + height <= INT_MAX);
    assert((size_t)width <= SIZE_MAX / (size_t)height);
    count = (size_t)width * (size_t)height;
    assert(count <= SIZE_MAX / sizeof(float));
    assert(count <= SIZE_MAX / sizeof(int));
    *mask = (struct ToriRS_Silhouette){.x=x, .y=y, .width=width, .height=height};
    mask->depth = calloc(count, sizeof(*mask->depth));
    assert(mask->depth);
    mask->order = calloc(count, sizeof(*mask->order));
    assert(mask->order);
    mask->coverage = calloc(count, sizeof(*mask->coverage));
    assert(mask->coverage);
    mask->alpha = calloc(count, sizeof(*mask->alpha));
    assert(mask->alpha);
}

void
ToriRS_SilhouetteFree(struct ToriRS_Silhouette* mask)
{
    if( !mask ) return;
    free(mask->depth);
    free(mask->order);
    free(mask->coverage);
    free(mask->alpha);
    memset(mask, 0, sizeof(*mask));
}

static double
silhouette_edge(struct ToriRS_SilhouetteVertex a,
                struct ToriRS_SilhouetteVertex b, double x, double y)
{
    return ((double)b.x - a.x) * (y - a.y) - ((double)b.y - a.y) * (x - a.x);
}

static bool
silhouette_top_left(struct ToriRS_SilhouetteVertex a,
                    struct ToriRS_SilhouetteVertex b)
{
    return b.y < a.y || (b.y == a.y && b.x > a.x);
}

static int
silhouette_wrap(float coordinate, int size)
{
    float const fraction = coordinate - floorf(coordinate);
    int result = (int)(fraction * size);
    if( result >= size ) result = size - 1;
    return result;
}

void
ToriRS_SilhouetteTriangle(
    struct ToriRS_Silhouette* mask,
    const struct ToriRS_SilhouetteVertex vertices[3],
    int opacity, bool textured, const struct ToriRS_SilhouetteTexture* texture,
    bool subject, int draw_order, bool depth_test)
{
    struct ToriRS_SilhouetteVertex v[3];
    double area;
    int min_x, min_y, max_x, max_y;
    bool top[3];

    assert(mask);
    assert(mask->depth);
    assert(mask->order);
    assert(mask->coverage);
    assert(mask->alpha);
    assert(vertices);
    assert(opacity >= 0);
    assert(opacity <= 255);
    if( opacity == 0 ) return;
    if( textured )
    {
        assert(texture);
        assert(texture->pixels);
        assert(texture->width > 0);
        assert(texture->height > 0);
    }
    memcpy(v, vertices, sizeof(v));
    for( int i = 0; i < 3; ++i )
    {
        assert(isfinite(v[i].x));
        assert(isfinite(v[i].y));
        assert(isfinite(v[i].depth));
        assert(isfinite(v[i].u));
        assert(isfinite(v[i].v));
        assert(isfinite(v[i].q));
    }
    area = silhouette_edge(v[0], v[1], v[2].x, v[2].y);
    if( area == 0 ) return;
    if( area < 0 )
    {
        struct ToriRS_SilhouetteVertex const swap = v[1];
        v[1] = v[2]; v[2] = swap; area = -area;
    }
    /* Clamp in floating point before converting: a near-clipped face may
     * project well outside the integer range even with an ordinary canvas. */
    {
        double const left = mask->x, top_y = mask->y;
        double const right = (double)mask->x + mask->width;
        double const bottom = (double)mask->y + mask->height;
        min_x = (int)fmin(right, fmax(left, floor(fmin(v[0].x, fmin(v[1].x, v[2].x)))));
        min_y = (int)fmin(bottom, fmax(top_y, floor(fmin(v[0].y, fmin(v[1].y, v[2].y)))));
        max_x = (int)fmax(left, fmin(right, ceil(fmax(v[0].x, fmax(v[1].x, v[2].x)))));
        max_y = (int)fmax(top_y, fmin(bottom, ceil(fmax(v[0].y, fmax(v[1].y, v[2].y)))));
    }
    if( max_x <= min_x || max_y <= min_y ) return;
    top[0] = silhouette_top_left(v[1], v[2]);
    top[1] = silhouette_top_left(v[2], v[0]);
    top[2] = silhouette_top_left(v[0], v[1]);
    for( int y = min_y; y < max_y; ++y )
    {
        for( int x = min_x; x < max_x; ++x )
        {
            double e[3] = {
                silhouette_edge(v[1], v[2], x + 0.5, y + 0.5),
                silhouette_edge(v[2], v[0], x + 0.5, y + 0.5),
                silhouette_edge(v[0], v[1], x + 0.5, y + 0.5)};
            float depth;
            int alpha = opacity;
            size_t index;
            if( e[0] < 0 || (e[0] == 0 && !top[0]) ||
                e[1] < 0 || (e[1] == 0 && !top[1]) ||
                e[2] < 0 || (e[2] == 0 && !top[2]) ) continue;
            for( int i = 0; i < 3; ++i ) e[i] /= area;
            depth = (float)(e[0]*v[0].depth + e[1]*v[1].depth + e[2]*v[2].depth);
            index = (size_t)(y - mask->y) * mask->width + (x - mask->x);
            if( !subject )
            {
                if( !mask->alpha[index] ) continue;
                if( depth_test )
                {
                    /* Equal-depth/coplanar geometry does not cover the mark.
                     * Relative tolerance only absorbs projection rounding. */
                    float const tolerance = fabsf(mask->depth[index]) * (8.0f * FLT_EPSILON) + FLT_MIN;
                    if( depth <= mask->depth[index] + tolerance ) continue;
                }
                else if( draw_order <= mask->order[index] ) continue;
            }
            if( textured )
            {
                double const q = e[0]*v[0].q + e[1]*v[1].q + e[2]*v[2].q;
                float u, tv;
                uint32_t texel;
                if( q <= 0 ) continue;
                u = (float)((e[0]*v[0].u*v[0].q + e[1]*v[1].u*v[1].q + e[2]*v[2].u*v[2].q)/q);
                tv = (float)((e[0]*v[0].v*v[0].q + e[1]*v[1].v*v[1].q + e[2]*v[2].v*v[2].q)/q);
                texel = texture->pixels[silhouette_wrap(tv, texture->height)*texture->width +
                                        silhouette_wrap(u, texture->width)];
                if( texture->color_key && texel == 0 ) continue;
                if( texture->texel_alpha ) alpha = (alpha * (int)(texel >> 24) + 127) / 255;
            }
            if( subject )
            {
                if( alpha == 0 ) continue;
                if( !mask->coverage[index] || depth > mask->depth[index] )
                    mask->depth[index] = depth;
                mask->order[index] = draw_order;
                mask->coverage[index] = (uint8_t)(alpha +
                    ((int)mask->coverage[index] * (255 - alpha) + 127) / 255);
            }
            else
                mask->alpha[index] = (uint8_t)(((int)mask->alpha[index] * (255 - alpha) + 127) / 255);
        }
    }
}

/* Four-connected distance gives a one-pixel border at width one and a
 * two-pixel border at width two, including their diagonal joins. Propagate
 * nearest subject depth/order with the distance, in two linear sweeps. */
static void
silhouette_distance_step(struct ToriRS_Silhouette* mask, uint16_t* distance,
                         size_t destination, size_t source)
{
    if( distance[source] + 1 < distance[destination] )
    {
        distance[destination] = (uint16_t)(distance[source] + 1);
        mask->depth[destination] = mask->depth[source];
        mask->order[destination] = mask->order[source];
        mask->alpha[destination] = mask->alpha[source];
    }
}

void
ToriRS_SilhouetteStyle(struct ToriRS_Silhouette* mask, int fill_alpha, int outline_width)
{
    size_t count;
    uint16_t* distance;
    assert(mask);
    assert(mask->coverage);
    assert(mask->alpha);
    assert(fill_alpha >= 0);
    assert(fill_alpha <= 255);
    assert(outline_width >= 0);
    assert(outline_width <= 255);
    count = (size_t)mask->width * mask->height;
    if( outline_width == 0 )
    {
        for( size_t i = 0; i < count; ++i )
            mask->alpha[i] = (uint8_t)((fill_alpha * (int)mask->coverage[i] + 127) / 255);
        return;
    }
    distance = malloc(count * sizeof(*distance));
    assert(distance);
    for( size_t i = 0; i < count; ++i )
    {
        distance[i] = mask->coverage[i] ? 0 : (uint16_t)(outline_width + 1);
        mask->alpha[i] = mask->coverage[i];
    }
    for( int y = 0; y < mask->height; ++y )
        for( int x = 0; x < mask->width; ++x )
        {
            size_t const i = (size_t)y * mask->width + x;
            if( x > 0 ) silhouette_distance_step(mask, distance, i, i - 1);
            if( y > 0 ) silhouette_distance_step(mask, distance, i, i - mask->width);
        }
    for( int y = mask->height - 1; y >= 0; --y )
        for( int x = mask->width - 1; x >= 0; --x )
        {
            size_t const i = (size_t)y * mask->width + x;
            if( x + 1 < mask->width ) silhouette_distance_step(mask, distance, i, i + 1);
            if( y + 1 < mask->height ) silhouette_distance_step(mask, distance, i, i + mask->width);
        }
    for( size_t i = 0; i < count; ++i )
    {
        if( mask->coverage[i] )
            mask->alpha[i] = (uint8_t)((fill_alpha * (int)mask->coverage[i] + 127) / 255);
        else if( distance[i] > outline_width )
            mask->alpha[i] = 0;
    }
    free(distance);
}
