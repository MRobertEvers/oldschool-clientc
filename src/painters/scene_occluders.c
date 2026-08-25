#include "scene_occluders.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct SceneOccluders*
scene_occluders_new(
    int width,
    int height,
    int levels)
{
    struct SceneOccluders* occ;
    int i;

    assert(width > 0);
    assert(height > 0);
    assert(levels > 0 && levels <= 4);

    occ = calloc(1, sizeof(*occ));
    assert(occ);

    occ->width = width;
    occ->height = height;
    occ->levels = levels;

    for( i = 0; i < levels; i++ )
    {
        occ->level_occluders[i] =
            calloc((size_t)OCCLUDER_MAX_PER_LEVEL, sizeof(struct SceneOccluder));
        if( !occ->level_occluders[i] )
        {
            scene_occluders_free(occ);
            return NULL;
        }
        occ->level_occluder_count[i] = 0;
    }

    occ->tile_cycle =
        calloc((size_t)width * (size_t)height * (size_t)levels, sizeof(int32_t));
    if( !occ->tile_cycle )
    {
        scene_occluders_free(occ);
        return NULL;
    }

    return occ;
}

void
scene_occluders_free(struct SceneOccluders* occ)
{
    int i;
    if( !occ )
        return;
    for( i = 0; i < 4; i++ )
        free(occ->level_occluders[i]);
    free(occ->ground_heights);
    free(occ->tile_cycle);
    free(occ);
}

void
scene_occluders_clear(struct SceneOccluders* occ)
{
    int i;
    assert(occ);
    for( i = 0; i < occ->levels; i++ )
        occ->level_occluder_count[i] = 0;
    occ->active_count = 0;
    for( i = 0; i < 6; i++ )
    {
        occ->run_start[i] = 0;
        occ->run_end[i] = 0;
    }
}

int
scene_occluders_add(
    struct SceneOccluders* occ,
    int top_level,
    int plane,
    int min_x,
    int min_y,
    int min_z,
    int max_x,
    int max_y,
    int max_z)
{
    struct SceneOccluder* o;
    int n;

    assert(occ);
    assert(top_level >= 0 && top_level < occ->levels);
    n = occ->level_occluder_count[top_level];
    if( n >= OCCLUDER_MAX_PER_LEVEL )
        return -1;

    o = &occ->level_occluders[top_level][n];
    memset(o, 0, sizeof(*o));
    o->min_x = min_x;
    o->max_x = max_x;
    o->min_y = min_y;
    o->max_y = max_y;
    o->min_z = min_z;
    o->max_z = max_z;
    o->tile_min_x = (int16_t)(min_x / 128);
    o->tile_max_x = (int16_t)(max_x / 128);
    o->tile_min_z = (int16_t)(min_z / 128);
    o->tile_max_z = (int16_t)(max_z / 128);
    /* Constant-X/Z planes collapse one axis (min==max), so tile_max equals
     * tile_min for that axis — matching World.setOcclude's (coord/128)|0. */
    o->plane = (uint8_t)plane;
    o->hides = OCCLUDER_HIDES_NOTHING;
    occ->level_occluder_count[top_level] = n + 1;
    return 0;
}

void
scene_occluders_set_ground_heights(
    struct SceneOccluders* occ,
    const int16_t* heights,
    int ground_width,
    int ground_height,
    int levels)
{
    size_t n;
    assert(occ);
    assert(heights);
    assert(levels == occ->levels);

    n = (size_t)ground_width * (size_t)ground_height * (size_t)levels;
    free(occ->ground_heights);
    occ->ground_heights = malloc(n * sizeof(int16_t));
    assert(occ->ground_heights);
    memcpy(occ->ground_heights, heights, n * sizeof(int16_t));
    occ->ground_width = ground_width;
    occ->ground_height = ground_height;
}

int
scene_occluders_ground_height(
    const struct SceneOccluders* occ,
    int x,
    int z,
    int level)
{
    int idx;
    assert(occ);
    assert(occ->ground_heights);
    if( x < 0 )
        x = 0;
    if( z < 0 )
        z = 0;
    if( x >= occ->ground_width )
        x = occ->ground_width - 1;
    if( z >= occ->ground_height )
        z = occ->ground_height - 1;
    if( level < 0 )
        level = 0;
    if( level >= occ->levels )
        level = occ->levels - 1;
    idx = x + z * occ->ground_width + level * occ->ground_width * occ->ground_height;
    return occ->ground_heights[idx];
}

