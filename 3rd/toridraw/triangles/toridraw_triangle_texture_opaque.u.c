#include "graphics/tori_compat.h"
#include "toridraw_triangle_clip.u.c"

#ifndef TORIDRAW_TRIANGLE_TEXTURE_OPAQUE_U_C
#define TORIDRAW_TRIANGLE_TEXTURE_OPAQUE_U_C

#include "../toridraw_types.h"
#include "graphics/dash_restrict.h"

#include <assert.h>
#include <stdbool.h>

// clang-format off
#include "graphics/projection.u.c"
#include "graphics/clamp.h"
#include "graphics/shade.h"
#include "graphics/raster/texture/span/tex.span.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.ordered.lerp8.scanline.u.c"
#include "graphics/raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texshadeflat.persp.texopaque.branching.lerp8.u.c"
#include "graphics/raster/texture/tex_tri_asm.h"
#include "graphics/raster/texture/tex_tri_asm_support.u.c"
#include "graphics/raster/face_census.h"
// clang-format on

static inline void
ToriDraw_TriangleTextureBlendOpaqueImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_x0,
    int orthographic_x1,
    int orthographic_x2,
    int orthographic_y0,
    int orthographic_y1,
    int orthographic_y2,
    int orthographic_z0,
    int orthographic_z1,
    int orthographic_z2,
    int shade_a,
    int shade_b,
    int shade_c,
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
    bool scanline)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_TEX_BLEND_OPAQUE,
        screen_x0, screen_y0,
        screen_x1, screen_y1,
        screen_x2, screen_y2,
        screen_width * screen_height);

    (void)near_plane_z;
    (void)offset_x;
    (void)offset_y;

    if( scanline )
    {
        raster_texshadeblend_persp_texopaque_scanline_lerp8(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            camera_cot16,
            screen_x0,
            screen_x1,
            screen_x2,
            screen_y0,
            screen_y1,
            screen_y2,
            orthographic_x0,
            orthographic_x1,
            orthographic_x2,
            orthographic_y0,
            orthographic_y1,
            orthographic_y2,
            orthographic_z0,
            orthographic_z1,
            orthographic_z2,
            shade_a,
            shade_b,
            shade_c,
            0xFF,
            texels,
            texture_size);
        return;
    }

    TORIDRAW_TEX_TRI_PERSP_OPAQUE(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        screen_x0,
        screen_x1,
        screen_x2,
        screen_y0,
        screen_y1,
        screen_y2,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        shade_a,
        shade_b,
        shade_c,
        texels,
        texture_size);
}

static inline void
ToriDraw_TriangleTextureFlatOpaqueImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_x0,
    int orthographic_x1,
    int orthographic_x2,
    int orthographic_y0,
    int orthographic_y1,
    int orthographic_y2,
    int orthographic_z0,
    int orthographic_z1,
    int orthographic_z2,
    int shade,
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
    bool scanline)
{
    TORIDRAW_FACE_CENSUS_RECORD(
        TORIDRAW_FACE_CENSUS_TEX_FLAT_OPAQUE,
        screen_x0, screen_y0,
        screen_x1, screen_y1,
        screen_x2, screen_y2,
        screen_width * screen_height);

    (void)near_plane_z;
    (void)offset_x;
    (void)offset_y;

    if( scanline )
    {
        raster_texshadeflat_persp_texopaque_scanline_lerp8(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            camera_cot16,
            screen_x0,
            screen_x1,
            screen_x2,
            screen_y0,
            screen_y1,
            screen_y2,
            orthographic_x0,
            orthographic_x1,
            orthographic_x2,
            orthographic_y0,
            orthographic_y1,
            orthographic_y2,
            orthographic_z0,
            orthographic_z1,
            orthographic_z2,
            shade,
            shade,
            shade,
            0xFF,
            texels,
            texture_size);
        return;
    }

    raster_texshadeflat_persp_texopaque_branching_lerp8(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        screen_x0,
        screen_x1,
        screen_x2,
        screen_y0,
        screen_y1,
        screen_y2,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        shade,
        texels,
        texture_size);
}

