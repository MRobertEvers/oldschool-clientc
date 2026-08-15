/*
 * Texture coordinate generation for model render types 0-3.
 *
 * Ported from rs-map-viewer's TextureMapper.ts (computeTextureCoords,
 * calculateTextureScales, method2424/2416/2431/2434/2437), itself a
 * transcription of the client's. See toridraw_texture_uv.h for why explicit uv
 * is the only representation that covers all four types.
 *
 * Three things are deliberately preserved rather than tidied:
 *
 *   - **float, not double.** The reference computes the mapping bases in
 *     Float32Array and the uv in Float32Array. Promoting to double changes the
 *     last bits of every angle, and the seam-fixup thresholds below
 *     (`> 0.99 && < 1.1`, `> scaleZHalf`) compare against those bits. The
 *     authored mappings were tuned against the reference's rounding.
 *
 *   - **the type-3 asymmetry.** In the sphere seam fixup the reference tests
 *     `u0 - u1 > 0` where every sibling test uses `> 0.5`. It reads like a typo
 *     and it is reproduced, because it changes which side of a seam a vertex
 *     lands on and "fixing" it would silently move geometry the reference does
 *     not move. Flagged here rather than corrected.
 *
 *   - **the cube axis test re-applies the raw scales.** The basis matrix is
 *     already scaled, and the axis test divides by the scales *again*. That is
 *     what the reference does and which cube face a triangle lands on depends
 *     on it.
 *
 * The projections themselves are NOT duplicated here: they live in
 * graphics/raster/texture/texmap_common.h, because the texcylinder / texcube /
 * texsphere kernels do the same projection per triangle and the two must not be
 * able to disagree. This file owns the per-face-group setup (the mapping basis)
 * and the type-0 plane solve; the projection is shared.
 *
 * What is NOT the reference: the angles. libm's atan2 and asin are not
 * bit-identical across platforms, so uv generated from them is not reproducible
 * either. Both go through the arctangent table in graphics/shared_tables.h,
 * which is accurate to about 0.003 of a texel on a 128-wide wrap and identical
 * everywhere. Spherical mapping uses `atan2(y, hypot(x, z))` rather than
 * `asin(y / len)` — the same angle, one fewer division and one fewer square
 * root, and it never forms 0/0 at the centre.
 */

#include "toridraw_texture_uv.h"

#include "graphics/raster/texture/texmap_common.h"
#include "graphics/shared_tables.h"


#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_TAU 6.2831855f
#define TORIDRAW_PI 3.1415927f
/* pi / 128 — the rotation byte is in 1/256 turns. */
#define TORIDRAW_ROTATION_STEP 0.024543693f

/**
 * The 3x3 that takes a centred vertex into mapping space.
 *
 * Rotation about the stored axis (p, m, n), then the stored rotation about y,
 * then the per-axis scales. `m / 32767` is the axis's y component, so a stored
 * m of -32767 is a clean -Y axis — which is what every QBD cube face uses.
 */