/** Footprint tile visible under the current frustum. Mirrors
 * painters_cullspan_visible using the public span fields so this TU does not
 * depend on painters.c internals. Requires a span: "no span at all" is the
 * caller's condition (conservatively visible), tested at the call sites. */
static int
occluder_footprint_tile_visible(
    const struct PaintersCullSpan* span,
    int dx,
    int dz)
{
    int idx;
    assert(span);
    if( span->empty )
        return 1;
    if( dz < span->dz_min || dz > span->dz_max )
        return 0;
    idx = dz - span->dz_min;
    if( span->row_min[idx] > span->row_max[idx] )
        return 0;
    return dx >= (int)span->row_min[idx] && dx <= (int)span->row_max[idx];
}

static void
occluder_partition_active(struct SceneOccluders* occ)
{
    struct SceneOccluder* tmp[OCCLUDER_MAX_PER_LEVEL];
    int counts[6];
    int starts[6];
    int i;
    int h;
    int n = occ->active_count;

    memset(counts, 0, sizeof(counts));
    for( i = 0; i < n; i++ )
    {
        h = occ->active[i]->hides;
        if( h >= 1 && h <= 5 )
            counts[h]++;
    }
    starts[0] = 0;
    starts[1] = 0;
    for( h = 2; h <= 5; h++ )
        starts[h] = starts[h - 1] + counts[h - 1];
    for( h = 1; h <= 5; h++ )
    {
        occ->run_start[h] = starts[h];
        occ->run_end[h] = starts[h];
    }
    for( i = 0; i < n; i++ )
    {
        h = occ->active[i]->hides;
        if( h < 1 || h > 5 )
            continue;
        tmp[occ->run_end[h]++] = occ->active[i];
    }
    for( i = 0; i < n; i++ )
        occ->active[i] = tmp[i];
}

/**
 * Build occ->packed from occ->active: sort each run by its (sign-folded)
 * plane coordinate descending, then copy in the nine fields the point test
 * reads.
 *
 * Insertion sort because a run is at most a few dozen entries and this runs
 * once a frame against ~600,000 point-test visits -- the constant factor
 * here is not a term in anything.
 */
#if !(defined(TORIDRAW_OCC_REF) && TORIDRAW_OCC_REF)
static void
occluder_pack_active(struct SceneOccluders* occ)
{
    int h;

    for( h = 1; h <= 5; h++ )
    {
        int start = occ->run_start[h];
        int end = occ->run_end[h];
        int i;

        for( i = start; i < end; i++ )
        {
            struct SceneOccluder* o = occ->active[i];
            struct SceneOccluderPacked e;
            int j;

            switch( h )
            {
            case OCCLUDER_HIDES_WEST_OF_PLANE:
                e.key = o->min_x;
                e.lo_a = o->min_z;
                e.hi_a = o->max_z;
                e.spread_lo_a = o->spread_min_z;
                e.spread_hi_a = o->spread_max_z;
                e.lo_b = o->min_y;
                e.hi_b = o->max_y;
                e.spread_lo_b = o->spread_min_y;
                e.spread_hi_b = o->spread_max_y;
                break;
            case OCCLUDER_HIDES_EAST_OF_PLANE:
                e.key = -o->min_x;
                e.lo_a = o->min_z;
                e.hi_a = o->max_z;
                e.spread_lo_a = o->spread_min_z;
                e.spread_hi_a = o->spread_max_z;
                e.lo_b = o->min_y;
                e.hi_b = o->max_y;
                e.spread_lo_b = o->spread_min_y;
                e.spread_hi_b = o->spread_max_y;
                break;
            case OCCLUDER_HIDES_SOUTH_OF_PLANE:
                e.key = o->min_z;
                e.lo_a = o->min_x;
                e.hi_a = o->max_x;
                e.spread_lo_a = o->spread_min_x;
                e.spread_hi_a = o->spread_max_x;
                e.lo_b = o->min_y;
                e.hi_b = o->max_y;
                e.spread_lo_b = o->spread_min_y;
                e.spread_hi_b = o->spread_max_y;
                break;
            case OCCLUDER_HIDES_NORTH_OF_PLANE:
                e.key = -o->min_z;
                e.lo_a = o->min_x;
                e.hi_a = o->max_x;
                e.spread_lo_a = o->spread_min_x;
                e.spread_hi_a = o->spread_max_x;
                e.lo_b = o->min_y;
                e.hi_b = o->max_y;
                e.spread_lo_b = o->spread_min_y;
                e.spread_hi_b = o->spread_max_y;
                break;
            default:
                assert(h == OCCLUDER_HIDES_BELOW_PLANE);
                e.key = -o->min_y;
                e.lo_a = o->min_x;
                e.hi_a = o->max_x;
                e.spread_lo_a = o->spread_min_x;
                e.spread_hi_a = o->spread_max_x;
                e.lo_b = o->min_z;
                e.hi_b = o->max_z;
                e.spread_lo_b = o->spread_min_z;
                e.spread_hi_b = o->spread_max_z;
                break;
            }

            for( j = i; j > start && occ->packed[j - 1].key < e.key; j-- )
                occ->packed[j] = occ->packed[j - 1];
            occ->packed[j] = e;
        }
    }
}
#endif

