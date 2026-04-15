/* PaintersCullMap: bitpacked precomputed frustum visibility per (pitch,yaw,dx,dz).
 * Included from painters.c — do not compile as a separate translation unit. */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* projection.u.c is include-guarded (PROJECTION_U_C); safe if another TU already linked it. */
// clang-format off
#include "../graphics/projection.u.c"
// clang-format on

#ifndef PCULL_PITCH_MIN
#define PCULL_PITCH_MIN 128
#endif
#ifndef PCULL_PITCH_MAX
#define PCULL_PITCH_MAX 384
#endif
#ifndef PCULL_PITCH_STEP
#define PCULL_PITCH_STEP 32
#endif
#ifndef PCULL_YAW_STEP
#define PCULL_YAW_STEP 64
#endif
#ifndef PCULL_FRUSTUM_Y_START
#define PCULL_FRUSTUM_Y_START (-500)
#endif
#ifndef PCULL_FRUSTUM_Y_END
#define PCULL_FRUSTUM_Y_END 1500
#endif
#ifndef PCULL_Y_STEP
#define PCULL_Y_STEP 128
#endif
#ifndef PCULL_Y_GRANULARITY
#define PCULL_Y_GRANULARITY 1
#endif

extern int g_sin_table[2048];

struct PaintersCullMap
{
    uint8_t* visibility;
    int radius;
    int pitch_levels;
    int yaw_levels;
    int grid_side;
    int all_visible;
};

static int64_t
pcull_pitch_height(int pitch_rad)
{
    int angle = pitch_rad + 15;
    if( angle >= 2048 )
        angle -= 2048;
    if( angle < 0 )
        angle += 2048;
    int offset = 600;
    int sin = g_sin_table[angle];
    return (int64_t)offset * (int64_t)sin >> 16;
}

static bool
pcull_test_point_in_frustum(
    int x,
    int z,
    int y,
    int pitch,
    int yaw,
    int near_clip_z,
    int screen_width,
    int screen_height)
{
    struct ProjectedVertex projected_vertex;
    project_fast(
        &projected_vertex,
        0,
        0,
        0,
        0,
        x,
        y,
        z,
        pitch,
        yaw,
        512,
        near_clip_z,
        screen_width,
        screen_height);

    if( projected_vertex.clipped || projected_vertex.z > 3500 )
        return false;

    projected_vertex.x = (projected_vertex.x + (screen_width >> 1));
    projected_vertex.y = (projected_vertex.y + (screen_height >> 1));

    return projected_vertex.x >= 0 && projected_vertex.x < screen_width &&
           projected_vertex.y >= 0 && projected_vertex.y < screen_height;
}

static inline size_t
pcull_bit_count(
    int pitch_levels,
    int yaw_levels,
    int grid_side)
{
    return (size_t)pitch_levels * (size_t)yaw_levels * (size_t)grid_side * (size_t)grid_side;
}

static inline void
pcull_bit_set(
    uint8_t* vis,
    size_t bit_index,
    int on)
{
    size_t bi = bit_index >> 3;
    unsigned sh = (unsigned)(bit_index & 7u);
    if( on )
        vis[bi] |= (uint8_t)(1u << sh);
    else
        vis[bi] &= (uint8_t)~(1u << sh);
}

static inline int
pcull_bit_get(
    const uint8_t* vis,
    size_t bit_index)
{
    size_t bi = bit_index >> 3;
    unsigned sh = (unsigned)(bit_index & 7u);
    return (vis[bi] >> sh) & 1;
}

static inline size_t
pcull_bit_index(
    int pitch,
    int yaw,
    int ix,
    int iz,
    int yaw_levels,
    int grid_side)
{
    size_t per_pitch = (size_t)yaw_levels * (size_t)grid_side * (size_t)grid_side;
    return (size_t)pitch * per_pitch + (size_t)yaw * (size_t)grid_side * (size_t)grid_side +
           (size_t)ix * (size_t)grid_side + (size_t)iz;
}

