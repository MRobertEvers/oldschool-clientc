#include "painters.h"

#include "painters_i.h"
#include "scene_occluders.h"
#include "perf/torirs_perf.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "painters_intqueue.u.c"
#include "painters_cullmap.u.c"
#include "painters_cullspan.u.c"
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
    tile->sx = (uint16_t)sx;
    tile->sz = (uint16_t)sz;
    painters_tile_set_visible_gte_level(tile, (uint8_t)slevel);
    painters_tile_set_paintgrid_level(tile, (uint8_t)slevel);
    painters_tile_set_mesh_level(tile, (uint8_t)slevel);
    painters_tile_set_flags(tile, 0);
    /* Its own mesh and nothing else until the world build says otherwise. */
    tile->terrain_levels = (uint8_t)(1u << (slevel & 3));
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
    return tile_sx <= camera_sx && tile_sx + 1 < max_draw_x;
}

static inline bool
tile_inward_west_inbounds(
    int tile_sx,
    int camera_sx,
    int min_draw_x)
{
    return tile_sx >= camera_sx && tile_sx - 1 >= min_draw_x;
}

static inline bool
tile_inward_north_inbounds(
    int tile_sz,
    int camera_sz,
    int max_draw_z)
{
    return tile_sz <= camera_sz && tile_sz + 1 < max_draw_z;
}