void
scene_occluders_select_for_camera(
    struct SceneOccluders* occ,
    int eye_x,
    int eye_y,
    int eye_z,
    int top_level,
    int draw_distance,
    const struct PaintersCullSpan* span,
    const struct PaintersCullMap* cullmap,
    int camera_pitch,
    int camera_yaw)
{
    int count;
    struct SceneOccluder* occluders;
    int i;
    int camera_sx;
    int camera_sz;
    int tile_x;
    int tile_z;
    int span_extent;

    (void)cullmap;
    (void)camera_pitch;
    (void)camera_yaw;

    assert(occ);
    if( top_level < 0 )
        top_level = 0;
    if( top_level >= occ->levels )
        top_level = occ->levels - 1;

    /* drawDistance mirrors Scene.setDrawDistance (clamped 25..90). */
    if( draw_distance < OCCLUDER_DRAW_DISTANCE_MIN )
        draw_distance = OCCLUDER_DRAW_DISTANCE_MIN;
    if( draw_distance > OCCLUDER_DRAW_DISTANCE_MAX )
        draw_distance = OCCLUDER_DRAW_DISTANCE_MAX;
    span_extent = draw_distance + draw_distance;

    /* Deob keeps cameraX/Y/Z raw for depth/spread and wall side tests; only
     * the camera tile used by the footprint gate is derived from a scene-
     * clamped copy so it lines up with the painter's clamped cam_sx/cam_sz. */
    occ->eye_x = eye_x;
    occ->eye_y = eye_y;
    occ->eye_z = eye_z;

    tile_x = eye_x;
    tile_z = eye_z;
    if( tile_x < 0 )
        tile_x = 0;
    else if( tile_x >= occ->width * 128 )
        tile_x = occ->width * 128 - 1;
    if( tile_z < 0 )
        tile_z = 0;
    else if( tile_z >= occ->height * 128 )
        tile_z = occ->height * 128 - 1;

    camera_sx = tile_x / 128;
    camera_sz = tile_z / 128;
    occ->camera_sx = camera_sx;
    occ->camera_sz = camera_sz;
    occ->frame_no++;
    if( occ->frame_no == 0 )
        occ->frame_no = 1;
    /* Wipe the tile cache? Not needed — the signed-frame trick means a stale
     * +/-frame_no from a previous frame never equals the current frame_no. */

    occ->active_count = 0;
    for( i = 0; i < 6; i++ )
    {
        occ->run_start[i] = 0;
        occ->run_end[i] = 0;
    }

    count = occ->level_occluder_count[top_level];
    occluders = occ->level_occluders[top_level];

    for( i = 0; i < count; i++ )
    {
        struct SceneOccluder* o = &occluders[i];
        int depth;

        if( o->plane == OCCLUDER_PLANE_CONSTANT_X )
        {
            int dx_vis = o->tile_min_x - camera_sx + draw_distance;
            int z0;
            int z1;
            int ok = 0;

            if( dx_vis < 0 || dx_vis > span_extent )
                continue;
            z0 = o->tile_min_z - camera_sz + draw_distance;
            if( z0 < 0 )
                z0 = 0;
            z1 = o->tile_max_z - camera_sz + draw_distance;
            if( z1 > span_extent )
                z1 = span_extent;
            while( z0 <= z1 )
            {
                if( !span || occluder_footprint_tile_visible(
                                 span, dx_vis - draw_distance, z0 - draw_distance) )
                {
                    ok = 1;
                    break;
                }
                z0++;
            }
            if( !ok )
                continue;

            depth = eye_x - o->min_x;
            if( depth > OCCLUDER_CAMERA_DEAD_ZONE )
            {
                o->hides = OCCLUDER_HIDES_WEST_OF_PLANE;
            }
            else
            {
                if( depth >= -OCCLUDER_CAMERA_DEAD_ZONE )
                    continue;
                o->hides = OCCLUDER_HIDES_EAST_OF_PLANE;
                depth = -depth;
            }
            o->spread_min_z = ((o->min_z - eye_z) << 8) / depth;
            o->spread_max_z = ((o->max_z - eye_z) << 8) / depth;
            o->spread_min_y = ((o->min_y - eye_y) << 8) / depth;
            o->spread_max_y = ((o->max_y - eye_y) << 8) / depth;
            occ->active[occ->active_count++] = o;
        }
        else if( o->plane == OCCLUDER_PLANE_CONSTANT_Z )
        {
            int dz_vis = o->tile_min_z - camera_sz + draw_distance;
            int x0;
            int x1;
            int ok = 0;

            if( dz_vis < 0 || dz_vis > span_extent )
                continue;
            x0 = o->tile_min_x - camera_sx + draw_distance;
            if( x0 < 0 )
                x0 = 0;
            x1 = o->tile_max_x - camera_sx + draw_distance;
            if( x1 > span_extent )
                x1 = span_extent;
            while( x0 <= x1 )
            {
                if( !span || occluder_footprint_tile_visible(
                                 span, x0 - draw_distance, dz_vis - draw_distance) )
                {
                    ok = 1;
                    break;
                }
                x0++;
            }
            if( !ok )
                continue;

            depth = eye_z - o->min_z;
            if( depth > OCCLUDER_CAMERA_DEAD_ZONE )
            {
                o->hides = OCCLUDER_HIDES_SOUTH_OF_PLANE;
            }
            else
            {
                if( depth >= -OCCLUDER_CAMERA_DEAD_ZONE )
                    continue;
                o->hides = OCCLUDER_HIDES_NORTH_OF_PLANE;
                depth = -depth;
            }
            o->spread_min_x = ((o->min_x - eye_x) << 8) / depth;
            o->spread_max_x = ((o->max_x - eye_x) << 8) / depth;
            o->spread_min_y = ((o->min_y - eye_y) << 8) / depth;
            o->spread_max_y = ((o->max_y - eye_y) << 8) / depth;
            occ->active[occ->active_count++] = o;
        }
        else if( o->plane == OCCLUDER_PLANE_CONSTANT_Y )
        {
            int z0;
            int z1;
            int x0;
            int x1;
            int ok = 0;
            int x;
            int z;

            depth = o->min_y - eye_y;
            if( depth <= OCCLUDER_ROOF_MIN_CLEARANCE )
                continue;

            z0 = o->tile_min_z - camera_sz + draw_distance;
            if( z0 < 0 )
                z0 = 0;
            z1 = o->tile_max_z - camera_sz + draw_distance;
            if( z1 > span_extent )
                z1 = span_extent;
            if( z0 > z1 )
                continue;
            x0 = o->tile_min_x - camera_sx + draw_distance;
            if( x0 < 0 )
                x0 = 0;
            x1 = o->tile_max_x - camera_sx + draw_distance;
            if( x1 > span_extent )
                x1 = span_extent;

            for( x = x0; x <= x1 && !ok; x++ )
                for( z = z0; z <= z1; z++ )
                    if( !span || occluder_footprint_tile_visible(
                                     span, x - draw_distance, z - draw_distance) )
                    {
                        ok = 1;
                        break;
                    }
            if( !ok )
                continue;

            o->hides = OCCLUDER_HIDES_BELOW_PLANE;
            o->spread_min_x = ((o->min_x - eye_x) << 8) / depth;
            o->spread_max_x = ((o->max_x - eye_x) << 8) / depth;
            o->spread_min_z = ((o->min_z - eye_z) << 8) / depth;
            o->spread_max_z = ((o->max_z - eye_z) << 8) / depth;
            occ->active[occ->active_count++] = o;
        }
    }

    occluder_partition_active(occ);
#if !(defined(TORIDRAW_OCC_REF) && TORIDRAW_OCC_REF)
    /* The A/B baseline must not pay for a table its predicate never reads. */
    occluder_pack_active(occ);
#endif

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
    scene_occluders_census_arm();
    g_scene_occluder_census.frames++;
    g_scene_occluder_census.active_sum += (unsigned)occ->active_count;
    if( (unsigned)occ->active_count > g_scene_occluder_census.active_max )
        g_scene_occluder_census.active_max = (unsigned)occ->active_count;
#endif
}

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
#include <stdio.h>
#include <stdlib.h>

