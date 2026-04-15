#include "painters.h"

#include "painters_i.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "painters_intqueue.u.c"
#include "painters_cullmap.u.c"
// clang-format on

/* Uncomment to disable all cullmap visibility checks (every tile treated as visible). */
// #define DISABLE_CULLMAPS 1

static inline void
init_painter_tile(
    struct PaintersTile* tile,
    int sx,
    int sz,
    int slevel)
{
    (void)sx;
    (void)sz;
    painters_tile_set_slevel(tile, (uint8_t)slevel);
    painters_tile_set_terrain_slevel(tile, (uint8_t)slevel);
    painters_tile_set_flags(tile, 0);
    tile->spans = 0;
    tile->scenery_head = -1;
    tile->wall_a = -1;
    tile->wall_b = -1;
    tile->wall_decor_a = -1;
    tile->wall_decor_b = -1;
    tile->bridge_tile = -1;
    tile->ground_decor = -1;
    tile->ground_object_bottom = -1;
    tile->ground_object_middle = -1;
    tile->ground_object_top = -1;
}

static inline bool
tile_is_east_inbounds(
    int tile_sx,
    int camera_sx,
    int max_draw_x)
{
    return tile_sx >= camera_sx && tile_sx + 1 < max_draw_x;
}

static inline bool
tile_is_west_inbounds(
    int tile_sx,
    int camera_sx,
    int min_draw_x)
{
    return tile_sx <= camera_sx && tile_sx - 1 >= min_draw_x;
}

static inline bool
tile_is_north_inbounds(
    int tile_sz,
    int camera_sz,
    int max_draw_z)
{
    return tile_sz >= camera_sz && tile_sz + 1 < max_draw_z;
}

static inline bool
tile_is_south_inbounds(
    int tile_sz,
    int camera_sz,
    int min_draw_z)
{
    return tile_sz <= camera_sz && tile_sz - 1 >= min_draw_z;
}

static inline bool
tile_inward_east_inbounds(
    int tile_sx,
    int camera_sx,
    int max_draw_x)
{
    return tile_sx < camera_sx && tile_sx + 1 < max_draw_x;
}

static inline bool
tile_inward_west_inbounds(
    int tile_sx,
    int camera_sx,
    int min_draw_x)
{
    return tile_sx > camera_sx && tile_sx - 1 >= min_draw_x;
}

static inline bool
tile_inward_north_inbounds(
    int tile_sz,
    int camera_sz,
    int max_draw_z)
{
    return tile_sz < camera_sz && tile_sz + 1 < max_draw_z;
}

static inline bool
tile_inward_south_inbounds(
    int tile_sz,
    int camera_sz,
    int min_draw_z)
{
    return tile_sz > camera_sz && tile_sz - 1 >= min_draw_z;
}

/* Mode ctx init/free (defined in painters_*.u.c, included later in this TU). */

static int
bucket_ctx_init(struct Painter* painter);
static void
bucket_ctx_free(struct Painter* painter);
static int
w3d_ctx_init(struct Painter* painter);
static void
w3d_ctx_free(struct Painter* painter);
static int
distmetric_ctx_init(struct Painter* painter);
static void
distmetric_ctx_free(struct Painter* painter);

static struct Painter* s_scenery_sort_painter;
static int s_scenery_sort_camera_sx;
static int s_scenery_sort_camera_sz;

static int
scenery_min_dist_sq_cam(
    const struct PaintersElement* el,
    int cam_sx,
    int cam_sz)
{
    int min_dist_sq = INT_MAX;
    for( int tx = 0; tx < el->_scenery.size_x; tx++ )
    {
        for( int tz = 0; tz < el->_scenery.size_z; tz++ )
        {
            int dx = (el->sx + tx) - cam_sx;
            int dz = (el->sz + tz) - cam_sz;
            int d = dx * dx + dz * dz;
            if( d < min_dist_sq )
                min_dist_sq = d;
        }
    }
    return min_dist_sq;
}

static int
scenery_min_dist_sq(const struct PaintersElement* el)
{
    return scenery_min_dist_sq_cam(el, s_scenery_sort_camera_sx, s_scenery_sort_camera_sz);
}