#define TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_PARAMETERS                                        \
    int* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int camera_cot16, \
        int screen_x0, int screen_x1, int screen_x2, int screen_y0, int screen_y1, int screen_y2, \
        int orthographic_x0, int orthographic_x1, int orthographic_x2, int orthographic_y0,       \
        int orthographic_y1, int orthographic_y2, int orthographic_z0, int orthographic_z1,       \
        int orthographic_z2, int shade_a, int shade_b, int shade_c, int* RESTRICT texels,         \
        int texture_size, int near_plane_z, int offset_x, int offset_y
#define TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS                                          \
    pixel_buffer, stride, screen_width, screen_height, camera_cot16, screen_x0, screen_x1,         \
        screen_x2, screen_y0, screen_y1, screen_y2, orthographic_x0, orthographic_x1,             \
        orthographic_x2, orthographic_y0, orthographic_y1, orthographic_y2, orthographic_z0,      \
        orthographic_z1, orthographic_z2, shade_a, shade_b, shade_c, texels, texture_size,        \
        near_plane_z, offset_x, offset_y

static inline void
ToriDraw_TriangleTextureBlendOpaqueBranching(
    TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleTextureBlendOpaqueImpl(
        TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleTextureBlendOpaqueScanline(TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleTextureBlendOpaqueImpl(TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleTextureBlendOpaque(TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleTextureBlendOpaqueScanline(
            TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS);
        return;
    }
    ToriDraw_TriangleTextureBlendOpaqueBranching(
        TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_ARGUMENTS
#undef TORIDRAW_TRIANGLE_TEXTURE_BLEND_OPAQUE_PARAMETERS

#define TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_PARAMETERS                                         \
    int* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int camera_cot16, \
        int screen_x0, int screen_x1, int screen_x2, int screen_y0, int screen_y1, int screen_y2, \
        int orthographic_x0, int orthographic_x1, int orthographic_x2, int orthographic_y0,       \
        int orthographic_y1, int orthographic_y2, int orthographic_z0, int orthographic_z1,       \
        int orthographic_z2, int shade, int* RESTRICT texels, int texture_size, int near_plane_z, \
        int offset_x, int offset_y
#define TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS                                           \
    pixel_buffer, stride, screen_width, screen_height, camera_cot16, screen_x0, screen_x1,         \
        screen_x2, screen_y0, screen_y1, screen_y2, orthographic_x0, orthographic_x1,             \
        orthographic_x2, orthographic_y0, orthographic_y1, orthographic_y2, orthographic_z0,      \
        orthographic_z1, orthographic_z2, shade, texels, texture_size, near_plane_z, offset_x,    \
        offset_y

static inline void
ToriDraw_TriangleTextureFlatOpaqueBranching(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleTextureFlatOpaqueImpl(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleTextureFlatOpaqueScanline(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleTextureFlatOpaqueImpl(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleTextureFlatOpaque(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleTextureFlatOpaqueScanline(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS);
        return;
    }
    ToriDraw_TriangleTextureFlatOpaqueBranching(TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_ARGUMENTS
#undef TORIDRAW_TRIANGLE_TEXTURE_FLAT_OPAQUE_PARAMETERS

static inline void
ToriDraw_TriangleFaceTextureBlendOpaqueNearClipImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
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

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_c[face], color_a);

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

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_b[face], color_a);

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

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);

                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_a[face], color_b);

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

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_c[face], color_b);

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

                g_toridraw_triangle_clip_x[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
                g_toridraw_triangle_clip_y[clipped_count] = ToriDraw_TriangleLerpPlaneProjecti(
                    camera_cot16, near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_b[face], color_c);

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

                g_toridraw_triangle_clip_color[clipped_count] = ToriDraw_TriangleLerpPlanei(
                    near_plane_z, lerp_slope, colors_a[face], color_c);

                clipped_count++;
            }
        }
    }
    if( !ToriDraw_TriangleClipFrontFacing(clipped_count) )
        return;

    int orthographic_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_x2 = orthographic_vertices_x[tn_vertex];

    int orthographic_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_y2 = orthographic_vertices_y[tn_vertex];

    int orthographic_z0 = orthographic_vertices_z[tp_vertex];
    int orthographic_z1 = orthographic_vertices_z[tm_vertex];
    int orthographic_z2 = orthographic_vertices_z[tn_vertex];

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
    ToriDraw_TriangleTextureBlendOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        color_a,
        color_b,
        color_c,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);

    static int colors[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };

    // for( int i = 0; i < screen_height; i++ )
    // {
    //     for( int j = 0; j < clipped_count; j++ )
    //     {
    //         int x = g_toridraw_triangle_clip_x[j];

    //         x += offset_x;

    //         if( x > 0 && x < screen_width )
    //         {
    //             pixel_buffer[i * screen_width + ((int)x)] = colors[j];
    //         }
    //     }
    // }

    assert(clipped_count <= 4);
    if( clipped_count != 4 )
        return;

    xb = g_toridraw_triangle_clip_x[3];
    yb = g_toridraw_triangle_clip_y[3];
    color_b = g_toridraw_triangle_clip_color[3];

    // assert((xb > 0 && xb < screen_width) || (xa > 0 && xa < screen_width));

    xb += offset_x;
    yb += offset_y;
    ToriDraw_TriangleTextureBlendOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        color_a,
        color_b,
        color_c,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);
}