struct PaintersCullMap*
painters_cullmap_build(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height)
{
    if( radius < 1 )
        return NULL;

    int pitch_raw_levels = (PCULL_PITCH_MAX - PCULL_PITCH_MIN) / PCULL_PITCH_STEP + 1;
    if( pitch_raw_levels < 2 )
        return NULL;

    int pitch_out_levels = pitch_raw_levels - 1;
    int yaw_levels = 2048 / PCULL_YAW_STEP;
    if( yaw_levels < 1 )
        yaw_levels = 1;

    int grid_side = radius * 2;
    int padded_side = 2 * radius + 3;

    size_t raw_count =
        (size_t)pitch_raw_levels * (size_t)yaw_levels * (size_t)padded_side * (size_t)padded_side;
    uint8_t* raw = (uint8_t*)malloc(raw_count);
    if( !raw )
        return NULL;
    memset(raw, 0, raw_count);

#define PCULL_RAW_IDX(pr, yw, px, pz)                                                              \
    ((size_t)(pr) * (size_t)yaw_levels * (size_t)padded_side * (size_t)padded_side +               \
     (size_t)(yw) * (size_t)padded_side * (size_t)padded_side +                                    \
     (size_t)(px) * (size_t)padded_side + (size_t)(pz))

    for( int pr = 0; pr < pitch_raw_levels; pr++ )
    {
        int pitch_rad = PCULL_PITCH_MIN + pr * PCULL_PITCH_STEP;
        int64_t ph = pcull_pitch_height(pitch_rad);

        for( int yw = 0; yw < yaw_levels; yw++ )
        {
            int yaw_rad = yw * PCULL_YAW_STEP;

            for( int ddx = -(radius + 1); ddx <= (radius + 1); ddx++ )
            {
                for( int ddz = -(radius + 1); ddz <= (radius + 1); ddz++ )
                {
                    int px = ddx + (radius + 1);
                    int pz = ddz + (radius + 1);
                    int screen_x = ddx * 128;
                    int screen_z = ddz * 128;
                    int vis = 0;

                    for( int fy = PCULL_FRUSTUM_Y_START; fy <= PCULL_FRUSTUM_Y_END;
                         fy += PCULL_Y_STEP )
                    {
                        int to_tile_y = (int)(ph + (int64_t)fy);

                        if( pcull_test_point_in_frustum(
                                screen_x,
                                screen_z,
                                to_tile_y,
                                pitch_rad,
                                yaw_rad,
                                near_clip_z,
                                screen_width,
                                screen_height) )
                        {
                            vis = 1;
                            break;
                        }
                        if( pcull_test_point_in_frustum(
                                screen_x + 128,
                                screen_z,
                                to_tile_y,
                                pitch_rad,
                                yaw_rad,
                                near_clip_z,
                                screen_width,
                                screen_height) )
                        {
                            vis = 1;
                            break;
                        }
                        if( pcull_test_point_in_frustum(
                                screen_x + 128,
                                screen_z + 128,
                                to_tile_y,
                                pitch_rad,
                                yaw_rad,
                                near_clip_z,
                                screen_width,
                                screen_height) )
                        {
                            vis = 1;
                            break;
                        }
                        if( pcull_test_point_in_frustum(
                                screen_x,
                                screen_z + 128,
                                to_tile_y,
                                pitch_rad,
                                yaw_rad,
                                near_clip_z,
                                screen_width,
                                screen_height) )
                        {
                            vis = 1;
                            break;
                        }
                    }

                    raw[PCULL_RAW_IDX(pr, yw, px, pz)] = (uint8_t)(vis ? 1 : 0);
                }
            }
        }
    }

#undef PCULL_RAW_IDX

    size_t nbits = pcull_bit_count(pitch_out_levels, yaw_levels, grid_side);
    size_t nbytes = (nbits + 7u) / 8u;
    uint8_t* visibility = (uint8_t*)malloc(nbytes);
    if( !visibility )
    {
        free(raw);
        return NULL;
    }
    memset(visibility, 0, nbytes);

    for( int op = 0; op < pitch_out_levels; op++ )
    {
        for( int yw = 0; yw < yaw_levels; yw++ )
        {
            for( int ix = 0; ix < grid_side; ix++ )
            {
                for( int iz = 0; iz < grid_side; iz++ )
                {
                    int x_off = ix - radius;
                    int z_off = iz - radius;
                    int out_vis = 0;

                    for( int dpx = 0; dpx <= 1 && !out_vis; dpx++ )
                    {
                        for( int dyw = 0; dyw <= 1 && !out_vis; dyw++ )
                        {
                            int rp = op + dpx;
                            int ry = (yw + dyw) % yaw_levels;

                            for( int edx = -1; edx <= 1 && !out_vis; edx++ )
                            {
                                for( int edz = -1; edz <= 1 && !out_vis; edz++ )
                                {
                                    int padded_x = (x_off + edx) + (radius + 1);
                                    int padded_z = (z_off + edz) + (radius + 1);
                                    if( padded_x >= 0 && padded_x < padded_side && padded_z >= 0 &&
                                        padded_z < padded_side )
                                    {
                                        size_t ri =
                                            (size_t)rp * (size_t)yaw_levels * (size_t)padded_side *
                                                (size_t)padded_side +
                                            (size_t)ry * (size_t)padded_side * (size_t)padded_side +
                                            (size_t)padded_x * (size_t)padded_side +
                                            (size_t)padded_z;
                                        if( raw[ri] )
                                            out_vis = 1;
                                    }
                                }
                            }
                        }
                    }

                    size_t bidx = pcull_bit_index(op, yw, ix, iz, yaw_levels, grid_side);
                    pcull_bit_set(visibility, bidx, out_vis);
                }
            }
        }
    }

    free(raw);

    struct PaintersCullMap* cm = (struct PaintersCullMap*)malloc(sizeof(struct PaintersCullMap));
    if( !cm )
    {
        free(visibility);
        return NULL;
    }
    cm->visibility = visibility;
    cm->radius = radius;
    cm->pitch_levels = pitch_out_levels;
    cm->yaw_levels = yaw_levels;
    cm->grid_side = grid_side;
    cm->all_visible = 0;
    return cm;
}