struct SceneOccluderCensus g_scene_occluder_census;
static int g_scene_occluder_census_armed;

static void
scene_occluders_census_dump(void)
{
    const struct SceneOccluderCensus* s = &g_scene_occluder_census;
    const char* path = getenv("TORIDRAW_OCC_CENSUS_FILE");
    FILE* f = path ? fopen(path, "w") : stderr;
    double frames = (double)(s->frames ? s->frames : 1);
    double calls = (double)(s->point_calls ? s->point_calls : 1);
    double tiles = (double)(s->tile_calls ? s->tile_calls : 1);

    assert(f);
    fprintf(f, "occluder census over %llu frames\n", s->frames);
    fprintf(f, "  active set:  mean %.1f, max %u\n",
            (double)s->active_sum / frames, s->active_max);
    fprintf(f, "  point_hidden: %llu calls (%.0f/frame), %llu hit (%.1f%%)\n",
            s->point_calls, calls / frames, s->point_hits,
            100.0 * (double)s->point_hits / calls);
    fprintf(f, "  active set walked (upper bound): %llu (%.1f/call, %.0f/frame)\n",
            s->point_active_sum, (double)s->point_active_sum / calls,
            (double)s->point_active_sum / frames);
    fprintf(f, "  occluders visited: %llu (%.1f/call, %.0f/frame, %.1f%% of the set)\n",
            s->point_visits, (double)s->point_visits / calls,
            (double)s->point_visits / frames,
            100.0 * (double)s->point_visits
                / (double)(s->point_active_sum ? s->point_active_sum : 1));
    fprintf(f, "  past the d>0 gate: %llu (%.1f%% of visits)\n",
            s->point_dpos, 100.0 * (double)s->point_dpos
                / (double)(s->point_visits ? s->point_visits : 1));
    fprintf(f, "  of those, first range axis alone rejected: %llu (%.1f%%)\n",
            s->point_axis_a_reject, 100.0 * (double)s->point_axis_a_reject
                / (double)(s->point_dpos ? s->point_dpos : 1));
    fprintf(f, "  ground_tile_hidden: %llu calls (%.0f/frame), %llu cached (%.1f%%)\n",
            s->tile_calls, (double)s->tile_calls / frames, s->tile_cached,
            100.0 * (double)s->tile_cached / tiles);
    fprintf(f, "  wall_hidden: %llu calls (%.0f/frame)\n",
            s->wall_calls, (double)s->wall_calls / frames);
    if( path )
        fclose(f);
}
void
scene_occluders_census_arm(void)
{
    if( g_scene_occluder_census_armed )
        return;
    g_scene_occluder_census_armed = 1;
    atexit(scene_occluders_census_dump);
}
#endif

