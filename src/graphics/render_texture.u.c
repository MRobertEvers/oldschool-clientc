
#include <assert.h>
#include <stdlib.h>

// clang-format off
#include "texture.u.c"
#include "texture_blend_branching.u.c"
#include "texture_affine_branching_bary.u.c"
#include "render_clip.u.c"
// clang-format on

static inline void
raster_texture_blend(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
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
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    if( texture_opaque )
    {
        raster_texture_opaque_blend_blerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
    else
    {
        raster_texture_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
}

static inline void
raster_texture_flat(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
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
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    if( texture_opaque )
    {
        raster_texture_opaque_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
    else
    {
        raster_texture_transparent_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
}

static inline void
raster_face_texture_blend_near_clip(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
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

    int orthographic_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_x2 = orthographic_vertices_x[tn_vertex];

    int orthographic_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_y2 = orthographic_vertices_y[tn_vertex];

    int orthographic_z0 = orthographic_vertices_z[tp_vertex];
    int orthographic_z1 = orthographic_vertices_z[tm_vertex];
    int orthographic_z2 = orthographic_vertices_z[tn_vertex];

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

    raster_texture_blend(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);

    static int colors[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };

    // for( int i = 0; i < screen_height; i++ )
    // {
    //     for( int j = 0; j < clipped_count; j++ )
    //     {
    //         int x = g_clip_x[j];

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

    xb = g_clip_x[3];
    yb = g_clip_y[3];
    color_b = g_clip_color[3];

    // assert((xb > 0 && xb < screen_width) || (xa > 0 && xa < screen_width));

    xb += offset_x;
    yb += offset_y;

    raster_texture_blend(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);
}

static inline void
raster_face_texture_blend(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
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
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
    {
        raster_face_texture_blend_near_clip(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
            texture_opaque,
            near_plane_z,
            offset_x,
            offset_y);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[face]];
    int z1 = screen_vertices_z[face_indices_a[face]];
    int y2 = screen_vertices_y[face_indices_b[face]];
    int z2 = screen_vertices_z[face_indices_b[face]];
    int y3 = screen_vertices_y[face_indices_c[face]];
    int z3 = screen_vertices_z[face_indices_c[face]];

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

    // raster_texture_affine_opaque_blend_blerp8(
    //     pixel_buffer,
    //     screen_width,
    //     screen_height,
    //     camera_fov,
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

    raster_texture_blend(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);

    return;
}

static inline void
raster_face_texture_flat_near_clip(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors,
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
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

    if( za >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[a];
        g_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];

        if( zc >= near_plane_z )
        {
            assert(zc - za >= 0);
            lerp_slope = slopei(near_plane_z, zc, za);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

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

            clipped_count++;
        }
    }
    if( zb >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[b];
        g_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];

        if( za >= near_plane_z )
        {
            lerp_slope = slopei(near_plane_z, za, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);

            clipped_count++;
        }

        if( zc >= near_plane_z )
        {
            assert(zc - zb >= 0);
            lerp_slope = slopei(near_plane_z, zc, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);

            clipped_count++;
        }
    }
    if( zc >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[c];
        g_clip_y[clipped_count] = screen_vertices_y[c];
        assert(g_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];

        if( zb >= near_plane_z )
        {
            assert(zb - zc >= 0);
            lerp_slope = slopei(near_plane_z, zb, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);

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

            clipped_count++;
        }
    }
    if( clipped_count < 3 )
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

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_texture_flat(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);

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

    xb += offset_x;
    yb += offset_y;

    raster_texture_flat(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);
}

static inline void
raster_face_texture_flat(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors,
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
    {
        raster_face_texture_flat_near_clip(
            pixel_buffer,
            screen_width,
            screen_height,
            camera_fov,
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
            texture_opaque,
            near_plane_z,
            offset_x,
            offset_y);
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

    raster_texture_flat(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
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
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);

    return;
}
