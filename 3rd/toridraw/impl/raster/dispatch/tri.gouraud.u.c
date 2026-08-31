#include "graphics/tori_compat.h"

#ifndef TORIDRAW_TRIANGLE_GOURAUD_U_C
#define TORIDRAW_TRIANGLE_GOURAUD_U_C

#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>

// clang-format off
#include "impl/raster/span/span.gouraudhsllightness.alpha.dispatch.u.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.sort.s1.scalar.u.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s1.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "graphics/raster/gouraudhsllightness/gouraud_tri_asm.h"
#include "graphics/raster/face_census.h"
// clang-format on

static inline void
ToriDraw_TriangleGouraudImpl(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
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
    int alpha,
    bool scanline)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_GOURAUD,
        x1, y1,
        x2, y2,
        x3, y3,
        screen_width, screen_height);

    if( scanline )
    {
        if( alpha == 0xFF )
        {
            raster_gouraudhsllightness_screen_opaque_bary_scanline_s4(
                pixel_buffer,
                stride,
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
            raster_gouraudhsllightness_screen_alpha_bary_scanline_s4(
                pixel_buffer,
                stride,
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
        return;
    }

    if( alpha == 0xFF )
    {
        // raster_gouraudhsllightness_screen_opaque_edge_sort_s4(
        TORIDRAW_GOURAUD_TRI_OPAQUE_S4(
            pixel_buffer,
            stride,
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
        raster_gouraudhsllightness_screen_alpha_bary_branching_s4(
            pixel_buffer,
            stride,
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

#define TORIDRAW_TRIANGLE_GOURAUD_PARAMETERS                                                     \
    toripixel_t* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int x1,  \
        int x2, int x3, int y1, int y2, int y3, int color_a, int color_b, int color_c, int alpha
#define TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS                                                       \
    pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color_a, color_b,  \
        color_c, alpha

static inline void
ToriDraw_TriangleGouraudBranching(TORIDRAW_TRIANGLE_GOURAUD_PARAMETERS)
{
    ToriDraw_TriangleGouraudImpl(TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleGouraudScanline(TORIDRAW_TRIANGLE_GOURAUD_PARAMETERS)
{
    ToriDraw_TriangleGouraudImpl(TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleGouraud(TORIDRAW_TRIANGLE_GOURAUD_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleGouraudScanline(TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS);
        return;
    }

    ToriDraw_TriangleGouraudBranching(TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_GOURAUD_ARGUMENTS
#undef TORIDRAW_TRIANGLE_GOURAUD_PARAMETERS

static inline void
ToriDraw_TriangleGouraudS1Impl(
    toripixel_t* RESTRICT pixel_buffer,
    int stride,
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
    int alpha,
    bool scanline)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_GOURAUD,
        x1, y1,
        x2, y2,
        x3, y3,
        screen_width, screen_height);

    if( scanline )
    {
        ToriDraw_TriangleGouraudScanline(
            pixel_buffer,
            stride,
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
        return;
    }

    if( alpha == 0xFF )
    {
        raster_gouraudhsllightness_screen_opaque_bary_sort_s1(
            pixel_buffer,
            stride,
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
        raster_gouraudhsllightness_screen_alpha_bary_branching_s1(
            pixel_buffer,
            stride,
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

#define TORIDRAW_TRIANGLE_GOURAUD_S1_PARAMETERS                                                  \
    toripixel_t* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int x1,  \
        int x2, int x3, int y1, int y2, int y3, int color_a, int color_b, int color_c, int alpha
#define TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS                                                    \
    pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color_a, color_b,  \
        color_c, alpha

static inline void
ToriDraw_TriangleGouraudS1Branching(TORIDRAW_TRIANGLE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleGouraudS1Impl(TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleGouraudS1Scanline(TORIDRAW_TRIANGLE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleGouraudS1Impl(TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleGouraudS1(TORIDRAW_TRIANGLE_GOURAUD_S1_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleGouraudS1Scanline(TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS);
        return;
    }

    ToriDraw_TriangleGouraudS1Branching(TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_GOURAUD_S1_ARGUMENTS
#undef TORIDRAW_TRIANGLE_GOURAUD_S1_PARAMETERS

/**
 * This requires vertices to be wound counterclockwise.
 */
static inline void
ToriDraw_TriangleFaceGouraudNearClipImpl(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
    int screen_width,
    int screen_height,
    bool scanline)
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

    if( screen_vertices_x[a] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[a];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        g_toridraw_triangle_clip_color[clipped_count] = colors_a[face];
        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_c[face], color_a);
                clipped_count++;
            }
        }

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_b[face], color_a);
                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[b] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[b];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        g_toridraw_triangle_clip_color[clipped_count] = colors_b[face];
        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_a[face], color_b);
                clipped_count++;
            }
        }

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_c[face], color_b);
                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[c] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[c];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[c];
        g_toridraw_triangle_clip_color[clipped_count] = colors_c[face];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_b[face], color_c);
                clipped_count++;
            }
        }

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_a[face], color_c);
                clipped_count++;
            }
        }
    }
    if( !ToriDraw_TriangleClipFrontFacing(clipped_count) )
        return;

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    xa = g_toridraw_triangle_clip_x[0];
    ya = g_toridraw_triangle_clip_y[0];
    color_a = g_toridraw_triangle_clip_color[0];
    xb = g_toridraw_triangle_clip_x[1];
    yb = g_toridraw_triangle_clip_y[1];
    color_b = g_toridraw_triangle_clip_color[1];
    xc = g_toridraw_triangle_clip_x[2];
    yc = g_toridraw_triangle_clip_y[2];
    color_c = g_toridraw_triangle_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;
    ToriDraw_TriangleGouraudImpl(
            pixel_buffer,
            stride,
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
            alpha,
            scanline);

    if( clipped_count != 4 )
        return;

    xb = g_toridraw_triangle_clip_x[3];
    yb = g_toridraw_triangle_clip_y[3];
    color_b = g_toridraw_triangle_clip_color[3];

    xb += offset_x;
    yb += offset_y;
    ToriDraw_TriangleGouraudImpl(
            pixel_buffer,
            stride,
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
            alpha,
            scanline);
}

static inline void
raster_face_gouraud_near_clipf(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
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

    if( screen_vertices_x[a] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[a];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        g_toridraw_triangle_clip_color[clipped_count] = colors_a[face];

        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, zc, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_c[face], color_a);

                clipped_count++;
            }
        }

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, zb, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_b[face], color_a);

                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[b] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[b];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        g_toridraw_triangle_clip_color[clipped_count] = colors_b[face];

        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, za, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_a[face], color_b);

                clipped_count++;
            }
        }

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, zc, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_c[face], color_b);

                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[c] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[c];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[c];
        g_toridraw_triangle_clip_color[clipped_count] = colors_c[face];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, zb, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_b[face], color_c);

                clipped_count++;
            }
        }

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopef(near_plane_z, za, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjectf(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, colors_a[face], color_c);

                clipped_count++;
            }
        }
    }
    if( !ToriDraw_TriangleClipFrontFacing(clipped_count) )
        return;

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    xa = g_toridraw_triangle_clip_x[0];
    ya = g_toridraw_triangle_clip_y[0];
    color_a = g_toridraw_triangle_clip_color[0];
    xb = g_toridraw_triangle_clip_x[1];
    yb = g_toridraw_triangle_clip_y[1];
    color_b = g_toridraw_triangle_clip_color[1];
    xc = g_toridraw_triangle_clip_x[2];
    yc = g_toridraw_triangle_clip_y[2];
    color_c = g_toridraw_triangle_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;
    ToriDraw_TriangleGouraud(
            pixel_buffer,
            stride,
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
    //         int x = g_toridraw_triangle_clip_x[j];

    //         x += offset_x;

    //         if( x > 0 && x < screen_width )
    //         {
    //             pixel_buffer[i * screen_width + ((int)x)] = colors[j];
    //         }
    //     }
    // }

    xb = g_toridraw_triangle_clip_x[3];
    yb = g_toridraw_triangle_clip_y[3];
    color_b = g_toridraw_triangle_clip_color[3];

    xb += offset_x;
    yb += offset_y;
    ToriDraw_TriangleGouraud(
            pixel_buffer,
            stride,
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
ToriDraw_TriangleFaceGouraudImpl(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
    int screen_width,
    int screen_height,
    bool allow_near_clip,
    bool near_clipped,
    bool scanline)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    /* Route the face to the near-plane clip builder if any vertex is behind the
     * eye. `near_clipped` answers that for the whole model up front (see
     * ToriDraw_Project); the reference gates the identical test the same way,
     * on `clipped` in Model.render2:1876. It is not just a saving — with the
     * flag clear the projection ran its no-clip kernel, which skips the -5001
     * nudge, so a legitimately projected -5000 can occur and must NOT be read
     * as the sentinel. Everything below this branch, including the NearClip
     * builders' own per-vertex -5000 tests, runs only when the flag is set. */
    if( near_clipped &&
        (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
    {
        if( !allow_near_clip || !orthographic_vertices_x )
            return;
        ToriDraw_TriangleFaceGouraudNearClipImpl(
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
            camera_cot16,
            offset_x,
            offset_y,
            stride,
            screen_width,
            screen_height,
            scanline);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];

    int y2 = screen_vertices_y[face_indices_b[face]];

    int y3 = screen_vertices_y[face_indices_c[face]];

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    int color_a = colors_a[face];
    int color_b = colors_b[face];
    int color_c = colors_c[face];

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);
    ToriDraw_TriangleGouraudImpl(
            pixel_buffer,
            stride,
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
            alpha,
            scanline);
}

#define TORIDRAW_TRIANGLE_FACE_GOURAUD_PARAMETERS                                                \
    toripixel_t* RESTRICT pixel_buffer, int face, faceint_t* RESTRICT face_indices_a,             \
        faceint_t* RESTRICT face_indices_b, faceint_t* RESTRICT face_indices_c,                   \
        int* RESTRICT screen_vertices_x, int* RESTRICT screen_vertices_y,                         \
        int* RESTRICT screen_vertices_z, int* RESTRICT orthographic_vertices_x,                   \
        int* RESTRICT orthographic_vertices_y, int* RESTRICT orthographic_vertices_z,             \
        hsl16_t* RESTRICT colors_a, hsl16_t* RESTRICT colors_b, hsl16_t* RESTRICT colors_c,       \
        alphaint_t* RESTRICT face_alphas_nullable, int near_plane_z, int camera_cot16,            \
        int offset_x, int offset_y, int stride, int screen_width, int screen_height,              \
        bool allow_near_clip, bool near_clipped

#define TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS                                                  \
    pixel_buffer, face, face_indices_a, face_indices_b, face_indices_c, screen_vertices_x,         \
        screen_vertices_y, screen_vertices_z, orthographic_vertices_x, orthographic_vertices_y,   \
        orthographic_vertices_z, colors_a, colors_b, colors_c, face_alphas_nullable, near_plane_z, \
        camera_cot16, offset_x, offset_y, stride, screen_width, screen_height, allow_near_clip,   \
        near_clipped

static inline void
ToriDraw_TriangleFaceGouraudBranching(TORIDRAW_TRIANGLE_FACE_GOURAUD_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudImpl(TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleFaceGouraudScanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudImpl(TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleFaceGouraud(TORIDRAW_TRIANGLE_FACE_GOURAUD_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFaceGouraudScanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS);
        return;
    }

    ToriDraw_TriangleFaceGouraudBranching(TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_FACE_GOURAUD_ARGUMENTS
#undef TORIDRAW_TRIANGLE_FACE_GOURAUD_PARAMETERS

static inline void
ToriDraw_TriangleFaceGouraudNearClipS1Impl(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
    int screen_width,
    int screen_height,
    bool scanline)
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

    if( screen_vertices_x[a] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[a];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        g_toridraw_triangle_clip_color[clipped_count] = colors_a[face];

        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_c[face], color_a);

                clipped_count++;
            }
        }

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, za);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);

                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);

                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_b[face], color_a);

                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[b] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[b];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[b];
        g_toridraw_triangle_clip_color[clipped_count] = colors_b[face];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_a[face], color_b);
                clipped_count++;
            }
        }

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, zb);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_c[face], color_b);
                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[c] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[c];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[c];
        g_toridraw_triangle_clip_color[clipped_count] = colors_c[face];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_b[face], color_c);
                clipped_count++;
            }
        }

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zc);

                g_toridraw_triangle_clip_x[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
                g_toridraw_triangle_clip_y[clipped_count] =
                    ToriDraw_TriangleLerpPlaneProjecti(camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
                g_toridraw_triangle_clip_color[clipped_count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, colors_a[face], color_c);
                clipped_count++;
            }
        }
    }
    if( !ToriDraw_TriangleClipFrontFacing(clipped_count) )
        return;

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    xa = g_toridraw_triangle_clip_x[0];
    ya = g_toridraw_triangle_clip_y[0];
    color_a = g_toridraw_triangle_clip_color[0];
    xb = g_toridraw_triangle_clip_x[1];
    yb = g_toridraw_triangle_clip_y[1];
    color_b = g_toridraw_triangle_clip_color[1];
    xc = g_toridraw_triangle_clip_x[2];
    yc = g_toridraw_triangle_clip_y[2];
    color_c = g_toridraw_triangle_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    ToriDraw_TriangleGouraudS1Impl(
        pixel_buffer,
        stride,
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
        alpha,
        scanline);

    if( clipped_count != 4 )
        return;

    xb = g_toridraw_triangle_clip_x[3];
    yb = g_toridraw_triangle_clip_y[3];
    color_b = g_toridraw_triangle_clip_color[3];

    xb += offset_x;
    yb += offset_y;

    ToriDraw_TriangleGouraudS1Impl(
        pixel_buffer,
        stride,
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
        alpha,
        scanline);
}