/* Same ordering as qsort(..., scenery_distance_compare): ascending min corner dist^2. */
static void
scenery_queue_insertion_sort(
    int* queue,
    int len,
    struct Painter* painter,
    int cam_sx,
    int cam_sz)
{
    for( int i = 1; i < len; i++ )
    {
        int val = queue[i];
        int d_val = scenery_min_dist_sq_cam(&painter->elements[val], cam_sx, cam_sz);
        int j = i - 1;
        while( j >= 0 )
        {
            int d_j = scenery_min_dist_sq_cam(&painter->elements[queue[j]], cam_sx, cam_sz);
            if( d_j <= d_val )
                break;
            queue[j + 1] = queue[j];
            j--;
        }
        queue[j + 1] = val;
    }
}

static int
scenery_distance_compare(
    const void* a,
    const void* b)
{
    int elem_a = *(const int*)a;
    int elem_b = *(const int*)b;
    struct PaintersElement* el_a = &s_scenery_sort_painter->elements[elem_a];
    struct PaintersElement* el_b = &s_scenery_sort_painter->elements[elem_b];
    int dist_sq_a = scenery_min_dist_sq(el_a);
    int dist_sq_b = scenery_min_dist_sq(el_b);
    return dist_sq_a - dist_sq_b; /* farthest first (painter's algorithm) */
}

static inline int
painter_push_element(struct Painter* painter)
{
    int element = painter->element_count++;
    if( element >= painter->element_capacity )
    {
        painter->element_capacity *= 2;
        painter->elements =
            realloc(painter->elements, painter->element_capacity * sizeof(struct PaintersElement));

        painter->element_paints = realloc(
            painter->element_paints, painter->element_capacity * sizeof(struct ElementPaint));
    }
    memset(&painter->elements[element], 0, sizeof(struct PaintersElement));
    return element;
}

static inline int
painter_coord_idx(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    assert(sx >= 0);
    assert(sz >= 0);
    assert(slevel >= 0);
    assert(sx < painter->width);
    assert(sz < painter->height);
    assert(slevel < painter->levels);

    return sx + sz * painter->width + slevel * painter->width * painter->height;
}

/* Clears tile_paints only in [min_x,max_x)×[min_z,max_z)×[0,max_level). */
static void
painter_clear_tile_paints_region(
    struct Painter* painter,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int max_level)
{
    for( int s = 0; s < max_level; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            int base = painter_coord_idx(painter, min_draw_x, z, s);
            memset(
                &painter->tile_paints[base],
                0,
                (size_t)(max_draw_x - min_draw_x) * sizeof(struct TilePaint));
        }
    }
}

static inline int
step_idx_east(
    struct Painter* painter,
    int tile_idx)
{
    (void)painter;
    return tile_idx + 1;
}

static inline int
step_idx_west(
    struct Painter* painter,
    int tile_idx)
{
    (void)painter;
    return tile_idx - 1;
}

static inline int
step_idx_north(
    struct Painter* painter,
    int tile_idx)
{
    return tile_idx + painter->width;
}

static inline int
step_idx_south(
    struct Painter* painter,
    int tile_idx)
{
    return tile_idx - painter->width;
}

static inline int
step_idx_up(
    struct Painter* painter,
    int tile_idx)
{
    return tile_idx + painter->width * painter->height;
}

static inline int
step_idx_down(
    struct Painter* painter,
    int tile_idx)
{
    return tile_idx - painter->width * painter->height;
}

static void
scenery_pool_ensure(
    struct Painter* painter,
    int extra)
{
    if( painter->scenery_pool_count + extra <= painter->scenery_pool_capacity )
        return;
    int need = painter->scenery_pool_count + extra;
    int cap = painter->scenery_pool_capacity ? painter->scenery_pool_capacity : 256;
    while( cap < need )
        cap *= 2;
    painter->scenery_pool =
        realloc(painter->scenery_pool, (size_t)cap * sizeof(struct SceneryNode));
    painter->scenery_pool_capacity = cap;
}

static void
scenery_prepend(
    struct Painter* painter,
    struct PaintersTile* tile,
    int16_t element_idx,
    uint8_t span_flags)
{
    scenery_pool_ensure(painter, 1);
    int idx = painter->scenery_pool_count++;
    painter->scenery_pool[idx] = (struct SceneryNode){
        .element_idx = element_idx,
        .span = span_flags,
        .next = tile->scenery_head,
    };
    tile->scenery_head = idx;
    tile->spans |= span_flags;
}