static int
tile_cycle_idx(
    const struct SceneOccluders* occ,
    int level,
    int x,
    int z)
{
    return x + z * occ->width + level * occ->width * occ->height;
}

bool
scene_occluders_ground_tile_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z)
{
    int idx;
    int32_t cycle;
    int sx;
    int sz;

    assert(occ);
    if( occ->active_count == 0 )
        return false;
    if( x < 0 || z < 0 || x >= occ->width || z >= occ->height )
        return false;
    if( level < 0 || level >= occ->levels )
        return false;

#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
    g_scene_occluder_census.tile_calls++;
#endif
    idx = tile_cycle_idx(occ, level, x, z);
    cycle = occ->tile_cycle[idx];
    if( cycle == -occ->frame_no || cycle == occ->frame_no )
    {
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
        g_scene_occluder_census.tile_cached++;
#endif
        return cycle == occ->frame_no;
    }

    sx = x << 7;
    sz = z << 7;
    if( scene_occluders_point_hidden(
            occ, sx + 1, scene_occluders_ground_height(occ, x, z, level), sz + 1) &&
        scene_occluders_point_hidden(
            occ,
            sx + 128 - 1,
            scene_occluders_ground_height(occ, x + 1, z, level),
            sz + 1) &&
        scene_occluders_point_hidden(
            occ,
            sx + 128 - 1,
            scene_occluders_ground_height(occ, x + 1, z + 1, level),
            sz + 128 - 1) &&
        scene_occluders_point_hidden(
            occ,
            sx + 1,
            scene_occluders_ground_height(occ, x, z + 1, level),
            sz + 128 - 1) )
    {
        occ->tile_cycle[idx] = occ->frame_no;
        return true;
    }
    occ->tile_cycle[idx] = -occ->frame_no;
    return false;
}