static inline void
ToriDraw_TriangleFaceGouraudS1Impl(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
    int screen_width,
    int screen_height,
    bool allow_near_clip,
    bool near_clipped,
    bool scanline)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    /* Route the face to the near-plane clip builder if any vertex is behind the
     * eye. `near_clipped` answers that for the whole model up front (see
     * ToriDraw_Project); the reference gates the identical test the same way,
     * on `clipped` in Model.render2:1876. It is not just a saving — with the
     * flag clear the projection ran its no-clip kernel, which skips the -5001
     * nudge, so a legitimately projected -5000 can occur and must NOT be read
     * as the sentinel. Everything below this branch, including the NearClip
     * builders' own per-vertex -5000 tests, runs only when the flag is set. */
    if( near_clipped &&
        (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
    {
        if( !allow_near_clip || !orthographic_vertices_x )
            return;
        ToriDraw_TriangleFaceGouraudNearClipS1Impl(
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
            camera_cot16,
            offset_x,
            offset_y,
            stride,
            screen_width,
            screen_height,
            scanline);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];

    int y2 = screen_vertices_y[face_indices_b[face]];

    int y3 = screen_vertices_y[face_indices_c[face]];

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    int color_a = colors_a[face];
    int color_b = colors_b[face];
    int color_c = colors_c[face];

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    ToriDraw_TriangleGouraudS1Impl(
        pixel_buffer,
        stride,
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
        alpha,
        scanline);
}

#define TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS                                             \
    toripixel_t* RESTRICT pixel_buffer, int face, faceint_t* RESTRICT face_indices_a,             \
        faceint_t* RESTRICT face_indices_b, faceint_t* RESTRICT face_indices_c,                   \
        int* RESTRICT screen_vertices_x, int* RESTRICT screen_vertices_y,                         \
        int* RESTRICT screen_vertices_z, int* RESTRICT orthographic_vertices_x,                   \
        int* RESTRICT orthographic_vertices_y, int* RESTRICT orthographic_vertices_z,             \
        hsl16_t* RESTRICT colors_a, hsl16_t* RESTRICT colors_b, hsl16_t* RESTRICT colors_c,       \
        alphaint_t* RESTRICT face_alphas_nullable, int near_plane_z, int camera_cot16,            \
        int offset_x, int offset_y, int stride, int screen_width, int screen_height,              \
        bool allow_near_clip, bool near_clipped

#define TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS                                               \
    pixel_buffer, face, face_indices_a, face_indices_b, face_indices_c, screen_vertices_x,         \
        screen_vertices_y, screen_vertices_z, orthographic_vertices_x, orthographic_vertices_y,   \
        orthographic_vertices_z, colors_a, colors_b, colors_c, face_alphas_nullable, near_plane_z, \
        camera_cot16, offset_x, offset_y, stride, screen_width, screen_height, allow_near_clip,   \
        near_clipped

static inline void
ToriDraw_TriangleFaceGouraudS1Branching(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudS1Impl(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleFaceGouraudS1Scanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudS1Impl(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleFaceGouraudS1(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFaceGouraudS1Scanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS);
        return;
    }

    ToriDraw_TriangleFaceGouraudS1Branching(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS);
}

static inline void
ToriDraw_TriangleFaceGouraudSmoothBranching(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudS1Branching(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS);
}

static inline void
ToriDraw_TriangleFaceGouraudSmoothScanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS)
{
    ToriDraw_TriangleFaceGouraudS1Scanline(TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_ARGUMENTS
#undef TORIDRAW_TRIANGLE_FACE_GOURAUD_S1_PARAMETERS

static inline void
ToriDraw_TriangleFaceGouraudSmooth(
    toripixel_t* RESTRICT pixel_buffer,
    int face,
    faceint_t* RESTRICT face_indices_a,
    faceint_t* RESTRICT face_indices_b,
    faceint_t* RESTRICT face_indices_c,
    int* RESTRICT screen_vertices_x,
    int* RESTRICT screen_vertices_y,
    int* RESTRICT screen_vertices_z,
    int* RESTRICT orthographic_vertices_x,
    int* RESTRICT orthographic_vertices_y,
    int* RESTRICT orthographic_vertices_z,
    hsl16_t* RESTRICT colors_a,
    hsl16_t* RESTRICT colors_b,
    hsl16_t* RESTRICT colors_c,
    alphaint_t* RESTRICT face_alphas_nullable,
    int near_plane_z,
    int camera_cot16,
    int offset_x,
    int offset_y,
    int stride,
    int screen_width,
    int screen_height,
    bool allow_near_clip,
    bool near_clipped)
{
    ToriDraw_TriangleFaceGouraudS1(
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
        camera_cot16,
        offset_x,
        offset_y,
        stride,
        screen_width,
        screen_height,
        allow_near_clip,
        near_clipped);
}

#endif