struct PaintersCullMap*
painters_cullmap_new_nocull(void)
{
    struct PaintersCullMap* cm = (struct PaintersCullMap*)malloc(sizeof(struct PaintersCullMap));
    if( !cm )
        return NULL;
    cm->visibility = NULL;
    cm->radius = 0;
    cm->pitch_levels = 0;
    cm->yaw_levels = 0;
    cm->grid_side = 0;
    cm->all_visible = 1;
    return cm;
}

void
painters_cullmap_free(struct PaintersCullMap* cm)
{
    if( !cm )
        return;
    free(cm->visibility);
    free(cm);
}

static int
pcull_dims_and_nbytes_for_radius(
    int radius,
    int* pitch_out_levels,
    int* yaw_levels,
    int* grid_side,
    size_t* nbytes_out)
{
    if( radius < 1 )
        return 0;
    int pitch_raw_levels = (PCULL_PITCH_MAX - PCULL_PITCH_MIN) / PCULL_PITCH_STEP + 1;
    if( pitch_raw_levels < 2 )
        return 0;
    int pol = pitch_raw_levels - 1;
    int yl = 2048 / PCULL_YAW_STEP;
    if( yl < 1 )
        yl = 1;
    int gs = radius * 2;
    size_t nbits = (size_t)pol * (size_t)yl * (size_t)gs * (size_t)gs;
    size_t nbytes = (nbits + 7u) / 8u;
    *pitch_out_levels = pol;
    *yaw_levels = yl;
    *grid_side = gs;
    *nbytes_out = nbytes;
    return 1;
}

struct PaintersCullMap*
painters_cullmap_from_blob(
    const uint8_t* data,
    size_t nbytes,
    int radius)
{
    int pitch_out_levels;
    int yaw_levels;
    int grid_side;
    size_t want;
    if( !data ||
        !pcull_dims_and_nbytes_for_radius(
            radius, &pitch_out_levels, &yaw_levels, &grid_side, &want) ||
        nbytes != want )
        return NULL;

    uint8_t* vis = (uint8_t*)malloc(nbytes);
    if( !vis )
        return NULL;
    memcpy(vis, data, nbytes);

    struct PaintersCullMap* cm = (struct PaintersCullMap*)malloc(sizeof(struct PaintersCullMap));
    if( !cm )
    {
        free(vis);
        return NULL;
    }
    cm->visibility = vis;
    cm->radius = radius;
    cm->pitch_levels = pitch_out_levels;
    cm->yaw_levels = yaw_levels;
    cm->grid_side = grid_side;
    cm->all_visible = 0;
    return cm;
}

/** Caller must pass non-NULL cm with all_visible false. */
static inline void
painters_cullmap_indices_from_angles(
    const struct PaintersCullMap* cm,
    int pitch,
    int yaw,
    int* pitch_idx_out,
    int* yaw_idx_out)
{
    int pitch_idx = (pitch - PCULL_PITCH_MIN) / PCULL_PITCH_STEP;
    if( pitch_idx < 0 )
        pitch_idx = 0;
    if( pitch_idx >= cm->pitch_levels )
        pitch_idx = cm->pitch_levels - 1;

    int yaw_norm = yaw % 2048;
    if( yaw_norm < 0 )
        yaw_norm += 2048;
    int yaw_idx = yaw_norm / PCULL_YAW_STEP;
    if( yaw_idx >= cm->yaw_levels )
        yaw_idx = cm->yaw_levels - 1;

    *pitch_idx_out = pitch_idx;
    *yaw_idx_out = yaw_idx;
}

static inline int
painters_cullmap_visible(
    const struct PaintersCullMap* cm,
    int dx,
    int dz,
    int pitch_idx,
    int yaw_idx)
{
    if( !cm || cm->all_visible )
        return 1;

    if( dx < -cm->radius || dx >= cm->radius || dz < -cm->radius || dz >= cm->radius )
        return 0;

    int ix = dx + cm->radius;
    int iz = dz + cm->radius;
    size_t bidx = pcull_bit_index(pitch_idx, yaw_idx, ix, iz, cm->yaw_levels, cm->grid_side);
    return pcull_bit_get(cm->visibility, bidx);
}