static void
texture_basis_matrix(
    int p,
    int m,
    int n,
    int rotation,
    float scale_x,
    float scale_y,
    float scale_z,
    float* out)
{
    float axis[9];
    float cos_a = 1.0f;
    float sin_a = 0.0f;

    float my = (float)m / 32767.0f;
    float mz = -sqrtf(1.0f - my * my);
    float one_minus = 1.0f - my;

    float len = sqrtf((float)p * (float)p + (float)n * (float)n);
    if( len != 0.0f )
    {
        cos_a = -(float)n / len;
        sin_a = (float)p / len;
    }

    axis[0] = my + cos_a * cos_a * one_minus;
    axis[1] = sin_a * mz;
    axis[2] = sin_a * cos_a * one_minus;
    axis[3] = -sin_a * mz;
    axis[4] = my;
    axis[5] = cos_a * mz;
    axis[6] = cos_a * sin_a * one_minus;
    axis[7] = -cos_a * mz;
    axis[8] = my + sin_a * sin_a * one_minus;

    float rot[9];
    float rc = cosf((float)rotation * TORIDRAW_ROTATION_STEP);
    float rs = sinf((float)rotation * TORIDRAW_ROTATION_STEP);
    rot[0] = rc;
    rot[1] = 0.0f;
    rot[2] = rs;
    rot[3] = 0.0f;
    rot[4] = 1.0f;
    rot[5] = 0.0f;
    rot[6] = -rs;
    rot[7] = 0.0f;
    rot[8] = rc;

    out[0] = rot[0] * axis[0] + rot[1] * axis[3] + rot[2] * axis[6];
    out[1] = rot[0] * axis[1] + rot[1] * axis[4] + rot[2] * axis[7];
    out[2] = rot[0] * axis[2] + rot[1] * axis[5] + rot[2] * axis[8];
    out[3] = rot[3] * axis[0] + rot[4] * axis[3] + rot[5] * axis[6];
    out[4] = rot[3] * axis[1] + rot[4] * axis[4] + rot[5] * axis[7];
    out[5] = rot[3] * axis[2] + rot[4] * axis[5] + rot[5] * axis[8];
    out[6] = rot[6] * axis[0] + rot[7] * axis[3] + rot[8] * axis[6];
    out[7] = rot[6] * axis[1] + rot[7] * axis[4] + rot[8] * axis[7];
    out[8] = rot[6] * axis[2] + rot[7] * axis[5] + rot[8] * axis[8];

    out[0] *= scale_x;
    out[1] *= scale_x;
    out[2] *= scale_x;
    out[3] *= scale_y;
    out[4] *= scale_y;
    out[5] *= scale_y;
    out[6] *= scale_z;
    out[7] *= scale_z;
    out[8] *= scale_z;
}






bool
ToriDraw_ComputeTextureUvBases(
    const struct ToriDraw_TextureUvSource* model,
    struct ToriDraw_TextureUvBasis* out_basis)
{
    if( !model || !out_basis || model->textured_face_count <= 0 )
        return false;

    int count = model->textured_face_count;
    memset(out_basis, 0, (size_t)count * sizeof(*out_basis));

    if( !model->render_types )
        return true;

    int* min_x = (int*)malloc((size_t)count * 6 * sizeof(int));
    if( !min_x )
        return false;
    int* max_x = min_x + count;
    int* min_y = max_x + count;
    int* max_y = min_y + count;
    int* min_z = max_y + count;
    int* max_z = min_z + count;

    for( int i = 0; i < count; i++ )
    {
        min_x[i] = min_y[i] = min_z[i] = 2147483647;
        max_x[i] = max_y[i] = max_z[i] = -2147483647;
    }

    /* The mapping centre is the midpoint of the bounding box of every vertex
     * used by a face naming this textured-face index — a property of the face
     * *group*, not of any one triangle, which is why it cannot be computed
     * inside the per-face loop below. */
    if( model->face_texture_coords )
    {
        for( int f = 0; f < model->face_count; f++ )
        {
            int coord = model->face_texture_coords[f];
            if( coord < 0 || coord >= count )
                continue;

            int idx[3] = { model->face_indices_a[f], model->face_indices_b[f],
                           model->face_indices_c[f] };
            for( int k = 0; k < 3; k++ )
            {
                int vi = idx[k];
                if( vi < 0 || vi >= model->vertex_count )
                    continue;
                int vx = model->vertices_x[vi];
                int vy = model->vertices_y[vi];
                int vz = model->vertices_z[vi];
                if( vx < min_x[coord] )
                    min_x[coord] = vx;
                if( vx > max_x[coord] )
                    max_x[coord] = vx;
                if( vy < min_y[coord] )
                    min_y[coord] = vy;
                if( vy > max_y[coord] )
                    max_y[coord] = vy;
                if( vz < min_z[coord] )
                    min_z[coord] = vz;
                if( vz > max_z[coord] )
                    max_z[coord] = vz;
            }
        }
    }