static inline bool
tile_inward_south_inbounds(
    int tile_sz,
    int camera_sz,
    int min_draw_z)
{
    return tile_sz >= camera_sz && tile_sz - 1 >= min_draw_z;
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

/*
 * Append at the tail: every consumer walks scenery_head -> next, so tail insertion is what makes
 * the per-tile draw order equal the add order (locs come in map-file order; dynamics re-added
 * after painter_reset_to_static land after the statics on their tile). Prepending here reversed
 * both. Chains are a handful of nodes, so the walk is cheaper than carrying a tail index in the
 * hot PaintersTile struct that painter_tile_copyto memcpy's wholesale.
 */
static void
scenery_append(
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
        .next = -1,
    };

    if( tile->scenery_head == -1 )
    {
        tile->scenery_head = idx;
    }
    else
    {
        int32_t tail = tile->scenery_head;
        while( painter->scenery_pool[tail].next != -1 )
            tail = painter->scenery_pool[tail].next;
        painter->scenery_pool[tail].next = idx;
    }

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
    painter->cull_camera_key = 0;
    painter->cullspan_active = 0;
    memset(&painter->cullspan, 0, sizeof(painter->cullspan));
    painter->cullspan.empty = 1;
    painter->has_draw_center = 0;
    painter->draw_center_sx = -1;
    painter->draw_center_sz = -1;
    painter->draw_distance = OCCLUDER_DRAW_DISTANCE_MIN;
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
painter_cullmap_refresh_camera_key(struct Painter* painter)
{
    const struct PaintersCullMap* cm = painter->cullmap;
    if( !cm || cm->all_visible )
    {
        painter->cull_camera_key = 0;
        return;
    }
    int pitch_idx;
    int yaw_idx;
    painters_cullmap_indices_from_angles(
        cm, painter->camera_pitch, painter->camera_yaw, &pitch_idx, &yaw_idx);
    painter->cull_camera_key = painters_cullmap_camera_key_from_indices(cm, pitch_idx, yaw_idx);
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
    int dx;
    int dz;
    if( tile_paint->step != PAINT_STEP_READY )
        return true;
    dx = tile_sx - camera_sx;
    dz = tile_sz - camera_sz;
    if( painter->cullspan_active )
        return painters_cullspan_visible(&painter->cullspan, dx, dz) != 0;
    if( painter->cullmap == NULL )
        return true;
    return painters_cullmap_visible_keyed(painter->cullmap, dx, dz, painter->cull_camera_key) != 0;
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
painter_set_cullspan(
    struct Painter* painter,
    const struct PaintersCullSpan* span)
{
    assert(painter);
    if( !span || span->empty )
    {
        painter->cullspan_active = 0;
        painter->cullspan.empty = 1;
        return;
    }
    painter->cullspan = *span;
    painter->cullspan_active = 1;
}

const struct PaintersCullSpan*
painter_get_cullspan(const struct Painter* painter)
{
    assert(painter);
    if( !painter->cullspan_active )
        return NULL;
    return &painter->cullspan;
}

void
painter_set_occluders(
    struct Painter* painter,
    struct SceneOccluders* occ)
{
    assert(painter);
    if( painter->occluders && painter->occluders != occ )
        scene_occluders_free(painter->occluders);
    painter->occluders = occ;
}

struct SceneOccluders*
painter_get_occluders(struct Painter* painter)
{
    assert(painter);
    return painter->occluders;
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
painter_set_draw_center(
    struct Painter* painter,
    int sx,
    int sz)
{
    assert(painter);
    if( sx < 0 || sz < 0 )
    {
        painter->has_draw_center = 0;
        painter->draw_center_sx = -1;
        painter->draw_center_sz = -1;
        return;
    }
    painter->has_draw_center = 1;
    painter->draw_center_sx = sx;
    painter->draw_center_sz = sz;
}

void
painter_set_draw_distance(
    struct Painter* painter,
    int draw_distance)
{
    assert(painter);
    if( draw_distance < OCCLUDER_DRAW_DISTANCE_MIN )
        draw_distance = OCCLUDER_DRAW_DISTANCE_MIN;
    if( draw_distance > OCCLUDER_DRAW_DISTANCE_MAX )
        draw_distance = OCCLUDER_DRAW_DISTANCE_MAX;
    painter->draw_distance = draw_distance;
}

int
painter_get_draw_distance(const struct Painter* painter)
{
    assert(painter);
    return painter->draw_distance;
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
    if( painter->occluders )
        scene_occluders_free(painter->occluders);
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
    /* paintgrid_level follows the destination slot; mesh_level stays from the source (bridge
     * push-down) so push_command_terrain still indexes the correct world terrain mesh. */
    painters_tile_set_paintgrid_level(dest_tile, (uint8_t)dest_slevel);
    dest_tile->sx = (uint16_t)dest_sx;
    dest_tile->sz = (uint16_t)dest_sz;
}

void
painter_tile_set_terrain_levels(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    unsigned levels)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    tile->terrain_levels = (uint8_t)(levels & 0xFu);
}

unsigned
painter_tile_get_terrain_levels(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel)
{
    return painter_tile_at(painter, sx, sz, slevel)->terrain_levels;
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
    painters_tile_set_visible_gte_level(tile, (uint8_t)draw_level);
}

void
painter_tile_set_grid_level(
    struct Painter* painter, //
    int sx,
    int sz,
    int slevel,
    int grid_level)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    assert(grid_level >= 0);
    assert(grid_level < painter->levels);
    painters_tile_set_paintgrid_level(tile, (uint8_t)grid_level);
}

static inline struct TilePaint*
tile_paint_at_idx(
    struct Painter* painter,
    int idx)
{
    return &painter->tile_paints[idx];
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
            scenery_append(painter, tile, (int16_t)element, (uint8_t)span_flags);
        }
    }
}

int
painter_add_normal_scenery_ex(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_z,
    int model_height,
    uint8_t flags)
{
    /* Scene ids are 0..TORIDRAW_SCENE_MAX_ELEMENTS-1 (65535); the command
     * word packs entity in 16 bits, so the full uint16 range is legal. */
    assert(entity >= 0 && entity <= UINT16_MAX);
    /* Loc configs can yield 0 (bad cache/orientation swap); spans require positive footprint. */
    if( size_x < 1 )
        size_x = 1;
    if( size_z < 1 )
        size_z = 1;
    /* Clamping here truncates the footprint the span gate is built from, which
     * lets the loc draw before the ground it covers. Only the field width
     * bounds it now; see struct NormalScenery. */
    if( size_x > PAINTER_SCENERY_MAX_SIZE )
        size_x = PAINTER_SCENERY_MAX_SIZE;
    if( size_z > PAINTER_SCENERY_MAX_SIZE )
        size_z = PAINTER_SCENERY_MAX_SIZE;
    if( model_height < 0 )
        model_height = 0;
    if( model_height > UINT16_MAX )
        model_height = UINT16_MAX;

    int element = painter_push_element(painter);

    compute_normal_scenery_spans(painter, sx, sz, slevel, size_x, size_z, element);

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_SCENERY,
        .sx = sx,
        .sz = sz,
        .source_level = slevel,
        ._scenery = {
            .entity = entity,
            .size_x = size_x,
            .size_z = size_z,
            .model_height = (uint16_t)model_height,
            .flags = flags,
        },
    };
    return element;
}

