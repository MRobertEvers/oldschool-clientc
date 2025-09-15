
#include <assert.h>

// clang-format off
#include "flat.u.c"
#include "render_face_alpha.u.c"
#include "render_clip.u.c"
// clang-format on

static inline void
raster_flat(
    int* pixel_buffer,
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
    if( alpha == 0xFF )
    {
        raster_flat_s4(pixel_buffer, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color);
    }
    else
    {
        raster_flat_alpha_s4(
            pixel_buffer, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
    }
}

/**
 * This requires vertices to be wound counterclockwise.
 */
static inline void
raster_face_flat_near_clip(
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
    int* colors,
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

            // assert(0xFFFF0000 & ((zb - near_plane_z) * (orthographic_vertices_x[c] - xb)) == 0);

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

    int alpha = face_alpha(face_alphas_nullable, face);

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];

    assert(color >= 0 && color < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_flat(pixel_buffer, screen_width, screen_height, xa, xb, xc, ya, yb, yc, color, alpha);

    if( clipped_count != 4 )
        return;

    xb = g_clip_x[3];
    yb = g_clip_y[3];

    xb += offset_x;
    yb += offset_y;

    raster_flat(pixel_buffer, screen_width, screen_height, xa, xc, xb, ya, yc, yb, color, alpha);
}

static inline void
raster_face_flat(
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
    int* colors,
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
        raster_face_flat_near_clip(
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

    int color = colors[face];

    int alpha = face_alpha(face_alphas_nullable, face);

    assert(color >= 0 && color < 65536);

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);

    raster_flat(pixel_buffer, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color, alpha);
}