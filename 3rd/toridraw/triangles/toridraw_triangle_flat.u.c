#include "graphics/tori_compat.h"

#ifndef TORIDRAW_TRIANGLE_FLAT_U_C
#define TORIDRAW_TRIANGLE_FLAT_U_C

#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"
#include "../toridraw_types.h"

#include <assert.h>
#include <stdbool.h>

// clang-format off
#include "graphics/raster/flat/flat.screen.opaque.sort.s4.u.c"
#include "graphics/raster/flat/flat.screen.alpha.sort.s4.u.c"
#include "graphics/raster/flat/flat.screen.opaque.branching.s4.c"
#include "graphics/raster/flat/flat.screen.alpha.branching.s4.c"
#include "graphics/raster/face_census.h"
// clang-format on

static inline void
ToriDraw_TriangleFlatScanline(
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
    int color,
    int alpha)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_FLAT,
        x1, y1,
        x2, y2,
        x3, y3,
        screen_width * screen_height);

    if( alpha == 0xFF )
    {
        raster_flat_screen_opaque_scanline_s8(
            pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color);
    }
    else
    {
        raster_flat_screen_alpha_scanline_s8(
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
            color,
            alpha);
    }
}

static inline void
ToriDraw_TriangleFlatBranching(
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
    int color,
    int alpha)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_FLAT,
        x1, y1,
        x2, y2,
        x3, y3,
        screen_width * screen_height);

    if( alpha == 0xFF )
    {
        raster_flat_screen_opaque_branching_s4(
            pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color);
    }
    else
    {
        raster_flat_screen_alpha_branching_s4(
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
            color,
            alpha);
    }
}

static inline void
ToriDraw_TriangleFlat(
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
    int color,
    int alpha)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFlatScanline(
            pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
        return;
    }

    ToriDraw_TriangleFlatBranching(
        pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
}

/**
 * This requires vertices to be wound counterclockwise.
 */
static inline void
ToriDraw_TriangleFaceFlatNearClipImpl(
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
    hsl16_t* RESTRICT colors,
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
    int color = colors[face];
    int lerp_slope;

    if( screen_vertices_x[a] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[a];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, za);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);
                clipped_count++;
            }
        }

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - za >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, za);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);
                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[b] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[b];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zb);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);
                clipped_count++;
            }
        }

        if( screen_vertices_x[c] != -5000 )
        {
            if( zc - zb >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zc, zb);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);
                clipped_count++;
            }
        }
    }
    if( screen_vertices_x[c] != -5000 )
    {
        g_toridraw_triangle_clip_x[clipped_count] = screen_vertices_x[c];
        g_toridraw_triangle_clip_y[clipped_count] = screen_vertices_y[c];
        assert(g_toridraw_triangle_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];

        if( screen_vertices_x[b] != -5000 )
        {
            if( zb - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, zb, zc);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);
                clipped_count++;
            }
        }

        if( screen_vertices_x[a] != -5000 )
        {
            if( za - zc >= 0 )
            {
                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, za, zc);

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
                clipped_count++;
            }
        }
    }
    if( !ToriDraw_TriangleClipFrontFacing(clipped_count) )
        return;

    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    xa = g_toridraw_triangle_clip_x[0];
    ya = g_toridraw_triangle_clip_y[0];
    xb = g_toridraw_triangle_clip_x[1];
    yb = g_toridraw_triangle_clip_y[1];
    xc = g_toridraw_triangle_clip_x[2];
    yc = g_toridraw_triangle_clip_y[2];

    assert(color >= 0 && color < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    if( scanline )
        ToriDraw_TriangleFlatScanline(
            pixel_buffer, stride, screen_width, screen_height, xa, xb, xc, ya, yb, yc, color, alpha);
    else
        ToriDraw_TriangleFlatBranching(
            pixel_buffer, stride, screen_width, screen_height, xa, xb, xc, ya, yb, yc, color, alpha);

    if( clipped_count != 4 )
        return;

    xb = g_toridraw_triangle_clip_x[3];
    yb = g_toridraw_triangle_clip_y[3];

    xb += offset_x;
    yb += offset_y;

    if( scanline )
        ToriDraw_TriangleFlatScanline(
            pixel_buffer, stride, screen_width, screen_height, xa, xc, xb, ya, yc, yb, color, alpha);
    else
        ToriDraw_TriangleFlatBranching(
            pixel_buffer, stride, screen_width, screen_height, xa, xc, xb, ya, yc, yb, color, alpha);
}

static inline void
ToriDraw_TriangleFaceFlatImpl(
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
    hsl16_t* RESTRICT colors,
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

    /* See the near_clipped gate note in toridraw_triangle_gouraud.u.c. */
    if( near_clipped &&
        (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
    {
        if( !allow_near_clip || !orthographic_vertices_x )
            return;
        ToriDraw_TriangleFaceFlatNearClipImpl(
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
            colors,
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

    int color = colors[face];
    int alpha = ToriDraw_TriangleFaceAlpha(face_alphas_nullable, face);

    assert(color >= 0 && color < 65536);

    if( scanline )
        ToriDraw_TriangleFlatScanline(
            pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
    else
        ToriDraw_TriangleFlatBranching(
            pixel_buffer, stride, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
}

#define TORIDRAW_TRIANGLE_FACE_FLAT_PARAMETERS                                                   \
    toripixel_t* RESTRICT pixel_buffer, int face, faceint_t* RESTRICT face_indices_a,             \
        faceint_t* RESTRICT face_indices_b, faceint_t* RESTRICT face_indices_c,                   \
        int* RESTRICT screen_vertices_x, int* RESTRICT screen_vertices_y,                         \
        int* RESTRICT screen_vertices_z, int* RESTRICT orthographic_vertices_x,                   \
        int* RESTRICT orthographic_vertices_y, int* RESTRICT orthographic_vertices_z,             \
        hsl16_t* RESTRICT colors, alphaint_t* RESTRICT face_alphas_nullable, int near_plane_z,     \
        int camera_cot16, int offset_x, int offset_y, int stride, int screen_width,                \
        int screen_height, bool allow_near_clip, bool near_clipped

#define TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS                                                     \
    pixel_buffer, face, face_indices_a, face_indices_b, face_indices_c, screen_vertices_x,         \
        screen_vertices_y, screen_vertices_z, orthographic_vertices_x, orthographic_vertices_y,   \
        orthographic_vertices_z, colors, face_alphas_nullable, near_plane_z, camera_cot16,        \
        offset_x, offset_y, stride, screen_width, screen_height, allow_near_clip, near_clipped

static inline void
ToriDraw_TriangleFaceFlatBranching(TORIDRAW_TRIANGLE_FACE_FLAT_PARAMETERS)
{
    ToriDraw_TriangleFaceFlatImpl(TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleFaceFlatScanline(TORIDRAW_TRIANGLE_FACE_FLAT_PARAMETERS)
{
    ToriDraw_TriangleFaceFlatImpl(TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleFaceFlat(TORIDRAW_TRIANGLE_FACE_FLAT_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFaceFlatScanline(TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS);
        return;
    }

    ToriDraw_TriangleFaceFlatBranching(TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_FACE_FLAT_ARGUMENTS
#undef TORIDRAW_TRIANGLE_FACE_FLAT_PARAMETERS

#endif