    for( int i = 0; i < count; i++ )
    {
        int type = model->render_types[i] & 0xFF;
        if( type == 0 || type > 3 )
            continue;
        if( min_x[i] > max_x[i] )
            continue; /* no face referenced it */

        out_basis[i].centre_x = (min_x[i] + max_x[i]) / 2;
        out_basis[i].centre_y = (min_y[i] + max_y[i]) / 2;
        out_basis[i].centre_z = (min_z[i] + max_z[i]) / 2;

        int sx_raw = model->scale_x ? model->scale_x[i] : 0;
        int sy_raw = model->scale_y ? model->scale_y[i] : 0;
        int sz_raw = model->scale_z ? model->scale_z[i] : 0;

        float scale_x;
        float scale_y;
        float scale_z;
        if( type == 1 )
        {
            scale_y = sy_raw ? 64.0f / (float)sy_raw : 0.0f;
            if( sx_raw == 0 )
            {
                scale_x = 1.0f;
                scale_z = 1.0f;
            }
            else if( sx_raw < 0 )
            {
                scale_x = -(float)sx_raw / 1024.0f;
                scale_z = 1.0f;
            }
            else
            {
                scale_x = 1.0f;
                scale_z = (float)sx_raw / 1024.0f;
            }
        }
        else if( type == 2 )
        {
            scale_x = sx_raw ? 64.0f / (float)sx_raw : 0.0f;
            scale_y = sy_raw ? 64.0f / (float)sy_raw : 0.0f;
            scale_z = sz_raw ? 64.0f / (float)sz_raw : 0.0f;
        }
        else
        {
            scale_x = (float)sx_raw / 1024.0f;
            scale_y = (float)sy_raw / 1024.0f;
            scale_z = (float)sz_raw / 1024.0f;
        }

        texture_basis_matrix(
            (int16_t)model->textured_p[i],
            (int16_t)model->textured_m[i],
            (int16_t)model->textured_n[i],
            model->rotation ? (model->rotation[i] & 0xFF) : 0,
            scale_x,
            scale_y,
            scale_z,
            out_basis[i].matrix);
        out_basis[i].valid = true;
    }

    free(min_x);
    return true;
}

/** The render-type-0 plane solve, straight from the reference. */
static void
project_plane(
    const struct ToriDraw_TextureUvSource* model,
    int face,
    int p,
    int m,
    int n,
    float* u,
    float* v)
{
    int i0 = model->face_indices_a[face];
    int i1 = model->face_indices_b[face];
    int i2 = model->face_indices_c[face];

    float ox = (float)model->vertices_x[p];
    float oy = (float)model->vertices_y[p];
    float oz = (float)model->vertices_z[p];

    float mu_x = (float)model->vertices_x[m] - ox;
    float mu_y = (float)model->vertices_y[m] - oy;
    float mu_z = (float)model->vertices_z[m] - oz;
    float nv_x = (float)model->vertices_x[n] - ox;
    float nv_y = (float)model->vertices_y[n] - oy;
    float nv_z = (float)model->vertices_z[n] - oz;

    float d0x = (float)model->vertices_x[i0] - ox;
    float d0y = (float)model->vertices_y[i0] - oy;
    float d0z = (float)model->vertices_z[i0] - oz;
    float d1x = (float)model->vertices_x[i1] - ox;
    float d1y = (float)model->vertices_y[i1] - oy;
    float d1z = (float)model->vertices_z[i1] - oz;
    float d2x = (float)model->vertices_x[i2] - ox;
    float d2y = (float)model->vertices_y[i2] - oy;
    float d2z = (float)model->vertices_z[i2] - oz;

    float nx = mu_y * nv_z - mu_z * nv_y;
    float ny = mu_z * nv_x - mu_x * nv_z;
    float nz = mu_x * nv_y - mu_y * nv_x;

    float ax = nv_y * nz - nv_z * ny;
    float ay = nv_z * nx - nv_x * nz;
    float az = nv_x * ny - nv_y * nx;
    float det_u = ax * mu_x + ay * mu_y + az * mu_z;
    /*
     * A degenerate projector — p, m and n collinear, or two of them the same
     * vertex — makes this determinant zero. The reference divides anyway and
     * gets Infinity, which JS carries harmlessly into a Float32Array; here it
     * would reach a rasterizer, and a non-finite uv poisons an entire triangle
     * rather than one texel. Measured at 467 of 6,395,265 type-0 faces across
     * cache.rs727_preeoc, so it is rare and real. Leaving uv at zero collapses
     * the face to one texel, which is what a projector with no extent means.
     */
    if( det_u == 0.0f )
    {
        u[0] = u[1] = u[2] = 0.0f;
        v[0] = v[1] = v[2] = 0.0f;
        return;
    }
    float inv = 1.0f / det_u;

    u[0] = (ax * d0x + ay * d0y + az * d0z) * inv;
    u[1] = (ax * d1x + ay * d1y + az * d1z) * inv;
    u[2] = (ax * d2x + ay * d2y + az * d2z) * inv;

    ax = mu_y * nz - mu_z * ny;
    ay = mu_z * nx - mu_x * nz;
    az = mu_x * ny - mu_y * nx;
    float det_v = ax * nv_x + ay * nv_y + az * nv_z;
    if( det_v == 0.0f )
    {
        u[0] = u[1] = u[2] = 0.0f;
        v[0] = v[1] = v[2] = 0.0f;
        return;
    }
    inv = 1.0f / det_v;

    v[0] = (ax * d0x + ay * d0y + az * d0z) * inv;
    v[1] = (ax * d1x + ay * d1y + az * d1z) * inv;
    v[2] = (ax * d2x + ay * d2y + az * d2z) * inv;

    /* The reference's snap-to-1 pass. A projector whose u span lands within a
     * hair of exactly one tile is snapped so adjacent faces share an edge
     * exactly; the asymmetric ordering is the reference's. */
    if( u[1] - u[0] > 0.99f && u[1] - u[0] < 1.1f )
        u[1] = 1.0f;
    if( u[2] - u[1] > 0.99f && u[2] - u[1] < 1.1f )
        u[2] = 1.0f;
    if( u[0] - u[2] > 0.99f && u[0] - u[2] < 1.1f )
        u[0] = 1.0f;
    if( u[0] - u[1] > 0.99f && u[0] - u[1] < 1.1f )
        u[0] = 1.0f;
    if( u[1] - u[2] > 0.99f && u[1] - u[2] < 1.1f )
        u[1] = 1.0f;
    if( u[2] - u[0] > 0.99f && u[2] - u[0] < 1.1f )
        u[2] = 1.0f;
}

