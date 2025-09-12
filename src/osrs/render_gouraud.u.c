#include "gouraud.h"
#include "shared_tables.h"

#include <assert.h>

static long long g_clip_x[10] = { 0 };
static long long g_clip_y[10] = { 0 };
static long long g_clip_color[10] = { 0 };

static const int reciprocol_shift = 16;

static inline void
raster_gouraud(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int color_a,
    int color_b,
    int color_c,
    int alpha)
{
    if( alpha == 0xFF )
    {
        raster_gouraud_s4(
            pixel_buffer,
            screen_width,
            screen_height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            color_a,
            color_b,
            color_c);
    }
    else
    {
        raster_gouraud_blend_s4(
            pixel_buffer,
            screen_width,
            screen_height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            color_a,
            color_b,
            color_c,
            alpha);
    }
}

static inline int
slopei(int near_plane_z, int za, int zb)
{
    assert(za - zb >= 0);
    return (za - near_plane_z) * g_reciprocal16[za - zb];
}

static inline int
lerp_planei(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = pa + (((pb - pa) * lerp_slope) >> reciprocol_shift);

    return lerp_p;
}

static inline int
lerp_plane_projecti(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = lerp_planei(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

/**
 * This requires vertices to be wound counterclockwise.
 */
static inline void
raster_face_gouraud_near_clip(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int clipped_count = 0;
    int a = face_indices_a[face];
    int b = face_indices_b[face];
    int c = face_indices_c[face];

    int za = orthographic_vertices_z[a];
    int zb = orthographic_vertices_z[b];
    int zc = orthographic_vertices_z[c];

    int xa;
    int xb;
    int xc;
    int ya;
    int yb;
    int yc;
    int color_a;
    int color_b;
    int color_c;
    int lerp_slope;

    if( za >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[a];
        g_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_a[face];
        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( zc >= near_plane_z )
        {
            assert(zc - za >= 0);
            lerp_slope = slopei(near_plane_z, zc, za);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_c[face], color_a);
            clipped_count++;
        }

        if( zb >= near_plane_z )
        {
            assert(zb - za >= 0);
            lerp_slope = slopei(near_plane_z, zb, za);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_b[face], color_a);
            clipped_count++;
        }
    }
    if( zb >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[b];
        g_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_b[face];
        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( za >= near_plane_z )
        {
            lerp_slope = slopei(near_plane_z, za, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_a[face], color_b);
            clipped_count++;
        }

        if( zc >= near_plane_z )
        {
            assert(zc - zb >= 0);
            lerp_slope = slopei(near_plane_z, zc, zb);

            // assert(0xFFFF0000 & ((zb - near_plane_z) * (orthographic_vertices_x[c] - xb)) == 0);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_c[face], color_b);
            clipped_count++;
        }
    }
    if( zc >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[c];
        g_clip_y[clipped_count] = screen_vertices_y[c];
        g_clip_color[clipped_count] = colors_c[face];
        assert(g_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( zb >= near_plane_z )
        {
            assert(zb - zc >= 0);
            lerp_slope = slopei(near_plane_z, zb, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_b[face], color_c);
            clipped_count++;
        }

        if( za >= near_plane_z )
        {
            assert(za - zc >= 0);
            lerp_slope = slopei(near_plane_z, za, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_a[face], color_c);
            clipped_count++;
        }
    }
    if( clipped_count < 3 )
        return;

    int alpha = face_alphas_nullable ? face_alphas_nullable[face] : 0xFF;

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    color_a = g_clip_color[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    color_b = g_clip_color[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];
    color_c = g_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        color_a,
        color_b,
        color_c,
        alpha);

    if( clipped_count != 4 )
        return;

    xb = g_clip_x[3];
    yb = g_clip_y[3];
    color_b = g_clip_color[3];

    xb += offset_x;
    yb += offset_y;

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xc,
        xb,
        ya,
        yc,
        yb,
        color_a,
        color_c,
        color_b,
        alpha);
}

