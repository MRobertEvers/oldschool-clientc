#include "scene_cache.h"

#include <assert.h>
#include <stdlib.h>

static inline void
raster_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int z1,
    int z2,
    int z3,
    int ortho_x0,
    int ortho_x1,
    int ortho_x3,
    int ortho_y0,
    int ortho_y1,
    int ortho_y3,
    int ortho_z0,
    int ortho_z1,
    int ortho_z3,
    int shade_a,
    int shade_b,
    int shade_c,
    struct Texture* texture,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    if( texture->opaque )
    {
        raster_texture_opaque_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            z1,
            z2,
            z3,
            ortho_x0,
            ortho_x1,
            ortho_x3,
            ortho_y0,
            ortho_y1,
            ortho_y3,
            ortho_z0,
            ortho_z1,
            ortho_z3,
            shade_a,
            shade_b,
            shade_c,
            texture->texels,
            texture->width);
    }
    else
    {
        raster_texture_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            z1,
            z2,
            z3,
            ortho_x0,
            ortho_x1,
            ortho_x3,
            ortho_y0,
            ortho_y1,
            ortho_y3,
            ortho_z0,
            ortho_z1,
            ortho_z3,
            shade_a,
            shade_b,
            shade_c,
            texture->texels,
            texture->width);
    }
}

static inline void
raster_face_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertex_x,
    int* screen_vertex_y,
    int* screen_vertex_z,
    int* orthographic_vertex_x,
    int* orthographic_vertex_y,
    int* orthographic_vertex_z,
    int* face_texture_ids,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    struct TexturesCache* textures_cache,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    struct Texture* texture = NULL;

    int x1 = screen_vertex_x[face_indices_a[face]];
    int x2 = screen_vertex_x[face_indices_b[face]];
    int x3 = screen_vertex_x[face_indices_c[face]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
        return;

    int y1 = screen_vertex_y[face_indices_a[face]];
    int z1 = screen_vertex_z[face_indices_a[face]];
    int y2 = screen_vertex_y[face_indices_b[face]];
    int z2 = screen_vertex_z[face_indices_b[face]];
    int y3 = screen_vertex_y[face_indices_c[face]];
    int z3 = screen_vertex_z[face_indices_c[face]];
    // // sw
    int ortho_x0 = orthographic_vertex_x[0];
    int ortho_y0 = orthographic_vertex_y[0];
    int ortho_z0 = orthographic_vertex_z[0];
    // se
    int ortho_x1 = orthographic_vertex_x[1];
    int ortho_y1 = orthographic_vertex_y[1];
    int ortho_z1 = orthographic_vertex_z[1];
    // ne
    int ortho_x2 = orthographic_vertex_x[2];
    int ortho_y2 = orthographic_vertex_y[2];
    int ortho_z2 = orthographic_vertex_z[2];
    // nw
    int ortho_x3 = orthographic_vertex_x[3];
    int ortho_y3 = orthographic_vertex_y[3];
    int ortho_z3 = orthographic_vertex_z[3];

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

    int texture_id = face_texture_ids[face];
    if( texture_id == -1 )
        return;

    texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 0.8);
    if( !texture )
        return;

    raster_texture(
        pixel_buffer,
        screen_width,
        screen_height,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        z1,
        z2,
        z3,
        ortho_x0,
        ortho_x1,
        ortho_x3,
        ortho_y0,
        ortho_y1,
        ortho_y3,
        ortho_z0,
        ortho_z1,
        ortho_z3,
        shade_a,
        shade_b,
        shade_c,
        texture,
        near_plane_z,
        offset_x,
        offset_y);

    return;
}