bool
ToriDraw_ComputeTextureUv(
    const struct ToriDraw_TextureUvSource* model,
    float* out_uv)
{
    if( !model || !out_uv || !model->face_textures )
        return false;

    memset(out_uv, 0, (size_t)model->face_count * 6 * sizeof(float));

    struct ToriDraw_TextureUvBasis* bases = NULL;
    if( model->textured_face_count > 0 )
    {
        bases = (struct ToriDraw_TextureUvBasis*)malloc(
            (size_t)model->textured_face_count * sizeof(*bases));
        if( !bases )
            return false;
        if( !ToriDraw_ComputeTextureUvBases(model, bases) )
        {
            free(bases);
            bases = NULL;
        }
    }

    for( int f = 0; f < model->face_count; f++ )
    {
        if( model->face_textures[f] < 0 )
            continue;

        int coord = model->face_texture_coords ? model->face_texture_coords[f] : -1;
        int type = 0;
        if( coord >= 0 && coord < model->textured_face_count && model->render_types )
            type = model->render_types[coord] & 0xFF;

        int i0 = model->face_indices_a[f];
        int i1 = model->face_indices_b[f];
        int i2 = model->face_indices_c[f];

        float u[3] = { 0.0f, 0.0f, 0.0f };
        float v[3] = { 0.0f, 0.0f, 0.0f };

        if( type == 0 )
        {
            int p = i0;
            int m = i1;
            int n = i2;
            if( coord >= 0 && coord < model->textured_face_count )
            {
                p = (int)model->textured_p[coord];
                m = (int)model->textured_m[coord];
                n = (int)model->textured_n[coord];
            }
            if( p < 0 || p >= model->vertex_count || m < 0 || m >= model->vertex_count ||
                n < 0 || n >= model->vertex_count )
                continue;
            project_plane(model, f, p, m, n, u, v);
        }
        else if( bases && coord >= 0 && coord < model->textured_face_count &&
                 bases[coord].valid )
        {
            /* Build the same mapping descriptor the kernels take, then call the
             * same projection they call. One implementation, so a generated uv
             * and a kernel-drawn uv cannot disagree. */
            const struct ToriDraw_TextureUvBasis* b = &bases[coord];
            struct ToriDraw_TexMapping map;
            memset(&map, 0, sizeof(map));
            map.centre_x = b->centre_x;
            map.centre_y = b->centre_y;
            map.centre_z = b->centre_z;
            for( int k = 0; k < 9; k++ )
                map.matrix[k] = b->matrix[k];
            map.direction = model->direction ? (model->direction[coord] & 0xFF) : 0;
            map.speed = model->speed ? (float)model->speed[coord] / 256.0f : 0.0f;

            int idx[3] = { i0, i1, i2 };

            if( type == 1 )
            {
                int sx_raw = model->scale_x ? model->scale_x[coord] : 0;
                map.scale_z = (float)sx_raw / 1024.0f;

                for( int k = 0; k < 3; k++ )
                    toridraw_texmap_project_cylinder(
                        &map,
                        model->vertices_x[idx[k]],
                        model->vertices_y[idx[k]],
                        model->vertices_z[idx[k]],
                        &u[k], &v[k]);

                float half = map.scale_z / 2.0f;
                float* axis = (map.direction & 1) ? v : u;
                if( axis[1] - axis[0] > half )
                    axis[1] -= map.scale_z;
                else if( axis[0] - axis[1] > half )
                    axis[1] += map.scale_z;
                if( axis[2] - axis[0] > half )
                    axis[2] -= map.scale_z;
                else if( axis[0] - axis[2] > half )
                    axis[2] += map.scale_z;
            }
            else if( type == 2 )
            {
                map.u_offset =
                    model->trans_u ? (float)model->trans_u[coord] / 256.0f : 0.0f;
                map.v_offset =
                    model->trans_v ? (float)model->trans_v[coord] / 256.0f : 0.0f;

                int sx_raw = model->scale_x ? model->scale_x[coord] : 0;
                int sy_raw = model->scale_y ? model->scale_y[coord] : 0;
                int sz_raw = model->scale_z ? model->scale_z[coord] : 0;
                map.axis_scale_x = sx_raw ? (float)sx_raw / 64.0f : 0.0f;
                map.axis_scale_y = sy_raw ? (float)sy_raw / 64.0f : 0.0f;
                map.axis_scale_z = sz_raw ? (float)sz_raw / 64.0f : 0.0f;

                float d1x = (float)(model->vertices_x[i1] - model->vertices_x[i0]);
                float d1y = (float)(model->vertices_y[i1] - model->vertices_y[i0]);
                float d1z = (float)(model->vertices_z[i1] - model->vertices_z[i0]);
                float d2x = (float)(model->vertices_x[i2] - model->vertices_x[i0]);
                float d2y = (float)(model->vertices_y[i2] - model->vertices_y[i0]);
                float d2z = (float)(model->vertices_z[i2] - model->vertices_z[i0]);
                float nx = d1y * d2z - d2y * d1z;
                float ny = d1z * d2x - d2z * d1x;
                float nz = d1x * d2y - d2x * d1y;

                float ax = (nx * map.matrix[0] + ny * map.matrix[1] + nz * map.matrix[2]) *
                           map.axis_scale_x;
                float ay = (nx * map.matrix[3] + ny * map.matrix[4] + nz * map.matrix[5]) *
                           map.axis_scale_y;
                float az = (nx * map.matrix[6] + ny * map.matrix[7] + nz * map.matrix[8]) *
                           map.axis_scale_z;
                int axis_sel = toridraw_texmap_cube_axis(ax, ay, az);

                for( int k = 0; k < 3; k++ )
                    toridraw_texmap_project_cube(
                        &map, axis_sel,
                        model->vertices_x[idx[k]],
                        model->vertices_y[idx[k]],
                        model->vertices_z[idx[k]],
                        &u[k], &v[k]);
            }
            else if( type == 3 )
            {
                for( int k = 0; k < 3; k++ )
                    toridraw_texmap_project_sphere(
                        &map,
                        model->vertices_x[idx[k]],
                        model->vertices_y[idx[k]],
                        model->vertices_z[idx[k]],
                        &u[k], &v[k]);

                float* axis = (map.direction & 1) ? v : u;
                if( axis[1] - axis[0] > 0.5f )
                    axis[1] -= 1.0f;
                else if( axis[0] - axis[1] > 0.0f )
                    axis[1] += 1.0f;
                if( axis[2] - axis[0] > 0.5f )
                    axis[2] -= 1.0f;
                else if( axis[0] - axis[2] > 0.5f )
                    axis[2] += 1.0f;
            }
        }

        float* dst = out_uv + (size_t)f * 6;
        dst[0] = u[0];
        dst[1] = v[0];
        dst[2] = u[1];
        dst[3] = v[1];
        dst[4] = u[2];
        dst[5] = v[2];
    }

    free(bases);
    return true;
}