static inline void
ToriDraw_TriangleFaceTextureBlendOpaqueImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
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
        if( !allow_near_clip )
            return;
        ToriDraw_TriangleFaceTextureBlendOpaqueNearClipImpl(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            camera_cot16,
            face,
            tp_vertex,
            tm_vertex,
            tn_vertex,
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
            texels,
            texture_size,
            near_plane_z,
            offset_x,
            offset_y,
            scanline);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];
    int z1 = screen_vertices_z[face_indices_a[face]];
    int y2 = screen_vertices_y[face_indices_b[face]];
    int z2 = screen_vertices_z[face_indices_b[face]];
    int y3 = screen_vertices_y[face_indices_c[face]];
    int z3 = screen_vertices_z[face_indices_c[face]];

    /* Pix3D deob: (tp_vertex,tm_vertex,tn_vertex) are O,B,C for textureTriangle; (x1,x2,x3) are
     * screen positions of face_indices_a/b/c — all six must refer to the same triangle corners. */
    int orthographic_uvorigin_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_uvorigin_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_uvorigin_z0 = orthographic_vertices_z[tp_vertex];
    int orthographic_uend_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_uend_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_uend_z1 = orthographic_vertices_z[tm_vertex];
    int orthographic_vend_x2 = orthographic_vertices_x[tn_vertex];
    int orthographic_vend_y2 = orthographic_vertices_y[tn_vertex];
    int orthographic_vend_z2 = orthographic_vertices_z[tn_vertex];

    int shade_a = colors_a[face];
    int shade_b = colors_b[face];
    int shade_c = colors_c[face];

    assert(shade_a >= 0 && shade_a < 0xFF);
    assert(shade_b >= 0 && shade_b < 0xFF);
    assert(shade_c >= 0 && shade_c < 0xFF);

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    // int orthographic_x0 = orthographic_vertices_x[face_indices_a[face]];
    // int orthographic_x1 = orthographic_vertices_x[face_indices_b[face]];
    // int orthographic_x2 = orthographic_vertices_x[face_indices_c[face]];
    // int orthographic_y0 = orthographic_vertices_y[face_indices_a[face]];
    // int orthographic_y1 = orthographic_vertices_y[face_indices_b[face]];
    // int orthographic_y2 = orthographic_vertices_y[face_indices_c[face]];
    // int orthographic_z0 = orthographic_vertices_z[face_indices_a[face]];
    // int orthographic_z1 = orthographic_vertices_z[face_indices_b[face]];
    // int orthographic_z2 = orthographic_vertices_z[face_indices_c[face]];

    // raster_texture_affine_opaque_blend_branching_lerp8(
    //     pixel_buffer,
    //     stride,
    //     screen_width,
    //     screen_height,
    //     camera_cot16,
    //     x1,
    //     x2,
    //     x3,
    //     y1,
    //     y2,
    //     y3,
    //     orthographic_x0,
    //     orthographic_x1,
    //     orthographic_x2,
    //     orthographic_y0,
    //     orthographic_y1,
    //     orthographic_y2,
    //     orthographic_z0,
    //     orthographic_z1,
    //     orthographic_z2,
    //     orthographic_uvorigin_x0,
    //     orthographic_uend_x1,
    //     orthographic_vend_x2,
    //     orthographic_uvorigin_y0,
    //     orthographic_uend_y1,
    //     orthographic_vend_y2,
    //     orthographic_uvorigin_z0,
    //     orthographic_uend_z1,
    //     orthographic_vend_z2,
    //     shade_a,
    //     shade_b,
    //     shade_c,
    //     texels,
    //     texture_size);

    //     return;

    // assert(shade_a >= 0 && shade_a < 128);
    // assert(shade_b >= 0 && shade_b < 128);
    // assert(shade_c >= 0 && shade_c < 128);

    // raster_texshadeblend_affine_texopaque_branching_lerp8(
    //     pixel_buffer,
    //     stride,
    //     screen_width,
    //     screen_height,
    //     512,
    //     x1,
    //     x2,
    //     x3,
    //     y1,
    //     y2,
    //     y3,
    //     // 80,80,80,
    //     orthographic_uvorigin_x0,
    //     orthographic_uend_x1,
    //     orthographic_vend_x2,
    //     orthographic_uvorigin_y0,
    //     orthographic_uend_y1,
    //     orthographic_vend_y2,
    //     orthographic_uvorigin_z0,
    //     orthographic_uend_z1,
    //     orthographic_vend_z2,

    //     shade_a,
    //     shade_b,
    //     shade_c,
    //     texels,
    //     texture_size);

    // return;
    ToriDraw_TriangleTextureBlendOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        orthographic_uvorigin_x0,
        orthographic_uend_x1,
        orthographic_vend_x2,
        orthographic_uvorigin_y0,
        orthographic_uend_y1,
        orthographic_vend_y2,
        orthographic_uvorigin_z0,
        orthographic_uend_z1,
        orthographic_vend_z2,
        shade_a,
        shade_b,
        shade_c,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);

    return;
}