static int32_t
clone_scenery_chain(
    struct Painter* painter,
    int32_t src_head)
{
    if( src_head < 0 )
        return -1;

    int32_t out_head = -1;
    int32_t* tail_next = &out_head;

    for( int32_t cur = src_head; cur != -1; cur = painter->scenery_pool[cur].next )
    {
        scenery_pool_ensure(painter, 1);
        int ni = painter->scenery_pool_count++;
        painter->scenery_pool[ni] = (struct SceneryNode){
            .element_idx = painter->scenery_pool[cur].element_idx,
            .span = painter->scenery_pool[cur].span,
            .next = -1,
        };
        *tail_next = ni;
        tail_next = &painter->scenery_pool[ni].next;
    }
    return out_head;
}

static void
tile_recalculate_spans(
    struct Painter* painter,
    struct PaintersTile* tile)
{
    tile->spans = 0;
    for( int32_t n = tile->scenery_head; n != -1; n = painter->scenery_pool[n].next )
        tile->spans |= painter->scenery_pool[n].span;
}

static void
tile_remove_scenery_element(
    struct Painter* painter,
    struct PaintersTile* tile,
    int element)
{
    int32_t* link = &tile->scenery_head;
    while( *link != -1 )
    {
        struct SceneryNode* node = &painter->scenery_pool[*link];
        if( node->element_idx == element )
        {
            *link = node->next;
            break;
        }
        link = &node->next;
    }
    tile_recalculate_spans(painter, tile);
}

struct Painter*
painter_new(
    int width,
    int height,
    int levels,
    uint32_t init_contexts)
{
    struct Painter* painter = malloc(sizeof(struct Painter));
    memset(painter, 0, sizeof(struct Painter));

    painter->width = width;
    painter->height = height;
    painter->levels = levels;

    int tile_count = width * height * levels;

    painter->tiles = malloc(tile_count * sizeof(struct PaintersTile));
    memset(painter->tiles, 0, tile_count * sizeof(struct PaintersTile));
    painter->tile_count = 0;
    painter->tile_capacity = tile_count;

    painter->scenery_pool = NULL;
    painter->scenery_pool_count = 0;
    painter->scenery_pool_capacity = 0;

    for( int sx = 0; sx < width; sx++ )
    {
        for( int sz = 0; sz < height; sz++ )
            for( int slevel = 0; slevel < levels; slevel++ )
                init_painter_tile(painter_tile_at(painter, sx, sz, slevel), sx, sz, slevel);
    }

    painter->tile_paints = malloc(tile_count * sizeof(struct TilePaint));
    memset(painter->tile_paints, 0, tile_count * sizeof(struct TilePaint));

    painter->elements = malloc(128 * sizeof(struct PaintersElement));
    painter->element_count = 0;
    painter->element_capacity = 128;

    painter->element_paints = malloc(128 * sizeof(struct ElementPaint));
    memset(painter->element_paints, 0, 128 * sizeof(struct ElementPaint));

    painter->bucket_ctx = NULL;
    painter->w3d_ctx = NULL;
    painter->distmetric_ctx = NULL;

    int ctx_ok = 1;
    if( init_contexts & PAINTER_NEW_CTX_BUCKET )
        ctx_ok = bucket_ctx_init(painter) == 0;
    if( ctx_ok && (init_contexts & PAINTER_NEW_CTX_WORLD3D) )
        ctx_ok = w3d_ctx_init(painter) == 0;
    if( ctx_ok && (init_contexts & PAINTER_NEW_CTX_DISTMETRIC) )
        ctx_ok = distmetric_ctx_init(painter) == 0;

    if( !ctx_ok )
    {
        bucket_ctx_free(painter);
        w3d_ctx_free(painter);
        distmetric_ctx_free(painter);
        free(painter->tiles);
        free(painter->scenery_pool);
        free(painter->tile_paints);
        free(painter->elements);
        free(painter->element_paints);
        free(painter);
        return NULL;
    }

    painter->cullmap = NULL;
    painter->camera_pitch = 0;
    painter->camera_yaw = 0;
    painter->level_mask = 0xFu;
    painter->min_level = 0;

    return painter;
}

void
painter_set_level_mask(
    struct Painter* painter,
    uint8_t mask)
{
    if( !painter )
        return;
    painter->level_mask = mask;
    painter->min_level = 0;
    if( mask == 0u )
        return;
    for( int i = 0; i < 8; i++ )
    {
        if( mask & (1u << i) )
        {
            painter->min_level = (uint8_t)i;
            return;
        }
    }
}