static inline void
raster_face_gouraud(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
    {
        raster_face_gouraud_near_clip(
            pixel_buffer,
            face,
            face_indices_a,
            face_indices_b,
            face_indices_c,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            colors_a,
            colors_b,
            colors_c,
            face_alphas_nullable,
            near_plane_z,
            offset_x,
            offset_y,
            screen_width,
            screen_height);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];
    int z1 = screen_vertices_z[face_indices_a[face]];

    int y2 = screen_vertices_y[face_indices_b[face]];
    int z2 = screen_vertices_z[face_indices_b[face]];

    int y3 = screen_vertices_y[face_indices_c[face]];
    int z3 = screen_vertices_z[face_indices_c[face]];

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    int color_a = colors_a[face];
    int color_b = colors_b[face];
    int color_c = colors_c[face];
    int alpha = face_alphas_nullable ? face_alphas_nullable[face] : 0xFF;

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        color_a,
        color_b,
        color_c,
        alpha);
}

static inline float
slopef(float near_plane_z, float za, float zb)
{
    return (za - near_plane_z) / (za - zb);
}

static inline float
lerp_planef(float near_plane_z, float lerp_slope, float pa, float pb)
{
    return pa + (((pb - pa) * lerp_slope));
}

static inline float
lerp_plane_projectf(float near_plane_z, float lerp_slope, float pa, float pb)
{
    float lerp_p = lerp_planef(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

static inline void
raster_face_gouraud_near_clipf(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int clipped_count = 0;
    int a = face_indices_a[face];
    int b = face_indices_b[face];
    int c = face_indices_c[face];

    float za = orthographic_vertices_z[a];
    float zb = orthographic_vertices_z[b];
    float zc = orthographic_vertices_z[c];

    float xa;
    float xb;
    float xc;
    float ya;
    float yb;
    float yc;
    float color_a;
    float color_b;
    float color_c;
    float lerp_slope;

    if( za >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[a];
        g_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_a[face];

        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( zc >= near_plane_z )
        {
            assert(zc - za >= 0);
            lerp_slope = slopef(near_plane_z, zc, za);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_c[face], color_a);

            clipped_count++;
        }

        if( zb >= near_plane_z )
        {
            assert(zb - za >= 0);
            lerp_slope = slopef(near_plane_z, zb, za);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_b[face], color_a);

            clipped_count++;
        }
    }
    if( zb >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[b];
        g_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_b[face];

        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( za >= near_plane_z )
        {
            lerp_slope = slopef(near_plane_z, za, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_a[face], color_b);

            clipped_count++;
        }

        if( zc >= near_plane_z )
        {
            assert(zc - zb >= 0);
            lerp_slope = slopef(near_plane_z, zc, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_c[face], color_b);

            clipped_count++;
        }
    }
    if( zc >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[c];
        g_clip_y[clipped_count] = screen_vertices_y[c];
        g_clip_color[clipped_count] = colors_c[face];
        assert(g_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( zb >= near_plane_z )
        {
            assert(zb - zc >= 0);
            lerp_slope = slopef(near_plane_z, zb, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_b[face], color_c);

            clipped_count++;
        }

        if( za >= near_plane_z )
        {
            assert(za - zc >= 0);
            lerp_slope = slopef(near_plane_z, za, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_a[face], color_c);

            clipped_count++;
        }
    }
    if( clipped_count < 3 )
        return;

    int alpha = face_alphas_nullable ? face_alphas_nullable[face] : 0xFF;

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    color_a = g_clip_color[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    color_b = g_clip_color[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];
    color_c = g_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        color_a,
        color_b,
        color_c,
        alpha);

    assert(clipped_count <= 4);
    if( clipped_count != 4 )
        return;

    // static int colors[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };

    // for( int i = 0; i < screen_height; i++ )
    // {
    //     for( int j = 0; j < 4; j++ )
    //     {
    //         int x = g_clip_x[j];

    //         x += offset_x;

    //         if( x > 0 && x < screen_width )
    //         {
    //             pixel_buffer[i * screen_width + ((int)x)] = colors[j];
    //         }
    //     }
    // }

    xb = g_clip_x[3];
    yb = g_clip_y[3];
    color_b = g_clip_color[3];

    xb += offset_x;
    yb += offset_y;

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xc,
        xb,
        ya,
        yc,
        yb,
        color_a,
        color_c,
        color_b,
        alpha);
}