static inline void
ToriDraw_TriangleFaceTextureFlatOpaqueNearClipImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
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

    int orthographic_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_x2 = orthographic_vertices_x[tn_vertex];

    int orthographic_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_y2 = orthographic_vertices_y[tn_vertex];

    int orthographic_z0 = orthographic_vertices_z[tp_vertex];
    int orthographic_z1 = orthographic_vertices_z[tm_vertex];
    int orthographic_z2 = orthographic_vertices_z[tn_vertex];

    xa = g_toridraw_triangle_clip_x[0];
    ya = g_toridraw_triangle_clip_y[0];
    xb = g_toridraw_triangle_clip_x[1];
    yb = g_toridraw_triangle_clip_y[1];
    xc = g_toridraw_triangle_clip_x[2];
    yc = g_toridraw_triangle_clip_y[2];

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;
    ToriDraw_TriangleTextureFlatOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        color,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);

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

    xb += offset_x;
    yb += offset_y;
    ToriDraw_TriangleTextureFlatOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        color,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);
}

static inline void
ToriDraw_TriangleFaceTextureFlatOpaqueImpl(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_cot16,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* RESTRICT texels,
    int texture_size,
    int near_plane_z,
    int offset_x,
    int offset_y,
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
        if( !allow_near_clip )
            return;
        ToriDraw_TriangleFaceTextureFlatOpaqueNearClipImpl(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            camera_cot16,
            face,
            tp_vertex,
            tm_vertex,
            tn_vertex,
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
            texels,
            texture_size,
            near_plane_z,
            offset_x,
            offset_y,
            scanline);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];
    int z1 = screen_vertices_z[face_indices_a[face]];
    int y2 = screen_vertices_y[face_indices_b[face]];
    int z2 = screen_vertices_z[face_indices_b[face]];
    int y3 = screen_vertices_y[face_indices_c[face]];
    int z3 = screen_vertices_z[face_indices_c[face]];

    int orthographic_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_z0 = orthographic_vertices_z[tp_vertex];

    int orthographic_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_z1 = orthographic_vertices_z[tm_vertex];

    int orthographic_x2 = orthographic_vertices_x[tn_vertex];
    int orthographic_y2 = orthographic_vertices_y[tn_vertex];
    int orthographic_z2 = orthographic_vertices_z[tn_vertex];

    int shade = colors[face];

    assert(shade >= 0 && shade < 0xFF);

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;
    ToriDraw_TriangleTextureFlatOpaqueImpl(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_cot16,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        shade,
        texels,
        texture_size,
        near_plane_z,
        offset_x,
        offset_y,
        scanline);

    return;
}