bool
scene_occluders_wall_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z,
    int wall_side)
{
    int scene_x;
    int scene_z;
    int scene_y;
    int y0;
    int y1;
    int y2;

    assert(occ);
    if( occ->active_count == 0 )
        return false;
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
    g_scene_occluder_census.wall_calls++;
#endif
    if( !scene_occluders_ground_tile_hidden(occ, level, x, z) )
        return false;

    scene_x = x << 7;
    scene_z = z << 7;
    scene_y = scene_occluders_ground_height(occ, x, z, level) - 1;
    y0 = scene_y - 120;
    y1 = scene_y - 230;
    y2 = scene_y - 238;

    if( wall_side < 16 )
    {
        if( wall_side == WALL_SIDE_WEST )
        {
            if( scene_x > occ->eye_x )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, scene_y, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(occ, scene_x, scene_y, scene_z + 128) )
                    return false;
            }
            if( level > 0 )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, y0, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(occ, scene_x, y0, scene_z + 128) )
                    return false;
            }
            if( !scene_occluders_point_hidden(occ, scene_x, y1, scene_z) )
                return false;
            return scene_occluders_point_hidden(occ, scene_x, y1, scene_z + 128);
        }
        if( wall_side == WALL_SIDE_NORTH )
        {
            if( scene_z < occ->eye_z )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, scene_y, scene_z + 128) )
                    return false;
                if( !scene_occluders_point_hidden(
                        occ, scene_x + 128, scene_y, scene_z + 128) )
                    return false;
            }
            if( level > 0 )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, y0, scene_z + 128) )
                    return false;
                if( !scene_occluders_point_hidden(
                        occ, scene_x + 128, y0, scene_z + 128) )
                    return false;
            }
            if( !scene_occluders_point_hidden(occ, scene_x, y1, scene_z + 128) )
                return false;
            return scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z + 128);
        }
        if( wall_side == WALL_SIDE_EAST )
        {
            if( scene_x < occ->eye_x )
            {
                if( !scene_occluders_point_hidden(occ, scene_x + 128, scene_y, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(
                        occ, scene_x + 128, scene_y, scene_z + 128) )
                    return false;
            }
            if( level > 0 )
            {
                if( !scene_occluders_point_hidden(occ, scene_x + 128, y0, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(
                        occ, scene_x + 128, y0, scene_z + 128) )
                    return false;
            }
            if( !scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z) )
                return false;
            return scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z + 128);
        }
        if( wall_side == WALL_SIDE_SOUTH )
        {
            if( scene_z > occ->eye_z )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, scene_y, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(occ, scene_x + 128, scene_y, scene_z) )
                    return false;
            }
            if( level > 0 )
            {
                if( !scene_occluders_point_hidden(occ, scene_x, y0, scene_z) )
                    return false;
                if( !scene_occluders_point_hidden(occ, scene_x + 128, y0, scene_z) )
                    return false;
            }
            if( !scene_occluders_point_hidden(occ, scene_x, y1, scene_z) )
                return false;
            return scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z);
        }
    }

    if( !scene_occluders_point_hidden(occ, scene_x + 64, y2, scene_z + 64) )
        return false;
    if( wall_side == WALL_CORNER_NORTHWEST )
        return scene_occluders_point_hidden(occ, scene_x, y1, scene_z + 128);
    if( wall_side == WALL_CORNER_NORTHEAST )
        return scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z + 128);
    if( wall_side == WALL_CORNER_SOUTHEAST )
        return scene_occluders_point_hidden(occ, scene_x + 128, y1, scene_z);
    if( wall_side == WALL_CORNER_SOUTHWEST )
        return scene_occluders_point_hidden(occ, scene_x, y1, scene_z);
    return true;
}