void
painter_set_level_range(
    struct Painter* painter,
    int lo,
    int hi)
{
    if( !painter )
        return;
    if( lo > hi )
    {
        int t = lo;
        lo = hi;
        hi = t;
    }
    if( lo < 0 )
        lo = 0;
    if( hi > 7 )
        hi = 7;
    unsigned mask = 0u;
    for( int i = lo; i <= hi; i++ )
        mask |= 1u << (unsigned)i;
    painter_set_level_mask(painter, (uint8_t)mask);
}

static void
painter_cullmap_refresh_indices(struct Painter* painter)
{
    const struct PaintersCullMap* cm = painter->cullmap;
    if( !cm || cm->all_visible )
    {
        painter->cull_pitch_idx = 0;
        painter->cull_yaw_idx = 0;
        return;
    }
    painters_cullmap_indices_from_angles(
        cm,
        painter->camera_pitch,
        painter->camera_yaw,
        &painter->cull_pitch_idx,
        &painter->cull_yaw_idx);
}

static inline bool
painter_cullmap_tile_visible(
    const struct Painter* painter,
    const struct TilePaint* tile_paint,
    int tile_sx,
    int tile_sz,
    int camera_sx,
    int camera_sz)
{
#ifdef DISABLE_CULLMAPS
    (void)painter;
    (void)tile_paint;
    (void)tile_sx;
    (void)tile_sz;
    (void)camera_sx;
    (void)camera_sz;
    return true;
#else
    if( painter->cullmap == NULL || tile_paint->step != PAINT_STEP_READY )
        return true;
    int dx = tile_sx - camera_sx;
    int dz = tile_sz - camera_sz;
    return painters_cullmap_visible(
        painter->cullmap, dx, dz, painter->cull_pitch_idx, painter->cull_yaw_idx);
#endif
}

void
painter_set_cullmap(
    struct Painter* painter,
    struct PaintersCullMap* cm)
{
    if( !painter )
        return;
    painter->cullmap = (const struct PaintersCullMap*)cm;
}

void
painter_set_camera_angles(
    struct Painter* painter,
    int pitch,
    int yaw)
{
    if( !painter )
        return;
    painter->camera_pitch = pitch;
    painter->camera_yaw = yaw;
}

void
painter_free(struct Painter* painter)
{
    if( !painter )
        return;
    free(painter->tiles);
    free(painter->scenery_pool);
    free(painter->tile_paints);
    free(painter->elements);
    free(painter->element_paints);
    bucket_ctx_free(painter);
    w3d_ctx_free(painter);
    distmetric_ctx_free(painter);
    free(painter);
}

int
painter_max_levels(struct Painter* painter)
{
    return painter->levels;
}

struct PaintersTile*
painter_tile_at(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    return &painter->tiles[painter_coord_idx(painter, sx, sz, slevel)];
}

struct PaintersElement*
painter_element_at(
    struct Painter* painter, //
    int element)
{
    assert(element < painter->element_count);
    return &painter->elements[element];
}

void
painter_tile_set_bridge(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int bridge_tile_sx,
    int bridge_tile_sz,
    int bridge_tile_slevel)
{
    if( sx == 34 && sz == 98 )
    {
        printf(
            "bridge_tile_sx: %d, bridge_tile_sz: %d, bridge_tile_slevel: %d\n",
            bridge_tile_sx,
            bridge_tile_sz,
            bridge_tile_slevel);
    }
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    tile->bridge_tile =
        painter_coord_idx(painter, bridge_tile_sx, bridge_tile_sz, bridge_tile_slevel);

    struct PaintersTile* bridge_tile =
        painter_tile_at(painter, bridge_tile_sx, bridge_tile_sz, bridge_tile_slevel);
    painters_tile_or_flags(bridge_tile, PAINTERS_TILE_FLAG_BRIDGE);
}

void
painter_tile_copyto(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int dest_sx,
    int dest_sz,
    int dest_slevel)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    struct PaintersTile* dest_tile = painter_tile_at(painter, dest_sx, dest_sz, dest_slevel);

    *dest_tile = *tile;
    dest_tile->scenery_head = clone_scenery_chain(painter, tile->scenery_head);
}

void
painter_tile_set_draw_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int draw_level)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    assert(draw_level >= 0);
    assert(draw_level < painter->levels);
    painters_tile_set_slevel(tile, (uint8_t)draw_level);
}