#define TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_PARAMETERS                                   \
    int* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int camera_cot16, \
        int face, int tp_vertex, int tm_vertex, int tn_vertex, faceint_t* RESTRICT face_indices_a, \
        faceint_t* RESTRICT face_indices_b, faceint_t* RESTRICT face_indices_c,                   \
        int* RESTRICT screen_vertices_x, int* RESTRICT screen_vertices_y,                         \
        int* RESTRICT screen_vertices_z, int* RESTRICT orthographic_vertices_x,                   \
        int* RESTRICT orthographic_vertices_y, int* RESTRICT orthographic_vertices_z,             \
        hsl16_t* RESTRICT colors_a, hsl16_t* RESTRICT colors_b, hsl16_t* RESTRICT colors_c,       \
        int* RESTRICT texels, int texture_size, int near_plane_z, int offset_x, int offset_y,     \
        bool allow_near_clip, bool near_clipped
#define TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS                                     \
    pixel_buffer, stride, screen_width, screen_height, camera_cot16, face, tp_vertex, tm_vertex,   \
        tn_vertex, face_indices_a, face_indices_b, face_indices_c, screen_vertices_x,              \
        screen_vertices_y, screen_vertices_z, orthographic_vertices_x, orthographic_vertices_y,   \
        orthographic_vertices_z, colors_a, colors_b, colors_c, texels, texture_size, near_plane_z, \
        offset_x, offset_y, allow_near_clip, near_clipped

static inline void
ToriDraw_TriangleFaceTextureBlendOpaqueBranching(
    TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleFaceTextureBlendOpaqueImpl(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleFaceTextureBlendOpaqueScanline(
    TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleFaceTextureBlendOpaqueImpl(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleFaceTextureBlendOpaque(
    TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFaceTextureBlendOpaqueScanline(
            TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS);
        return;
    }
    ToriDraw_TriangleFaceTextureBlendOpaqueBranching(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_ARGUMENTS
#undef TORIDRAW_TRIANGLE_FACE_TEXTURE_BLEND_OPAQUE_PARAMETERS

#define TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_PARAMETERS                                    \
    int* RESTRICT pixel_buffer, int stride, int screen_width, int screen_height, int camera_cot16, \
        int face, int tp_vertex, int tm_vertex, int tn_vertex, faceint_t* RESTRICT face_indices_a, \
        faceint_t* RESTRICT face_indices_b, faceint_t* RESTRICT face_indices_c,                   \
        int* RESTRICT screen_vertices_x, int* RESTRICT screen_vertices_y,                         \
        int* RESTRICT screen_vertices_z, int* RESTRICT orthographic_vertices_x,                   \
        int* RESTRICT orthographic_vertices_y, int* RESTRICT orthographic_vertices_z,             \
        hsl16_t* RESTRICT colors, int* RESTRICT texels, int texture_size, int near_plane_z,       \
        int offset_x, int offset_y, bool allow_near_clip, bool near_clipped
#define TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS                                      \
    pixel_buffer, stride, screen_width, screen_height, camera_cot16, face, tp_vertex, tm_vertex,   \
        tn_vertex, face_indices_a, face_indices_b, face_indices_c, screen_vertices_x,              \
        screen_vertices_y, screen_vertices_z, orthographic_vertices_x, orthographic_vertices_y,   \
        orthographic_vertices_z, colors, texels, texture_size, near_plane_z, offset_x, offset_y,  \
        allow_near_clip, near_clipped

static inline void
ToriDraw_TriangleFaceTextureFlatOpaqueBranching(
    TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleFaceTextureFlatOpaqueImpl(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS, false);
}

static inline void
ToriDraw_TriangleFaceTextureFlatOpaqueScanline(
    TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    ToriDraw_TriangleFaceTextureFlatOpaqueImpl(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS, true);
}

static inline void
ToriDraw_TriangleFaceTextureFlatOpaque(TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_PARAMETERS)
{
    if( TORIDRAW_SCANLINE_SELECTED() )
    {
        ToriDraw_TriangleFaceTextureFlatOpaqueScanline(
            TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS);
        return;
    }
    ToriDraw_TriangleFaceTextureFlatOpaqueBranching(
        TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS);
}

#undef TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_ARGUMENTS
#undef TORIDRAW_TRIANGLE_FACE_TEXTURE_FLAT_OPAQUE_PARAMETERS

#endif