bool
scene_occluders_column_hidden(
    struct SceneOccluders* occ,
    int level,
    int x,
    int z,
    int model_min_y)
{
    int sx;
    int sz;
    assert(occ);
    if( occ->active_count == 0 )
        return false;
#if defined(TORIDRAW_OCC_CENSUS) && TORIDRAW_OCC_CENSUS
    g_scene_occluder_census.wall_calls++;
#endif
    if( !scene_occluders_ground_tile_hidden(occ, level, x, z) )
        return false;
    sx = x << 7;
    sz = z << 7;
    return scene_occluders_point_hidden(
               occ,
               sx + 1,
               scene_occluders_ground_height(occ, x, z, level) - model_min_y,
               sz + 1) &&
           scene_occluders_point_hidden(
               occ,
               sx + 128 - 1,
               scene_occluders_ground_height(occ, x + 1, z, level) - model_min_y,
               sz + 1) &&
           scene_occluders_point_hidden(
               occ,
               sx + 128 - 1,
               scene_occluders_ground_height(occ, x + 1, z + 1, level) - model_min_y,
               sz + 128 - 1) &&
           scene_occluders_point_hidden(
               occ,
               sx + 1,
               scene_occluders_ground_height(occ, x, z + 1, level) - model_min_y,
               sz + 128 - 1);
}

bool
scene_occluders_footprint_hidden(
    struct SceneOccluders* occ,
    int level,
    int sx,
    int sz,
    int size_x,
    int size_z,
    int model_min_y)
{
    int min_x;
    int max_x;
    int min_z;
    int max_z;
    int x;
    int z;
    int px;
    int pz;
    int y0;
    int x1;
    int z1;

    assert(occ);
    if( occ->active_count == 0 )
        return false;

    assert(size_x >= 1);
    assert(size_z >= 1);
    min_x = sx;
    min_z = sz;
    max_x = sx + size_x - 1;
    max_z = sz + size_z - 1;

    if( min_x != max_x || min_z != max_z )
    {
        for( x = min_x; x <= max_x; x++ )
            for( z = min_z; z <= max_z; z++ )
            {
                int idx;
                if( x < 0 || z < 0 || x >= occ->width || z >= occ->height )
                    return false;
                idx = tile_cycle_idx(occ, level, x, z);
                if( occ->tile_cycle[idx] == -occ->frame_no )
                    return false;
            }

        px = (min_x << 7) + 1;
        pz = (min_z << 7) + 2;
        y0 = scene_occluders_ground_height(occ, min_x, min_z, level) - model_min_y;
        if( !scene_occluders_point_hidden(occ, px, y0, pz) )
            return false;
        x1 = (max_x << 7) - 1;
        if( !scene_occluders_point_hidden(occ, x1, y0, pz) )
            return false;
        z1 = (max_z << 7) - 1;
        if( !scene_occluders_point_hidden(occ, px, y0, z1) )
            return false;
        return scene_occluders_point_hidden(occ, x1, y0, z1);
    }

    return scene_occluders_column_hidden(occ, level, min_x, min_z, model_min_y);
}