int
painter_add_normal_scenery(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity,
    int size_x,
    int size_z,
    int model_height)
{
    return painter_add_normal_scenery_ex(
        painter, sx, sz, slevel, entity, size_x, size_z, model_height, 0);
}

void
painter_mark_static_count(struct Painter* painter)
{
    painter->static_element_count = painter->element_count;
}

void
painter_set_suppress_slot_registration(struct Painter* painter, int suppress)
{
    painter->suppress_slot_registration = suppress ? 1 : 0;
}

int
painter_ground_decor_enabled(void)
{
    /* TORIRS_NO_GROUND_DECOR=1: drop every ground-decor emit. A bisection
     * knob — ground decor is a whole class of geometry (shape 22 locs: floor
     * plates, paths, the Inferno's lava floor planes) that is easy to mistake
     * for terrain in a screenshot, and turning it off answers "is that thing
     * decor or floor?" in one frame instead of by pixel attribution. Read
     * once; it changes nothing when unset. */
    static int enabled = -1;
    if( enabled < 0 )
    {
        char const* env = getenv("TORIRS_NO_GROUND_DECOR");
        enabled = (env && env[0] && env[0] != '0') ? 0 : 1;
    }
    return enabled;
}

void
painter_reset_to_static(struct Painter* painter)
{
    struct PaintersTile* tile = NULL;
    for( int i = painter->static_element_count; i < painter->element_count; i++ )
    {
        /* Dynamic walls (runtime-spawned locs re-registered per frame): free
         * the exclusive tile slot so next frame's painter_add_wall re-claims
         * it instead of tripping the wall_a/wall_b == -1 assert. */
        if( painter->elements[i].kind == PNTRELEM_WALL_A ||
            painter->elements[i].kind == PNTRELEM_WALL_B )
        {
            tile = painter_tile_at(
                painter,
                painter->elements[i].sx,
                painter->elements[i].sz,
                painter->elements[i].source_level);
            if( painter->elements[i].kind == PNTRELEM_WALL_A && tile->wall_a == i )
                tile->wall_a = -1;
            if( painter->elements[i].kind == PNTRELEM_WALL_B && tile->wall_b == i )
                tile->wall_b = -1;
            continue;
        }

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
                    painter->elements[i].source_level);
                tile_remove_scenery_element(painter, tile, i);
            }
        }
    }

    painter->element_count = painter->static_element_count;
}

void
painter_release_wall(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, slevel);
    if( tile->wall_a >= 0 && painter->elements[tile->wall_a]._wall.entity == entity )
        tile->wall_a = -1;
    if( tile->wall_b >= 0 && painter->elements[tile->wall_b]._wall.entity == entity )
        tile->wall_b = -1;
}