void
painter_tile_set_terrain_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int terrain_slevel)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    assert(terrain_slevel >= 0);
    assert(terrain_slevel < painter->levels);
    painters_tile_set_terrain_slevel(tile, (uint8_t)terrain_slevel);
}

static struct TilePaint*
tile_paint_at(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    return &painter->tile_paints[painter_coord_idx(painter, sx, sz, slevel)];
}

static void
compute_normal_scenery_spans(
    struct Painter* painter,
    int loc_x,
    int loc_z,
    int loc_level,
    int size_x,
    int size_z,
    int element)
{
    struct PaintersTile* tile = NULL;
    int min_tile_x = loc_x;
    int min_tile_z = loc_z;

    // The max tile is not actually included in the span
    int max_tile_exclusive_x = loc_x + size_x - 1;
    int max_tile_exclusive_z = loc_z + size_z - 1;
    if( max_tile_exclusive_x > painter->width - 1 )
        max_tile_exclusive_x = painter->width - 1;
    if( max_tile_exclusive_z > painter->height - 1 )
        max_tile_exclusive_z = painter->height - 1;

    for( int x = min_tile_x; x <= max_tile_exclusive_x; x++ )
    {
        for( int z = min_tile_z; z <= max_tile_exclusive_z; z++ )
        {
            int span_flags = 0;
            if( x > min_tile_x )
            {
                // Block until the west tile underlay is drawn.
                span_flags |= SPAN_FLAG_WEST;
            }

            if( x < max_tile_exclusive_x )
            {
                // Block until the east tile underlay is drawn.
                span_flags |= SPAN_FLAG_EAST;
            }

            if( z > min_tile_z )
            {
                // Block until the south tile underlay is drawn.
                span_flags |= SPAN_FLAG_SOUTH;
            }

            if( z < max_tile_exclusive_z )
            {
                // Block until the north tile underlay is drawn.
                span_flags |= SPAN_FLAG_NORTH;
            }

            tile = painter_tile_at(painter, x, z, loc_level);
            scenery_prepend(painter, tile, (int16_t)element, (uint8_t)span_flags);
        }
    }
}

int
painter_add_normal_scenery(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_z)
{
    assert(entity >= 0 && entity < UINT16_MAX);
    int element = painter_push_element(painter);

    compute_normal_scenery_spans(painter, sx, sz, slevel, size_x, size_z, element);

    assert(size_x > 0);
    assert(size_z > 0);
    assert(size_x < 16);
    assert(size_z < 16);
    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_SCENERY,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._scenery = { .entity = entity, .size_x = size_x, .size_z = size_z },
    };
    return element;
}

void
painter_mark_static_count(struct Painter* painter)
{
    painter->static_element_count = painter->element_count;
}

void
painter_reset_to_static(struct Painter* painter)
{
    struct PaintersTile* tile = NULL;
    for( int i = painter->static_element_count; i < painter->element_count; i++ )
    {
        if( painter->elements[i].kind != PNTRELEM_SCENERY )
            continue;

        int size_x = painter->elements[i]._scenery.size_x;
        int size_z = painter->elements[i]._scenery.size_z;

        for( int x = 0; x < size_x; x++ )
        {
            for( int z = 0; z < size_z; z++ )
            {
                tile = painter_tile_at(
                    painter,
                    painter->elements[i].sx + x,
                    painter->elements[i].sz + z,
                    painter->elements[i].slevel);
                tile_remove_scenery_element(painter, tile, i);
            }
        }
    }

    painter->element_count = painter->static_element_count;
}

int
painter_add_wall(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    enum PaintersElementKind kind;
    int element = painter_push_element(painter);

    switch( wall_ab )
    {
    case WALL_A:
        assert(tile->wall_a == -1);
        kind = PNTRELEM_WALL_A;
        tile->wall_a = element;
        break;
    case WALL_B:
        assert(tile->wall_b == -1);
        kind = PNTRELEM_WALL_B;
        tile->wall_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = kind,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._wall = { .entity = entity, .side = side },
    };

    return element;
}

int
painter_add_wall_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int wall_ab,
    int side,
    int through_wall_flags)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    switch( wall_ab )
    {
    case WALL_A:
        assert(tile->wall_decor_a == -1);
        tile->wall_decor_a = element;
        break;
    case WALL_B:
        assert(tile->wall_decor_b == -1);
        tile->wall_decor_b = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_WALL_DECOR,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._wall_decor = { //
            .entity = entity, //
            ._bf_side = side,
            ._bf_through_wall_flags = through_wall_flags,
        },
    };
    return element;
}

int
painter_add_ground_decor(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    assert(tile->ground_decor == -1);
    tile->ground_decor = element;

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_DECOR,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._ground_decor = { .entity = entity },
    };
    return element;
}

int
painter_add_ground_object(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int bottom_middle_top)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    int element = painter_push_element(painter);

    switch( bottom_middle_top )
    {
    case GROUND_OBJECT_BOTTOM:
        assert(tile->ground_object_bottom == -1);
        tile->ground_object_bottom = element;
        break;
    case GROUND_OBJECT_MIDDLE:
        assert(tile->ground_object_middle == -1);
        tile->ground_object_middle = element;
        break;
    case GROUND_OBJECT_TOP:
        assert(tile->ground_object_top == -1);
        tile->ground_object_top = element;
        break;
    default:
        assert(false);
    }

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_OBJECT,
        .sx = sx,
        .sz = sz,
        .slevel = slevel,
        ._ground_object = { .entity = entity },
    };
    return element;
}

/**
 * ****
 * The Painters Algorithm
 * ****
 */

static inline void
ensure_command_capacity(
    struct PaintersBuffer* buffer,
    int count)
{
    if( buffer->command_count + count > buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands = realloc(
            buffer->commands, buffer->command_capacity * sizeof(struct PaintersElementCommand));
    }
}

int g_trap_command = -1;

static inline void
push_command_entity(
    struct PaintersBuffer* buffer,
    int entity)
{
    int count = buffer->command_count;

#if defined(__APPLE__)
    if( count == g_trap_command )
    {
        printf("TRAP: %d\n", count);
        __builtin_debugtrap(); // triggers debugger on macOS/Clang
    }
#endif
    buffer->command_count += 1;
    ensure_command_capacity(buffer, 1);
    buffer->commands[count] = (struct PaintersElementCommand){
        ._entity = {
            ._bf_kind = PNTR_CMD_ELEMENT,
            ._bf_entity = entity,
        },
    };
}

static inline void
push_command_terrain(
    struct PaintersBuffer* buffer,
    int sx,
    int sz,
    int slevel)
{
    int count = buffer->command_count;

#if defined(__APPLE__)
    if( count == g_trap_command )
    {
        printf("TRAP: %d\n", count);
        __builtin_debugtrap(); // triggers debugger on macOS/Clang
    }
#endif

    ensure_command_capacity(buffer, 1);
    buffer->commands[buffer->command_count++] = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN,
            ._bf_terrain_x = sx,
            ._bf_terrain_z = sz,
            ._bf_terrain_y = slevel,
        },
    };
}

static inline int
far_wall_flags(
    int camera_sx,
    int camera_sz,
    int sx,
    int sz)
{
    int flags = 0;

    int camera_is_north = sz < camera_sz;
    int camera_is_east = sx < camera_sx;

    if( camera_is_north )
        flags |= WALL_SIDE_SOUTH | WALL_CORNER_SOUTHEAST | WALL_CORNER_SOUTHWEST;

    if( !camera_is_north )
        flags |= WALL_SIDE_NORTH | WALL_CORNER_NORTHWEST | WALL_CORNER_NORTHEAST;

    if( camera_is_east )
        flags |= WALL_SIDE_WEST | WALL_CORNER_NORTHWEST | WALL_CORNER_SOUTHWEST;

    if( !camera_is_east )
        flags |= WALL_SIDE_EAST | WALL_CORNER_NORTHEAST | WALL_CORNER_SOUTHEAST;

    return flags;
}

struct PaintersBuffer*
painter_buffer_new()
{
    struct PaintersBuffer* buffer = (struct PaintersBuffer*)malloc(sizeof(struct PaintersBuffer));

    memset(buffer, 0x00, sizeof(struct PaintersBuffer));

    buffer->command_capacity = 128;
    buffer->commands = malloc(sizeof(struct PaintersElementCommand) * buffer->command_capacity);
    return buffer;
}

int g_trap_x = -1;
int g_trap_z = -1;

// clang-format off
#include "painters_bucket.u.c"
#include "painters_world3d.u.c"
#include "painters_distancemetric.u.c"
// clang-format on