void
painter_release_scenery(
    struct Painter* painter,
    int sx,
    int sz,
    int slevel,
    int entity)
{
    struct PaintersTile* anchor = painter_tile_at(painter, sx, sz, slevel);
    int matches[8];
    int match_count = 0;

    /* Collect first, unlink after: tile_remove_scenery_element rewrites the
     * chain this walk stands in. The anchor tile is enough to find the element —
     * a loc is registered on every tile of its footprint, so its own tile is
     * always one of them — and the element then names the footprint the unlink
     * has to cover. */
    for( int32_t sn = anchor->scenery_head; sn != -1 && match_count < 8;
         sn = painter->scenery_pool[sn].next )
    {
        int element = painter->scenery_pool[sn].element_idx;
        struct PaintersElement const* el = &painter->elements[element];
        if( el->kind == PNTRELEM_SCENERY && el->_scenery.entity == entity )
            matches[match_count++] = element;
    }

    for( int m = 0; m < match_count; m++ )
    {
        struct PaintersElement const* el = &painter->elements[matches[m]];
        for( int x = 0; x < (int)el->_scenery.size_x; x++ )
        {
            for( int z = 0; z < (int)el->_scenery.size_z; z++ )
            {
                int tx = (int)el->sx + x;
                int tz = (int)el->sz + z;
                if( tx < 0 || tz < 0 || tx >= painter->width || tz >= painter->height )
                    continue;
                tile_remove_scenery_element(
                    painter, painter_tile_at(painter, tx, tz, el->source_level), matches[m]);
            }
        }
    }
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
    struct PaintersTile* tile;
    enum PaintersElementKind kind;
    int element;

    if( painter->suppress_slot_registration )
        return -1;

    tile = painter_tile_at(painter, sx, sz, slevel);
    element = painter_push_element(painter);

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
        .source_level = slevel,
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
    int through_wall_flags,
    int model_height)
{
    struct PaintersTile* tile;
    int element;

    if( painter->suppress_slot_registration )
        return -1;

    if( model_height < 0 )
        model_height = 0;
    if( model_height > UINT16_MAX )
        model_height = UINT16_MAX;

    tile = painter_tile_at(painter, sx, sz, slevel);
    element = painter_push_element(painter);

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
        .source_level = slevel,
        ._wall_decor = { //
            .entity = entity, //
            ._bf_side = side,
            ._bf_through_wall_flags = through_wall_flags,
            .model_height = (uint16_t)model_height,
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
    struct PaintersTile* tile;
    int element;

    if( painter->suppress_slot_registration )
        return -1;

    tile = painter_tile_at(painter, sx, sz, slevel);
    element = painter_push_element(painter);

    assert(tile->ground_decor == -1);
    tile->ground_decor = element;

    painter->elements[element] = (struct PaintersElement){
        .kind = PNTRELEM_GROUND_DECOR,
        .sx = sx,
        .sz = sz,
        .source_level = slevel,
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
        .source_level = slevel,
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

    { /* TEMP diagnostic: trace one element id through the painter. */
        static int trace_init = 0;
        static int trace_id = -1;
        if( !trace_init )
        {
            char const* e = getenv("TORIRS_TRACE_ELEMENT");
            trace_init = 1;
            if( e )
                trace_id = (int)strtol(e, NULL, 0);
        }
        if( entity == trace_id || (trace_id == 0 && entity >= 8000) )
            fprintf(stderr, "trace_element: %d emitted at cmd %d\n", entity, count);
    }

#if defined(__APPLE__) && !defined(NDEBUG)
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

#if defined(__APPLE__) && !defined(NDEBUG)
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

static inline void
push_command_terrain_pick_only(
    struct PaintersBuffer* buffer,
    int sx,
    int sz,
    int slevel)
{
    ensure_command_capacity(buffer, 1);
    buffer->commands[buffer->command_count++] = (struct PaintersElementCommand){
        ._terrain = {
            ._bf_kind = PNTR_CMD_TERRAIN_PICK_ONLY,
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

/*
 * PainterSeedGen — incremental perimeter-seed generator shared by all painters.
 *
 * Replaces the materialized seeds[] + seed_seen[] buffers and the O(L*R^2) upfront
 * build that executed a full-buffer memset on every inner (dx,dz) iteration.
 *
 * Yields (sx, sz, level, phase) in the same order as the original nested loop:
 *   for phase in [1, 2]:
 *     for level in [0, L):
 *       for dx in [-R, 0]:
 *         for dz in [-R, 0]:
 *           up to 4 symmetrically reflected candidates, de-duped by coordinate check
 */
struct PainterSeedGen
{
    int eye_ix, eye_iz;
    int min_draw_x, max_draw_x;
    int min_draw_z, max_draw_z;
    int L, R;
    int phase;  /* 1..2; > 2 means exhausted */
    int level;
    int dx;     /* runs -R..0 */
    int dz;     /* runs -R..0 */
    int sub;    /* 0..3: which of the up-to-4 candidates within (dx,dz) */
};

/** Resolve draw-box centre (orbit target) and clamp a radius-R square to the
 * painter grid. Eye-relative distance / wall logic still uses camera_s*. */
static void
painter_resolve_draw_box(
    const struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int radius,
    int* out_center_sx,
    int* out_center_sz,
    int* out_min_x,
    int* out_max_x,
    int* out_min_z,
    int* out_max_z)
{
    int cx;
    int cz;
    int min_x;
    int max_x;
    int min_z;
    int max_z;
    assert(painter);
    assert(out_center_sx && out_center_sz);
    assert(out_min_x && out_max_x && out_min_z && out_max_z);

    cx = painter->has_draw_center ? painter->draw_center_sx : camera_sx;
    cz = painter->has_draw_center ? painter->draw_center_sz : camera_sz;

    max_x = cx + radius;
    max_z = cz + radius;
    if( max_x >= painter->width )
        max_x = painter->width;
    if( max_z >= painter->height )
        max_z = painter->height;
    if( max_x < 0 )
        max_x = 0;
    if( max_z < 0 )
        max_z = 0;

    min_x = cx - radius;
    min_z = cz - radius;
    if( min_x < 0 )
        min_x = 0;
    if( min_z < 0 )
        min_z = 0;
    if( min_x > painter->width )
        min_x = painter->width;
    if( min_z > painter->height )
        min_z = painter->height;

    *out_center_sx = cx;
    *out_center_sz = cz;
    *out_min_x = min_x;
    *out_max_x = max_x;
    *out_min_z = min_z;
    *out_max_z = max_z;
}

/** Seed radius must cover every tile in the draw box relative to the eye so an
 * offset orbit centre still gets perimeter seeds. */
static int
painter_seed_radius_for_box(
    int eye_sx,
    int eye_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius)
{
    int r = radius;
    int d;
    if( max_draw_x > min_draw_x )
    {
        d = eye_sx - min_draw_x;
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
        d = eye_sx - (max_draw_x - 1);
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
    }
    if( max_draw_z > min_draw_z )
    {
        d = eye_sz - min_draw_z;
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
        d = eye_sz - (max_draw_z - 1);
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
    }
    return r < 1 ? 1 : r;
}

static void
seed_gen_init(
    struct PainterSeedGen* g,
    int eye_ix,
    int eye_iz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int L,
    int radius)
{
    g->eye_ix     = eye_ix;
    g->eye_iz     = eye_iz;
    g->min_draw_x = min_draw_x;
    g->max_draw_x = max_draw_x;
    g->min_draw_z = min_draw_z;
    g->max_draw_z = max_draw_z;
    g->L          = L;
    g->R          = (radius < 1) ? 1 : radius;
    g->phase      = 1;
    g->level      = 0;
    g->dx         = -(g->R);
    g->dz         = -(g->R);
    g->sub        = 0;
}

/*
 * Advance to the next valid seed. Returns 1 and writes (sx,sz,level,phase), or
 * returns 0 when all perimeter seeds have been exhausted.
 */
static int
seed_gen_next(
    struct PainterSeedGen* g,
    int* out_sx,
    int* out_sz,
    int* out_level,
    int* out_phase)
{
    while( g->phase <= 2 )
    {
        int right_x = g->eye_ix + g->dx;
        int left_x  = g->eye_ix - g->dx;
        int fwd_z   = g->eye_iz + g->dz;
        int bwd_z   = g->eye_iz - g->dz;

        while( g->sub < 4 )
        {
            int sub = g->sub++;
            int sx  = (sub < 2) ? right_x : left_x;
            int sz  = (sub & 1) ? bwd_z : fwd_z;

            /* Skip symmetric duplicates (replaces the per-(dx,dz) seed_seen memset). */
            if( sub == 1 && bwd_z == fwd_z ) continue;                         /* dz == 0 */
            if( sub == 2 && left_x == right_x ) continue;                      /* dx == 0 */
            if( sub == 3 && (left_x == right_x || bwd_z == fwd_z) ) continue; /* dx|dz = 0 */

            /* Check against the draw rectangle (equivalent to original per-candidate checks
             * plus the emit_seed tile-bounds guard). */
            if( sx < g->min_draw_x || sx >= g->max_draw_x ) continue;
            if( sz < g->min_draw_z || sz >= g->max_draw_z ) continue;

            *out_sx    = sx;
            *out_sz    = sz;
            *out_level = g->level;
            *out_phase = g->phase;
            return 1;
        }

        /* Advance: dz → dx → level → phase */
        g->sub = 0;
        if( ++g->dz > 0 )
        {
            g->dz = -(g->R);
            if( ++g->dx > 0 )
            {
                g->dx = -(g->R);
                if( ++g->level >= g->L )
                {
                    g->level = 0;
                    g->phase++;
                }
            }
        }
    }
    return 0;
}

/*
 * TORIRS_PAINTER_DUMP=1: print the emitted draw order, grouped by the tile each element sits on,
 * for every tile contributing more than one element. Diagnosing "this loc should be under that
 * one" needs the per-tile sequence and each element's slot, neither of which survives into
 * PaintersElementCommand (it carries only the scene entity id).
 *
 * TORIRS_PAINTER_DUMP_TILE=sx,sz restricts output to a single tile column.
 */
static const char*
painter_element_kind_name(enum PaintersElementKind kind)
{
    switch( kind )
    {
    case PNTRELEM_GROUND: return "GROUND";
    case PNTRELEM_SCENERY: return "SCENERY";
    case PNTRELEM_WALL_A: return "WALL_A";
    case PNTRELEM_WALL_B: return "WALL_B";
    case PNTRELEM_GROUND_DECOR: return "GROUND_DECOR";
    case PNTRELEM_WALL_DECOR: return "WALL_DECOR";
    case PNTRELEM_GROUND_OBJECT: return "GROUND_OBJECT";
    default: return "INVALID";
    }
}

static void
painter_dump_command_order(
    struct Painter* painter,
    struct PaintersBuffer* buffer)
{
    /* Resolved once: this sits in the per-frame paint path. */
    static int enabled = -1;
    static int only_sx = -1;
    static int only_sz = -1;

    if( enabled < 0 )
    {
        char const* tile_env;
        enabled = getenv("TORIRS_PAINTER_DUMP") != NULL;
        tile_env = getenv("TORIRS_PAINTER_DUMP_TILE");
        if( !tile_env || sscanf(tile_env, "%d,%d", &only_sx, &only_sz) != 2 )
        {
            only_sx = -1;
            only_sz = -1;
        }
    }

    if( !enabled )
        return;

    /* entity id -> element index. Elements are few and this only runs when the env is set, so a
     * linear scan per command beats threading the element index through the command encoding. */
    for( int ci = 0; ci < buffer->command_count; ci++ )
    {
        struct PaintersElementCommand* cmd = &buffer->commands[ci];
        if( cmd->_bf_kind != PNTR_CMD_ELEMENT )
            continue;

        for( int ei = 0; ei < painter->element_count; ei++ )
        {
            struct PaintersElement* el = &painter->elements[ei];
            int entity = -1;

            switch( el->kind )
            {
            case PNTRELEM_SCENERY: entity = el->_scenery.entity; break;
            case PNTRELEM_WALL_A:
            case PNTRELEM_WALL_B: entity = el->_wall.entity; break;
            case PNTRELEM_GROUND_DECOR: entity = el->_ground_decor.entity; break;
            case PNTRELEM_WALL_DECOR: entity = el->_wall_decor.entity; break;
            case PNTRELEM_GROUND_OBJECT: entity = el->_ground_object.entity; break;
            default: continue;
            }

            if( entity != (int)cmd->_entity._bf_entity )
                continue;
            if( only_sx >= 0 && (el->sx != (uint16_t)only_sx || el->sz != (uint16_t)only_sz) )
                break;

            printf(
                "PDUMP cmd=%d tile=(%d,%d,L%d) %s entity=%d elem=%d\n",
                ci,
                (int)el->sx,
                (int)el->sz,
                (int)el->source_level,
                painter_element_kind_name(el->kind),
                entity,
                ei);
            break;
        }
    }
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * TORIRS_WEDGELOG=<path> — per-frame DRAW ORDER telemetry.
 *
 * Emits the same 7-column schema as the instrumented official rev-239 client
 * (Deobfuscator/instr/src/WedgeLog.java, hooked into class112):
 *
 *     seq  plane  x  z  drawLevel  renderLevel  what  [extra...]
 *
 *   plane        PaintersTile paintgrid_level — the grid slot the traversal is
 *                walking (official: the plane index of the tile array).
 *   drawLevel    PaintersTile visible_gte_level — the level at or above which
 *                the UI floor reveals this tile (official class112.method4161).
 *   renderLevel  the cache level whose mesh / element is actually drawn
 *                (official class112.method4114). For terrain this is the
 *                emitted mesh level; for elements it is the element's
 *                source_level.
 *   what         MARK / SEED / PUSH / POP for traversal machinery, otherwise a
 *                geometry category using the official's vocabulary
 *                (floor, wall_a, wall_b, wall_back_a, wall_back_b, decor,
 *                decor_alt, decor_back, decor_back_alt, grounddecor, item,
 *                item_back, loc, entity, and the `:bridge` variants).
 *
 * Everything here is read-only telemetry: it never mutates painter, tile or
 * command state, and the whole thing folds away behind one `armed` test when
 * the env var is unset.
 *
 * TORIRS_WEDGELOG_AT=<n>      capture on the n-th painter_paint_bucket call
 *                             (default 700 — the camera must have settled).
 * TORIRS_WEDGELOG_FRAMES=<n>  number of consecutive frames to record (default 1).
 * ------------------------------------------------------------------------ */
struct PainterWedgeLog
{
    FILE* fp;
    int enabled; /* -1 = unresolved */
    int armed;
    int paint_calls;
    int at;
    int frames_left;
    long seq;
    long paints;
    int eye_x;
    int eye_y;
    int eye_z;
    int vp_w;
    int vp_h;
    int eye_valid;
    const struct Painter* painter;
    char path[512];
};

static struct PainterWedgeLog g_wedgelog = { NULL, -1, 0, 0, 700, 1, 0, 0, 0, 0, 0, 0, 0, 0, NULL,
                                             { 0 } };

void
painter_wedgelog_set_eye(
    int eye_x,
    int eye_y,
    int eye_z,
    int viewport_w,
    int viewport_h)
{
    if( g_wedgelog.enabled == 0 )
        return;
    if( g_wedgelog.enabled < 0 )
    {
        char const* p = getenv("TORIRS_WEDGELOG");
        char const* at = getenv("TORIRS_WEDGELOG_AT");
        char const* fr = getenv("TORIRS_WEDGELOG_FRAMES");
        g_wedgelog.enabled = (p && p[0]) ? 1 : 0;
        if( g_wedgelog.enabled )
        {
            snprintf(g_wedgelog.path, sizeof(g_wedgelog.path), "%s", p);
            if( at && at[0] )
                g_wedgelog.at = atoi(at);
            if( fr && fr[0] )
                g_wedgelog.frames_left = atoi(fr);
        }
        if( !g_wedgelog.enabled )
            return;
    }
    g_wedgelog.eye_x = eye_x;
    g_wedgelog.eye_y = eye_y;
    g_wedgelog.eye_z = eye_z;
    g_wedgelog.vp_w = viewport_w;
    g_wedgelog.vp_h = viewport_h;
    g_wedgelog.eye_valid = 1;
}

/** True when this paint call is being recorded. */
static inline int
painter_wedgelog_armed(void)
{
    return g_wedgelog.armed;
}

/* Opens the sink on the requested paint call and writes the `#frame` /
 * `#path` header. Returns non-zero when this frame is being recorded. */
static int
painter_wedgelog_frame_begin(
    const struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int center_sx,
    int center_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius,
    unsigned draw_mask)
{
    g_wedgelog.armed = 0;
    if( g_wedgelog.enabled <= 0 )
        return 0;
    g_wedgelog.paint_calls++;
    if( g_wedgelog.paint_calls < g_wedgelog.at )
        return 0;
    if( g_wedgelog.frames_left <= 0 )
        return 0;
    if( !g_wedgelog.fp )
    {
        g_wedgelog.fp = fopen(g_wedgelog.path, "w");
        if( !g_wedgelog.fp )
        {
            g_wedgelog.enabled = 0;
            return 0;
        }
    }
    g_wedgelog.armed = 1;
    g_wedgelog.painter = painter;
    g_wedgelog.seq = 0;
    g_wedgelog.paints = 0;
    g_wedgelog.frames_left--;

    fprintf(g_wedgelog.fp, "#frame %d\n", g_wedgelog.paint_calls);
    fprintf(
        g_wedgelog.fp,
        "#path bucket:painter_paint_bucket dims=%dx%d planes=%d camTile=%d,%d cam=%d,%d,%d "
        "drawCenter=%d,%d window=x[%d,%d)z[%d,%d) drawDist=%d levelMask=0x%x minLevel=%d "
        "vp=%dx%d pitch=%d yaw=%d cullspan=%d occluders=%d\n",
        painter->width,
        painter->height,
        painter->levels,
        camera_sx,
        camera_sz,
        g_wedgelog.eye_x,
        g_wedgelog.eye_y,
        g_wedgelog.eye_z,
        center_sx,
        center_sz,
        min_draw_x,
        max_draw_x,
        min_draw_z,
        max_draw_z,
        radius,
        draw_mask,
        painter->min_level,
        g_wedgelog.vp_w,
        g_wedgelog.vp_h,
        painter->camera_pitch,
        painter->camera_yaw,
        painter->cullspan_active,
        painter->occluders ? painter->occluders->active_count : -1);
    return 1;
}

static void
painter_wedgelog_frame_end(long command_count)
{
    if( !g_wedgelog.armed )
        return;
    fprintf(
        g_wedgelog.fp,
        "#endframe seq=%ld paints=%ld commands=%ld\n",
        g_wedgelog.seq,
        g_wedgelog.paints,
        command_count);
    fflush(g_wedgelog.fp);
    g_wedgelog.armed = 0;
    if( g_wedgelog.frames_left <= 0 )
    {
        fclose(g_wedgelog.fp);
        g_wedgelog.fp = NULL;
        g_wedgelog.enabled = 0;
    }
}

/** Traversal machinery row: `seq plane x z - - KIND extra`. */
static void
painter_wedgelog_event(
    int ti,
    const char* kind,
    const char* extra)
{
    const struct PaintersTile* t;
    if( !g_wedgelog.armed )
        return;
    t = &g_wedgelog.painter->tiles[ti];
    fprintf(
        g_wedgelog.fp,
        "%ld %d %d %d - - %s%s%s\n",
        ++g_wedgelog.seq,
        (int)painters_tile_get_paintgrid_level(t),
        (int)t->sx,
        (int)t->sz,
        kind,
        extra ? " " : "",
        extra ? extra : "");
}

/** Geometry row: `seq plane x z drawLevel renderLevel what p=<paint#> ...`. */
static void
painter_wedgelog_paint(
    int ti,
    const char* what,
    int render_level,
    int entity,
    int element_idx)
{
    const struct PaintersTile* t;
    if( !g_wedgelog.armed )
        return;
    t = &g_wedgelog.painter->tiles[ti];
    fprintf(
        g_wedgelog.fp,
        "%ld %d %d %d %d %d %s p=%ld ent=%d elem=%d spans=0x%x flags=0x%x\n",
        ++g_wedgelog.seq,
        (int)painters_tile_get_paintgrid_level(t),
        (int)t->sx,
        (int)t->sz,
        (int)painters_tile_get_visible_gte_level(t),
        render_level,
        what,
        ++g_wedgelog.paints,
        entity,
        element_idx,
        (unsigned)t->spans,
        (unsigned)painters_tile_get_flags(t));
}

// clang-format off
#include "painters_bucket.u.c"
#include "painters_world3d.u.c"
#include "painters_distancemetric.u.c"
// clang-format on
