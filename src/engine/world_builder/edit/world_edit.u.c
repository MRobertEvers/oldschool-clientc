#ifndef WORLD_EDIT_U_C
#define WORLD_EDIT_U_C

/*
 * The EDIT world layout. See edit/world_edit.h for what it is and its contract.
 *
 * A fragment of world_builder.c's translation unit (included at its end), so it
 * calls the bake's own static helpers -- world_terrain_apply_floor,
 * world_terrain_build_tile, scenery_add, the collision stampers, the sharelight
 * merge -- rather than a second copy of any of them. What a local edit cannot
 * borrow from the bake is spelled here, and each such piece names the bake code
 * it must agree with; edit_world_test is what holds them to it.
 *
 * Vocabulary:
 *
 *   placement   one map loc, in map order: (square index in the window's bake
 *               order, sequence within the square). loc_place appends to its
 *               square, WorldEdit_LocPlaceBefore inserts ahead of a placement
 *               (the document inserts in cache order, so an undone delete lands
 *               mid-list), removals keep the rest in order -- exactly what the
 *               editor's document does to the square's loc list, so EDIT's
 *               order and a fresh bake's agree.
 *   record      what a placement did to the build maps while it was placed:
 *               the shadows it cast, the wall offset it set, the decor it hung,
 *               what lights each element, the elements themselves. Kept so a
 *               neighbourhood can be re-derived from the placements that are
 *               left when one goes.
 *   victim      a placement a flush takes down and places again through the
 *               bake's path: a loc that was added, one standing on or contoured
 *               over a tile whose heights moved, wall decor whose wall changed,
 *               and every share-lit neighbour of any of those.
 */

#include "world_edit.h"
#include "world_edit_hooks.h"

#include "osrs/palette.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WE_HANDLE_INDEX_BITS 20
#define WE_HANDLE_INDEX_MASK ((1 << WE_HANDLE_INDEX_BITS) - 1)
#define WE_GENERATION_MASK 0x7FF

/* Sunlight constants of lightmap.c's lightmap_build, which a local relight has
 * to reproduce corner for corner (world_edit_raw_light). */
#define WE_LIGHT_DIR_X -50
#define WE_LIGHT_DIR_Y -10
#define WE_LIGHT_DIR_Z -50
#define WE_LIGHT_AMBIENT 96
#define WE_LIGHT_ATTENUATION 768
#define WE_LIGHT_HEIGHT_SCALE 65536

/* blendmap.c's BLEND_RADIUS. Its running sums add the column/row R ahead and
 * drop the one R behind before reading a tile, so a tile averages the window
 * [x - R + 1, x + R] x [z - R + 1, z + R]: 10x10, one tile heavier to +x/+z. */
#define WE_BLEND_RADIUS 5

/* Contour sampling and a placement's footprint corners reach past the tiles
 * the placement stands on; a height change this many tiles from a footprint
 * re-places it. Over-inclusion costs a re-place, under-inclusion a wrong
 * model, so this errs wide. */
#define WE_HEIGHT_REACH 2

enum WorldEditRecType
{
    WE_REC_SHADE = 1,
    WE_REC_WALL_OFFSET = 2,
    WE_REC_DECOR = 3,
    WE_REC_LIGHT = 4,
    WE_REC_ELEMENT = 5,
};

struct WorldEditRec
{
    uint8_t type;
    int8_t level;
    /* SHADE: enum WorldEditShadeKind. DECOR: orientation. LIGHT: sharelight. */
    int8_t k;
    /* DECOR: displacement kind. */
    int8_t k2;
    int16_t x;
    int16_t z;
    int32_t element_id;
    /* SHADE: a, b, shade. WALL_OFFSET: offset. LIGHT: size_x, size_z, ambient,
     * contrast. */
    int32_t v[4];
};

enum WorldEditPlacementState
{
    /** In the scene as the map has it (or placing nothing: hidden by its
     *  multiloc, or no config). */
    WE_P_LIVE = 0,
    /** Placed by loc_place, built at the next flush. */
    WE_P_ADD = 1,
    /** Removed by loc_remove, taken down at the next flush. */
    WE_P_REMOVE = 2,
    /** Gone; the slot keeps its generation so an old handle stays dead. */
    WE_P_DEAD = 3,
};

struct WorldEditPlacement
{
    struct World_LocPlacement p;
    int square;
    int seq;
    int generation;
    int state;
    /** On a scene tile, so in the tile index (a centrezone bake records the
     *  out-of-scene locs of its edge squares too, to keep the order). */
    int in_scene;
    int next_in_tile;
    int rec_first;
    int rec_count;
    /** Flush epoch this placement joined the victim set in. */
    uint32_t mark;
};

struct WorldEditSquare
{
    int map_x;
    int map_z;
    int first_placement;
    int bake_count;
    int next_seq;
    /** One of the window's squares (not merely named by a loc_place). */
    int in_bake;
};

enum WorldEditDirty
{
    WE_D_CORNER = 1, /* the height at this corner moved */
    WE_D_MESH = 2, /* the tile remeshes */
    WE_D_BLEND = 4, /* the tile's underlay colour changed */
    WE_D_FLAGS = 8, /* the tile's settings changed */
    WE_D_BLOCK = 16, /* ... in the BLOCK bit: collision */
    WE_D_LIGHT = 32, /* the corner relights */
    WE_D_SHADE = 64, /* the tile's shade is replayed */
    WE_D_COLL = 128, /* the tile's collision is restamped */
    WE_D_MMCOL = 256, /* a bridge column's minimap is rebuilt and pushed down */
};

struct WorldEditHslMemo
{
    uint32_t rgb;
    int used;
    struct HSL hsl;
};

#define WE_HSL_MEMO 1024

struct WorldEdit
{
    int ready;
    int instance;
    uint32_t bake_serial;
    int scene_size;
    int tile_count; /* scene_size^2 * levels */

    struct ToriRS_MapFloor* floors;
    int* tile_head;

    struct WorldEditSquare* squares;
    int square_count;
    int square_cap;

    struct WorldEditPlacement* placements;
    int placement_count;
    int placement_cap;

    struct WorldEditRec* recs;
    int rec_count;
    int rec_cap;

    int capture;
    int generation_counter;
    /** Largest footprint side among the placements, and among the share-lit
     *  ones: how far a neighbour search has to look. */
    int max_size;
    int max_share_size;

    uint16_t* dirty;
    int* dirty_list;
    int dirty_count;
    int dirty_cap;

    /** Placements loc_place / loc_remove touched since the last flush. */
    int* changed;
    int changed_count;
    int changed_cap;

    int link_below_changed;
    int pending_rebuild;
    int tiles_set;
    uint32_t epoch;
    int occluders_dropped;

    int* victims;
    int victim_count;
    int victim_cap;

    struct WorldEditFlushStats stats;
    struct WorldEditHslMemo hsl_memo[WE_HSL_MEMO];
};

/* ---- small helpers ------------------------------------------------------ */

static void*
we_grow(
    void* array,
    int* cap,
    int want,
    size_t elem)
{
    int next;

    if( want <= *cap )
        return array;
    next = *cap ? *cap : 64;
    while( next < want )
        next *= 2;
    array = ToriMem_Realloc(ToriMem_Scene(), array, (size_t)next * elem);
    assert(array);
    *cap = next;
    return array;
}

static inline int
we_tile_idx(
    const struct WorldEdit* e,
    int x,
    int z,
    int level)
{
    return x + z * e->scene_size + level * e->scene_size * e->scene_size;
}

static inline int
we_in_scene(
    const struct WorldEdit* e,
    int x,
    int z)
{
    return x >= 0 && z >= 0 && x < e->scene_size && z < e->scene_size;
}

static inline int
we_make_handle(
    const struct WorldEdit* e,
    int index)
{
    return (e->placements[index].generation << WE_HANDLE_INDEX_BITS) | index;
}

/* The placement a handle names, or -1 when it names nothing that exists. */
static int
we_handle_index(
    const struct WorldEdit* e,
    int handle)
{
    int index;

    if( handle < 0 )
        return -1;
    index = handle & WE_HANDLE_INDEX_MASK;
    if( index >= e->placement_count )
        return -1;
    if( e->placements[index].generation != (handle >> WE_HANDLE_INDEX_BITS) )
        return -1;
    return index;
}

static void
we_mark(
    struct WorldEdit* e,
    int x,
    int z,
    int level,
    int bits)
{
    int t;

    if( !we_in_scene(e, x, z) || level < 0 || level >= WORLD_MAP_TERRAIN_LEVELS )
        return;
    t = we_tile_idx(e, x, z, level);
    if( e->dirty[t] == 0 )
    {
        e->dirty_list = we_grow(e->dirty_list, &e->dirty_cap, e->dirty_count + 1, sizeof(int));
        e->dirty_list[e->dirty_count++] = t;
    }
    e->dirty[t] |= (uint16_t)bits;
}

static void
we_tile_of(
    const struct WorldEdit* e,
    int t,
    int* x,
    int* z,
    int* level)
{
    int plane = e->scene_size * e->scene_size;
    *level = t / plane;
    *z = (t % plane) / e->scene_size;
    *x = t % e->scene_size;
}

/* A loc's footprint as placed: its config size, swapped for an odd angle. */
static void
we_footprint(
    struct WorldBuilder* builder,
    const struct World_LocPlacement* p,
    int* size_x,
    int* size_z)
{
    struct ToriRS_Location* cfg = CacheProvider_LocationGet(builder->cache, p->loc_id);
    int sx = cfg && cfg->size_x > 0 ? cfg->size_x : 1;
    int sz = cfg && cfg->size_z > 0 ? cfg->size_z : 1;

    if( (p->angle & 1) != 0 )
    {
        int t = sx;
        sx = sz;
        sz = t;
    }
    *size_x = sx;
    *size_z = sz;
}

static void
we_link(
    struct WorldEdit* e,
    int index)
{
    struct WorldEditPlacement* pl = &e->placements[index];
    int t;

    if( !pl->in_scene )
        return;
    t = we_tile_idx(e, pl->p.scene_x, pl->p.scene_z, pl->p.level);
    pl->next_in_tile = e->tile_head[t];
    e->tile_head[t] = index;
}

static void
we_unlink(
    struct WorldEdit* e,
    int index)
{
    struct WorldEditPlacement* pl = &e->placements[index];
    int t;
    int* link;

    if( !pl->in_scene )
        return;
    t = we_tile_idx(e, pl->p.scene_x, pl->p.scene_z, pl->p.level);
    for( link = &e->tile_head[t]; *link != -1; link = &e->placements[*link].next_in_tile )
    {
        if( *link == index )
        {
            *link = pl->next_in_tile;
            pl->next_in_tile = -1;
            return;
        }
    }
    assert(0 && "world_edit: placement missing from its tile chain");
}

static int
we_order_cmp(
    const struct WorldEdit* e,
    int a,
    int b)
{
    const struct WorldEditPlacement* pa = &e->placements[a];
    const struct WorldEditPlacement* pb = &e->placements[b];

    if( pa->square != pb->square )
        return pa->square < pb->square ? -1 : 1;
    if( pa->seq != pb->seq )
        return pa->seq < pb->seq ? -1 : 1;
    return 0;
}

/* Insertion sort by map order: victim and replay sets are small. */
static void
we_sort_order(
    const struct WorldEdit* e,
    int* list,
    int count)
{
    for( int i = 1; i < count; i++ )
    {
        int v = list[i];
        int j = i - 1;
        while( j >= 0 && we_order_cmp(e, list[j], v) > 0 )
        {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = v;
    }
}

static int
we_square_index(
    struct WorldEdit* e,
    int map_x,
    int map_z)
{
    for( int i = 0; i < e->square_count; i++ )
        if( e->squares[i].map_x == map_x && e->squares[i].map_z == map_z )
            return i;
    return -1;
}

static int
we_square_add(
    struct WorldEdit* e,
    int map_x,
    int map_z)
{
    int i = we_square_index(e, map_x, map_z);
    if( i >= 0 )
        return i;
    e->squares = we_grow(e->squares, &e->square_cap, e->square_count + 1, sizeof(*e->squares));
    i = e->square_count++;
    e->squares[i].map_x = map_x;
    e->squares[i].map_z = map_z;
    e->squares[i].first_placement = -1;
    e->squares[i].bake_count = 0;
    e->squares[i].next_seq = 0;
    e->squares[i].in_bake = 0;
    return i;
}

static int
we_placement_new(
    struct WorldEdit* e,
    const struct World_LocPlacement* p,
    int square,
    int state)
{
    int index;
    struct WorldEditPlacement* pl;

    assert(e->placement_count < WE_HANDLE_INDEX_MASK);
    e->placements = we_grow(
        e->placements, &e->placement_cap, e->placement_count + 1, sizeof(*e->placements));
    index = e->placement_count++;
    pl = &e->placements[index];
    memset(pl, 0, sizeof(*pl));
    pl->p = *p;
    pl->square = square;
    pl->seq = e->squares[square].next_seq++;
    e->generation_counter = (e->generation_counter + 1) & WE_GENERATION_MASK;
    if( e->generation_counter == 0 )
        e->generation_counter = 1;
    pl->generation = e->generation_counter;
    pl->state = state;
    pl->in_scene = we_in_scene(e, p->scene_x, p->scene_z) && p->level >= 0 &&
                   p->level < WORLD_MAP_TERRAIN_LEVELS;
    pl->next_in_tile = -1;
    pl->rec_first = e->rec_count;
    pl->rec_count = 0;
    we_link(e, index);
    return index;
}

static struct WorldEditRec*
we_rec_new(
    struct WorldEdit* e,
    int type)
{
    struct WorldEditRec* r;

    assert(e->capture >= 0);
    e->recs = we_grow(e->recs, &e->rec_cap, e->rec_count + 1, sizeof(*e->recs));
    r = &e->recs[e->rec_count++];
    memset(r, 0, sizeof(*r));
    r->type = (uint8_t)type;
    /* A placement's records are contiguous: nothing else captures while it is
     * the capture target. */
    assert(
        e->placements[e->capture].rec_first + e->placements[e->capture].rec_count ==
        e->rec_count - 1);
    e->placements[e->capture].rec_count++;
    return r;
}

static int
we_has_light_share(
    const struct WorldEdit* e,
    int index)
{
    const struct WorldEditPlacement* pl = &e->placements[index];
    for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
        if( e->recs[r].type == WE_REC_LIGHT && e->recs[r].k )
            return 1;
    return 0;
}

static struct WorldEdit*
we_get(struct WorldBuilder* builder)
{
    assert(builder);
    assert(builder->edit);
    return builder->edit;
}

/* ---- hooks the bake calls (edit/world_edit_hooks.h) --------------------- */

static void
world_edit_on_bake_begin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    const int* chunks_xz,
    int chunk_count)
{
    struct WorldEdit* e = we_get(builder);
    struct World* world = builder->world;
    int scene_size = world->_scene_size;
    int tiles = scene_size * scene_size * WORLD_MAP_TERRAIN_LEVELS;

    (void)zone_center_x;
    (void)zone_center_z;

    if( tiles != e->tile_count )
    {
        ToriMem_Free(ToriMem_Scene(), e->floors);
        ToriMem_Free(ToriMem_Scene(), e->tile_head);
        ToriMem_Free(ToriMem_Scene(), e->dirty);
        e->floors = ToriMem_Alloc(ToriMem_Scene(), (size_t)tiles * sizeof(*e->floors));
        assert(e->floors);
        e->tile_head = ToriMem_Alloc(ToriMem_Scene(), (size_t)tiles * sizeof(*e->tile_head));
        assert(e->tile_head);
        e->dirty = ToriMem_Alloc(ToriMem_Scene(), (size_t)tiles * sizeof(*e->dirty));
        assert(e->dirty);
        e->tile_count = tiles;
    }
    e->scene_size = scene_size;
    memset(e->floors, 0, (size_t)tiles * sizeof(*e->floors));
    for( int i = 0; i < tiles; i++ )
        e->tile_head[i] = -1;
    memset(e->dirty, 0, (size_t)tiles * sizeof(*e->dirty));

    e->ready = 0;
    e->instance = 0;
    e->square_count = 0;
    e->placement_count = 0;
    e->rec_count = 0;
    e->capture = -1;
    e->max_size = 1;
    e->max_share_size = 1;
    e->dirty_count = 0;
    e->changed_count = 0;
    e->link_below_changed = 0;
    e->pending_rebuild = 0;
    e->tiles_set = 0;
    e->occluders_dropped = 0;
    memset(&e->stats, 0, sizeof(e->stats));

    /* The window's squares in the order the scenery pass walks them -- the
     * first half of every placement's order key. */
    if( chunks_xz )
    {
        for( int i = 0; i < chunk_count; i++ )
        {
            /* Index first: the add may move the array. */
            int square = we_square_add(e, chunks_xz[i * 2], chunks_xz[i * 2 + 1]);
            e->squares[square].in_bake = 1;
        }
    }
    else
    {
        for( int mapx = world->_chunk_sw_x; mapx <= world->_chunk_ne_x; mapx++ )
            for( int mapz = world->_chunk_sw_z; mapz <= world->_chunk_ne_z; mapz++ )
            {
                int square = we_square_add(e, mapx, mapz);
                e->squares[square].in_bake = 1;
            }
    }
}

static void
world_edit_on_bake_instance(struct WorldBuilder* builder)
{
    we_get(builder)->instance = 1;
}

static void
world_edit_note_bake_floor(
    struct WorldBuilder* builder,
    const struct ToriRS_MapFloor* floor,
    int scene_x,
    int scene_z,
    int level)
{
    struct WorldEdit* e = we_get(builder);

    assert(floor);
    if( !we_in_scene(e, scene_x, scene_z) )
        return;
    e->floors[we_tile_idx(e, scene_x, scene_z, level)] = *floor;
}

static void
world_edit_bake_placement(
    struct WorldBuilder* builder,
    int mapx,
    int mapz,
    const struct ToriRS_MapLoc* map_loc)
{
    struct WorldEdit* e = we_get(builder);
    struct World* world = builder->world;
    struct World_LocPlacement p;
    int square;
    int index;

    assert(map_loc);
    square = we_square_add(e, mapx, mapz);
    p.scene_x = World_ToSceneX(world, mapx, map_loc->chunk_pos_x);
    p.scene_z = World_ToSceneZ(world, mapz, map_loc->chunk_pos_z);
    p.level = map_loc->chunk_pos_level;
    p.loc_id = map_loc->loc_id;
    p.shape = map_loc->shape_select;
    p.angle = map_loc->orientation;
    index = we_placement_new(e, &p, square, WE_P_LIVE);
    if( e->squares[square].first_placement < 0 )
        e->squares[square].first_placement = index;
    e->squares[square].bake_count++;
    e->capture = index;
}

static void
world_edit_on_bake_end(struct WorldBuilder* builder)
{
    struct WorldEdit* e = we_get(builder);

    e->capture = -1;
    for( int i = 0; i < e->placement_count; i++ )
    {
        int sx;
        int sz;
        we_footprint(builder, &e->placements[i].p, &sx, &sz);
        if( sx > e->max_size )
            e->max_size = sx;
        if( sz > e->max_size )
            e->max_size = sz;
    }
    for( int r = 0; r < e->rec_count; r++ )
    {
        if( e->recs[r].type != WE_REC_LIGHT || !e->recs[r].k )
            continue;
        if( e->recs[r].v[0] > e->max_share_size )
            e->max_share_size = e->recs[r].v[0];
        if( e->recs[r].v[1] > e->max_share_size )
            e->max_share_size = e->recs[r].v[1];
    }
    e->ready = !e->instance;
    e->bake_serial++;
}

static void
world_edit_capture_shade(
    struct WorldBuilder* builder,
    int kind,
    int x,
    int z,
    int level,
    int a,
    int b,
    int shade)
{
    struct WorldEdit* e = we_get(builder);
    struct WorldEditRec* r;

    if( e->capture < 0 )
        return;
    r = we_rec_new(e, WE_REC_SHADE);
    r->k = (int8_t)kind;
    r->x = (int16_t)x;
    r->z = (int16_t)z;
    r->level = (int8_t)level;
    r->v[0] = a;
    r->v[1] = b;
    r->v[2] = shade;
}

static void
world_edit_capture_wall_offset(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int offset)
{
    struct WorldEdit* e = we_get(builder);
    struct WorldEditRec* r;

    if( e->capture < 0 )
        return;
    r = we_rec_new(e, WE_REC_WALL_OFFSET);
    r->x = (int16_t)x;
    r->z = (int16_t)z;
    r->level = (int8_t)level;
    r->v[0] = offset;
}

static void
world_edit_capture_decor(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int element_id,
    int orientation,
    int displacement_kind)
{
    struct WorldEdit* e = we_get(builder);
    struct WorldEditRec* r;

    if( e->capture < 0 )
        return;
    r = we_rec_new(e, WE_REC_DECOR);
    r->x = (int16_t)x;
    r->z = (int16_t)z;
    r->level = (int8_t)level;
    r->element_id = element_id;
    r->k = (int8_t)orientation;
    r->k2 = (int8_t)displacement_kind;
}

static void
world_edit_capture_light(
    struct WorldBuilder* builder,
    int sharelight,
    int x,
    int z,
    int level,
    int element_id,
    int size_x,
    int size_z,
    int ambient,
    int contrast)
{
    struct WorldEdit* e = we_get(builder);
    struct WorldEditRec* r;

    if( e->capture < 0 )
        return;
    r = we_rec_new(e, WE_REC_LIGHT);
    r->k = (int8_t)(sharelight != 0);
    r->x = (int16_t)x;
    r->z = (int16_t)z;
    r->level = (int8_t)level;
    r->element_id = element_id;
    r->v[0] = size_x;
    r->v[1] = size_z;
    r->v[2] = ambient;
    r->v[3] = contrast;
}

static void
world_edit_capture_element(
    struct WorldBuilder* builder,
    int element_id)
{
    struct WorldEdit* e = we_get(builder);
    struct WorldEditRec* r;

    if( e->capture < 0 )
        return;
    r = we_rec_new(e, WE_REC_ELEMENT);
    r->element_id = element_id;
}

static void
world_edit_stash_maps(
    struct WorldBuilder* builder,
    struct WorldEditStash* stash)
{
    struct WorldEdit* e = we_get(builder);

    assert(stash);
    stash->maps[0] = builder->shademap;
    stash->maps[1] = builder->decor_buildmap;
    stash->maps[2] = builder->sharelight_map;
    stash->maps[3] = builder->occluder_buildmap;
    stash->maps[4] = (void*)(intptr_t)e->capture;
    builder->shademap = NULL;
    builder->decor_buildmap = NULL;
    builder->sharelight_map = NULL;
    builder->occluder_buildmap = NULL;
    e->capture = -1;
}

static void
world_edit_unstash_maps(
    struct WorldBuilder* builder,
    const struct WorldEditStash* stash)
{
    struct WorldEdit* e = we_get(builder);

    assert(stash);
    builder->shademap = stash->maps[0];
    builder->decor_buildmap = stash->maps[1];
    builder->sharelight_map = stash->maps[2];
    builder->occluder_buildmap = stash->maps[3];
    e->capture = (int)(intptr_t)stash->maps[4];
}

/* ---- tile_set ----------------------------------------------------------- */

/* overlaymap.c's init_tile: the state an untouched overlay cell starts in. */
static void
we_overlay_tile_reset(struct OverlaymapTile* tile)
{
    memset(tile, 0, sizeof(*tile));
    tile->minimap_rgb_color = UINT32_MAX;
    tile->rgb_color = UINT32_MAX;
    tile->secondary_rgb_color = -1;
    tile->texture_id = -1;
    tile->texture_avg_hsl16 = UINT16_MAX;
}

static void
world_edit_tile_set(
    struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    const struct ToriRS_MapFloor* floor)
{
    struct WorldEdit* e = we_get(builder);
    struct World* world = builder->world;
    struct ToriRS_MapFloor* old;
    int t;

    assert(floor);
    assert(level >= 0);
    assert(level < WORLD_MAP_TERRAIN_LEVELS);
    if( !e->ready )
    {
        /* Nothing resident to edit: the provider holds the change, a window
         * bake shows it. */
        e->pending_rebuild = 1;
        return;
    }
    assert(we_in_scene(e, scene_x, scene_z));

    t = we_tile_idx(e, scene_x, scene_z, level);
    old = &e->floors[t];
    if( old->height == floor->height && old->settings == floor->settings &&
        old->underlay_id == floor->underlay_id && old->overlay_id == floor->overlay_id &&
        old->shape == floor->shape && old->rotation == floor->rotation )
    {
        *old = *floor; /* the authored pair may differ; nothing drawn does */
        return;
    }
    e->tiles_set++;

    if( old->height != floor->height )
    {
        heightmap_set(world->heightmap, scene_x, scene_z, level, floor->height);
        we_mark(e, scene_x, scene_z, level, WE_D_CORNER);
    }

    if( old->settings != floor->settings )
    {
        int changed = old->settings ^ floor->settings;
        flag_map_set(builder->flag_map, scene_x, scene_z, level, floor->settings);
        if( world->tile_flags )
            world->tile_flags[t] = floor->settings;
        /* A bridge moves painter tiles between levels and every collision
         * stamp of the column a level down: the bake's job, not a flush's. */
        if( level == 1 && (changed & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
            e->link_below_changed = 1;
        we_mark(
            e,
            scene_x,
            scene_z,
            level,
            WE_D_FLAGS | ((changed & RSCACHE_FLOFLAG_BLOCK) != 0 ? (WE_D_BLOCK | WE_D_COLL) : 0));
    }

    if( old->underlay_id != floor->underlay_id || old->overlay_id != floor->overlay_id ||
        old->shape != floor->shape || old->rotation != floor->rotation )
    {
        uint32_t rgb_before = blendmap_get_underlay_rgb(builder->blendmap, scene_x, scene_z, level);

        /* Back to what a fresh build map holds, then the bake's own per-tile
         * apply: it only ever writes what the floor states. */
        blendmap_set_underlay_rgb(builder->blendmap, scene_x, scene_z, level, 0);
        we_overlay_tile_reset(overlaymap_get_tile(builder->overlaymap, scene_x, scene_z, level));
        *terrain_shape_map_get_tile(builder->terrain_shapemap, scene_x, scene_z, level) =
            (struct TerrainShapeMapTile){ 0 };
        world_terrain_apply_floor(
            builder, floor, scene_x, scene_z, level, scene_x, scene_z, level, 0);
        if( blendmap_get_underlay_rgb(builder->blendmap, scene_x, scene_z, level) != rgb_before )
            we_mark(e, scene_x, scene_z, level, WE_D_BLEND);
        we_mark(e, scene_x, scene_z, level, WE_D_MESH);
    }

    *old = *floor;
}

/* ---- loc_place / loc_remove ---------------------------------------------- */

static void
we_changed_add(
    struct WorldEdit* e,
    int index)
{
    e->changed = we_grow(e->changed, &e->changed_cap, e->changed_count + 1, sizeof(int));
    e->changed[e->changed_count++] = index;
}

/* Place, in map order just before placement `before` of the same square, or
 * (before < 0) at the end of the square's list. */
static int
we_loc_place(
    struct WorldBuilder* builder,
    const struct World_LocPlacement* placement,
    int before)
{
    struct WorldEdit* e = we_get(builder);
    struct World* world = builder->world;
    int abs_x;
    int abs_z;
    int square;
    int index;
    int sx;
    int sz;

    assert(placement);
    if( !e->ready )
    {
        e->pending_rebuild = 1;
        return WORLD_BUILDER_HANDLE_NONE;
    }
    assert(we_in_scene(e, placement->scene_x, placement->scene_z));
    assert(placement->level >= 0);
    assert(placement->level < WORLD_MAP_TERRAIN_LEVELS);

    abs_x = world->_chunk_sw_x * WORLD_MAP_TERRAIN_X + world->_offset_x + placement->scene_x;
    abs_z = world->_chunk_sw_z * WORLD_MAP_TERRAIN_Z + world->_offset_z + placement->scene_z;
    square = we_square_add(e, abs_x / WORLD_MAP_TERRAIN_X, abs_z / WORLD_MAP_TERRAIN_Z);
    index = we_placement_new(e, placement, square, WE_P_ADD);
    if( before >= 0 )
    {
        /* Where the document put it: the shade replay and the last wall of a
         * tile follow map order, so EDIT's order has to be the list's, or a
         * fresh bake of the same document lights the tile differently. */
        int const at = e->placements[before].seq;
        assert(e->placements[before].square == square);
        for( int i = 0; i < e->placement_count; i++ )
            if( i != index && e->placements[i].square == square && e->placements[i].seq >= at )
                e->placements[i].seq++;
        e->placements[index].seq = at;
    }
    we_footprint(builder, placement, &sx, &sz);
    if( sx > e->max_size )
        e->max_size = sx;
    if( sz > e->max_size )
        e->max_size = sz;
    we_changed_add(e, index);
    return we_make_handle(e, index);
}

static int
world_edit_loc_place(
    struct WorldBuilder* builder,
    const struct World_LocPlacement* placement)
{
    /* The end of its square's list: what loc_place has always meant. */
    return we_loc_place(builder, placement, -1);
}

int
WorldEdit_LocPlaceBefore(
    struct WorldBuilder* builder,
    const struct World_LocPlacement* placement,
    int before_handle)
{
    struct WorldEdit* e;
    int before;

    assert(builder);
    assert(builder->edit);
    assert(placement);
    e = builder->edit;
    if( before_handle == WORLD_BUILDER_HANDLE_NONE || !e->ready )
        return we_loc_place(builder, placement, -1);
    before = we_handle_index(e, before_handle);
    assert(before >= 0 && "world_edit: place before a handle that names nothing");
    assert(e->placements[before].state == WE_P_LIVE || e->placements[before].state == WE_P_ADD);
    return we_loc_place(builder, placement, before);
}

int
WorldEdit_PlacementNext(
    const struct WorldBuilder* builder,
    int handle)
{
    const struct WorldEdit* e;
    int index;
    int best = -1;

    assert(builder);
    assert(builder->edit);
    e = builder->edit;
    if( !e->ready )
        return WORLD_BUILDER_HANDLE_NONE;
    index = we_handle_index(e, handle);
    assert(index >= 0 && "world_edit: next of a handle that names nothing");
    for( int i = 0; i < e->placement_count; i++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[i];
        if( pl->square != e->placements[index].square || (pl->state != WE_P_LIVE && pl->state != WE_P_ADD) )
            continue;
        if( we_order_cmp(e, i, index) <= 0 )
            continue;
        if( best < 0 || we_order_cmp(e, i, best) < 0 )
            best = i;
    }
    return best < 0 ? WORLD_BUILDER_HANDLE_NONE : we_make_handle(e, best);
}

static void
world_edit_loc_remove(
    struct WorldBuilder* builder,
    int placement_handle)
{
    struct WorldEdit* e = we_get(builder);
    int index;
    struct WorldEditPlacement* pl;

    if( !e->ready )
    {
        e->pending_rebuild = 1;
        return;
    }
    index = we_handle_index(e, placement_handle);
    assert(index >= 0 && "world_edit: loc_remove of a handle that names nothing");
    pl = &e->placements[index];
    assert(pl->state == WE_P_LIVE || pl->state == WE_P_ADD);

    if( pl->state == WE_P_ADD )
    {
        /* Placed and removed inside one flush: never built, nothing to take
         * down, and gone from the change list too. */
        we_unlink(e, index);
        pl->state = WE_P_DEAD;
        for( int i = 0; i < e->changed_count; i++ )
            if( e->changed[i] == index )
            {
                e->changed[i] = e->changed[--e->changed_count];
                break;
            }
        return;
    }
    pl->state = WE_P_REMOVE;
    we_changed_add(e, index);
}

/* ---- flush: victims ----------------------------------------------------- */

static void
we_victim_add(
    struct WorldEdit* e,
    int index)
{
    struct WorldEditPlacement* pl = &e->placements[index];

    if( pl->state == WE_P_DEAD || pl->mark == e->epoch )
        return;
    pl->mark = e->epoch;
    e->victims = we_grow(e->victims, &e->victim_cap, e->victim_count + 1, sizeof(int));
    e->victims[e->victim_count++] = index;
}

/* Every placement whose footprint, widened by WE_HEIGHT_REACH, meets the four
 * tiles around a moved corner -- on that level, and on the level below (a loc
 * contoured ABOVE_OFFSET / DUAL_LEVEL_BLEND samples the level over it). */
static void
we_victims_for_corner(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int cx,
    int cz,
    int level)
{
    int reach = WE_HEIGHT_REACH + e->max_size;

    for( int l = level - 1; l <= level; l++ )
    {
        if( l < 0 )
            continue;
        for( int ox = cx - 1 - reach; ox <= cx + WE_HEIGHT_REACH; ox++ )
        {
            for( int oz = cz - 1 - reach; oz <= cz + WE_HEIGHT_REACH; oz++ )
            {
                if( !we_in_scene(e, ox, oz) )
                    continue;
                for( int i = e->tile_head[we_tile_idx(e, ox, oz, l)]; i != -1;
                     i = e->placements[i].next_in_tile )
                {
                    int sx;
                    int sz;
                    we_footprint(builder, &e->placements[i].p, &sx, &sz);
                    /* footprint [ox-R, ox+sx-1+R] against the tiles [cx-1, cx] */
                    if( ox - WE_HEIGHT_REACH > cx || ox + sx - 1 + WE_HEIGHT_REACH < cx - 1 )
                        continue;
                    if( oz - WE_HEIGHT_REACH > cz || oz + sz - 1 + WE_HEIGHT_REACH < cz - 1 )
                        continue;
                    we_victim_add(e, i);
                }
            }
        }
    }
}

/* Wall decor takes its offset from the wall on its tile: when that wall
 * changes, the decor re-places. */
static void
we_victims_for_wall(
    struct WorldEdit* e,
    int index)
{
    const struct WorldEditPlacement* pl = &e->placements[index];

    if( !pl->in_scene || World_LocShapeToLayer(pl->p.shape) != 0 )
        return;
    for( int i = e->tile_head[we_tile_idx(e, pl->p.scene_x, pl->p.scene_z, pl->p.level)]; i != -1;
         i = e->placements[i].next_in_tile )
        if( World_LocShapeToLayer(e->placements[i].p.shape) == 1 )
            we_victim_add(e, i);
}

/* Does this placement share light with its neighbours? Built: its records say.
 * Not built yet: its config does. */
static int
we_is_share_lit(
    struct WorldBuilder* builder,
    const struct WorldEdit* e,
    int index)
{
    const struct WorldEditPlacement* pl = &e->placements[index];

    if( pl->state == WE_P_ADD )
    {
        struct ToriRS_Location* cfg = CacheProvider_LocationGet(builder->cache, pl->p.loc_id);
        return cfg && cfg->sharelight != 0;
    }
    return we_has_light_share(e, index);
}

/*
 * The share-lit neighbours of a changed placement: everything whose lighting
 * merged with it, in either direction, and so changes with it. A gatherer at
 * `a` reaches tiles [a.x - 1, a.x + size] x [a.z - 1, a.z + size] (the level
 * above included, gather_adjacent_tiles), so a partner's origin lies within
 * max(size_a, size_b) + 1 of `a`, one level either way. Wider is safe.
 */
static void
we_victims_share_neighbours(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int index)
{
    const struct WorldEditPlacement* pl = &e->placements[index];
    int sx;
    int sz;
    int reach;

    if( !pl->in_scene || !we_is_share_lit(builder, e, index) )
        return;
    we_footprint(builder, &pl->p, &sx, &sz);
    reach = (sx > sz ? sx : sz);
    if( e->max_share_size > reach )
        reach = e->max_share_size;
    reach += 1;

    for( int l = pl->p.level - 1; l <= pl->p.level + 1; l++ )
    {
        if( l < 0 || l >= WORLD_MAP_TERRAIN_LEVELS )
            continue;
        for( int ox = pl->p.scene_x - reach; ox <= pl->p.scene_x + reach; ox++ )
            for( int oz = pl->p.scene_z - reach; oz <= pl->p.scene_z + reach; oz++ )
            {
                if( !we_in_scene(e, ox, oz) )
                    continue;
                for( int i = e->tile_head[we_tile_idx(e, ox, oz, l)]; i != -1;
                     i = e->placements[i].next_in_tile )
                    if( e->placements[i].state == WE_P_LIVE && we_has_light_share(e, i) )
                        we_victim_add(e, i);
            }
    }
}

/* ---- flush: take down, put back ------------------------------------------ */

static void
we_mark_shade_tiles(
    struct WorldEdit* e,
    const struct WorldEditRec* r)
{
    int x = r->x;
    int z = r->z;
    int l = r->level;

    /* The tiles shademap2_set_wall / _wall_corner / _sized write. */
    if( r->k == WORLD_EDIT_SHADE_SIZED )
    {
        for( int dx = 0; dx < r->v[0]; dx++ )
            for( int dz = 0; dz < r->v[1]; dz++ )
                we_mark(e, x + dx, z + dz, l, WE_D_SHADE);
        return;
    }
    /* Both wall writers touch the tile and its +x/+z neighbours at most. */
    for( int dx = 0; dx <= 1; dx++ )
        for( int dz = 0; dz <= 1; dz++ )
            we_mark(e, x + dx, z + dz, l, WE_D_SHADE);
}

/* The collision a placement stamps reaches one tile past its footprint (a
 * wall's far side). */
static void
we_mark_footprint(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int index,
    int bits)
{
    const struct WorldEditPlacement* pl = &e->placements[index];
    int sx;
    int sz;

    if( !pl->in_scene )
        return;
    we_footprint(builder, &pl->p, &sx, &sz);
    for( int x = pl->p.scene_x - 1; x <= pl->p.scene_x + sx; x++ )
        for( int z = pl->p.scene_z - 1; z <= pl->p.scene_z + sz; z++ )
            we_mark(e, x, z, pl->p.level, bits);
}

/* Release one scenery entity: the painter slots it holds, then the pool entry
 * (whose scene element the frame's entity-removed drain frees). */
static void
we_remove_entity(
    struct WorldBuilder* builder,
    int pool_index)
{
    struct World* world = builder->world;
    struct WorldEntity_Scenery* sc = World_EntityPoolGet(&world->entities.scenery, pool_index);

    assert(sc);
    if( world->painter )
    {
        int paint_level = World_LocPaintLevel(
            world, sc->grid_position.x, sc->grid_position.z, sc->grid_position.level);
        if( sc->shape >= RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE &&
            sc->shape <= RSCACHE_LOC_SHAPE_WALL_RECT_CORNER )
            painter_release_wall(
                world->painter, sc->grid_position.x, sc->grid_position.z, paint_level,
                sc->element_id);
        painter_release_scenery(
            world->painter, sc->grid_position.x, sc->grid_position.z, paint_level, sc->element_id);
        painter_release_decor(
            world->painter, sc->grid_position.x, sc->grid_position.z, paint_level, sc->element_id);
    }
    World_SceneryRemove(world, pool_index);
}

/* Remove what a placement put in the scene; its records stay for the shade
 * replay to read. The removal half of WorldBuilder_ApplyLocChange, without its
 * collision and minimap edits, which the flush redoes over a region. */
static void
we_take_down(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int index)
{
    struct World* world = builder->world;
    struct WorldEditPlacement* pl = &e->placements[index];
    int had_element = 0;
    int replaced = 0;

    for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
    {
        struct WorldEditRec const* rec = &e->recs[r];
        int pool_index;
        struct WorldEntity_Scenery* sc;

        if( rec->type == WE_REC_SHADE )
            we_mark_shade_tiles(e, rec);
        if( rec->type != WE_REC_ELEMENT )
            continue;
        had_element = 1;
        pool_index = ToriDraw_ElementIndexOfRaw(rec->element_id);
        if( pool_index < 0 || !World_EntityPoolIsActive(&world->entities.scenery, pool_index) )
            continue;
        sc = World_EntityPoolGet(&world->entities.scenery, pool_index);
        if( !sc || sc->element_id != rec->element_id || sc->loc_id != pl->p.loc_id )
        {
            replaced = 1;
            continue;
        }
        we_remove_entity(builder, pool_index);
    }
    if( replaced && pl->in_scene )
    {
        /* Another path -- the game's runtime loc change, which the editor's
         * ghost and its panel also use -- replaced an element of this
         * placement since it was built. What it put there in its place is this
         * same loc in this layer; it goes too, or the re-place would stand a
         * second copy beside it. */
        int pool_index;
        while( (pool_index = World_SceneryFindAt(
                    world, pl->p.scene_x, pl->p.scene_z, pl->p.level, pl->p.shape)) >= 0 )
        {
            struct WorldEntity_Scenery* sc =
                World_EntityPoolGet(&world->entities.scenery, pool_index);
            if( !sc || sc->loc_id != pl->p.loc_id || sc->angle != (pl->p.angle & 3) )
                break;
            we_remove_entity(builder, pool_index);
        }
    }
    /* A placement that built nothing may be one its multiloc hides. */
    if( !had_element && pl->in_scene )
        world_builder_hidden_forget_at(
            builder, pl->p.scene_x, pl->p.scene_z, pl->p.level, pl->p.shape);
}

/* Place one placement through the bake's own path, capturing its records. */
static void
we_put_back(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int index)
{
    struct World* world = builder->world;
    struct WorldEditPlacement* pl = &e->placements[index];
    struct ToriRS_Location* base;
    struct ToriRS_Location resolved;
    struct ToriRS_Location* cfg;
    struct ToriRS_MapLoc ml;
    struct Shademap2* shademap;
    int abs_x;
    int abs_z;

    pl->rec_first = e->rec_count;
    pl->rec_count = 0;
    if( !pl->in_scene )
        return;

    base = CacheProvider_LocationGet(builder->cache, pl->p.loc_id);
    if( !base )
        return; /* the bake places nothing for a loc with no config either */
    cfg = world_builder_resolve_loc_for_place(builder, base, &resolved);

    abs_x = world->_chunk_sw_x * WORLD_MAP_TERRAIN_X + world->_offset_x + pl->p.scene_x;
    abs_z = world->_chunk_sw_z * WORLD_MAP_TERRAIN_Z + world->_offset_z + pl->p.scene_z;
    ml.loc_id = pl->p.loc_id;
    ml.shape_select = pl->p.shape;
    ml.orientation = pl->p.angle;
    ml.chunk_pos_x = abs_x % WORLD_MAP_TERRAIN_X;
    ml.chunk_pos_z = abs_z % WORLD_MAP_TERRAIN_Z;
    ml.chunk_pos_level = pl->p.level;

    if( !cfg )
    {
        world_builder_hidden_note(
            builder, pl->p.scene_x, pl->p.scene_z, pl->p.level, pl->p.loc_id, pl->p.shape, pl->p.angle);
        return;
    }

    World_SceneryInfoMemoInvalidate(world, pl->p.loc_id);
    builder->scenery_mapx = abs_x / WORLD_MAP_TERRAIN_X;
    builder->scenery_mapz = abs_z / WORLD_MAP_TERRAIN_Z;
    builder->scenery_base_loc_id = pl->p.loc_id;

    /* Registered like a runtime spawn -- the painter's static set is baked, so
     * the per-frame scenery pass draws it -- but lit by the flush's own
     * sharelight pass, and its shadows only recorded: the shade replay writes
     * them in map order, which the last writer of a tile depends on. */
    shademap = builder->shademap;
    builder->shademap = NULL;
    builder->scenery_runtime_spawn = 1;
    builder->scenery_edit_rebake = 1;
    e->capture = index;
    if( world->painter )
        painter_set_suppress_slot_registration(world->painter, 1);
    scenery_add(builder, &ml, cfg, pl->p.scene_x, pl->p.scene_z);
    if( world->painter )
        painter_set_suppress_slot_registration(world->painter, 0);
    e->capture = -1;
    builder->scenery_edit_rebake = 0;
    builder->scenery_runtime_spawn = 0;
    builder->shademap = shademap;
}

/* The wall offset a tile ends a bake with: the last wall, in map order, that
 * set one there (decor_buildmap_set_wall_offset is a plain store). */
static int
we_wall_offset_at(
    struct WorldEdit* e,
    int x,
    int z,
    int level)
{
    int best = -1;
    int offset = 0;

    for( int i = e->tile_head[we_tile_idx(e, x, z, level)]; i != -1;
         i = e->placements[i].next_in_tile )
    {
        const struct WorldEditPlacement* pl = &e->placements[i];
        if( pl->state != WE_P_LIVE && pl->state != WE_P_ADD )
            continue;
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
        {
            const struct WorldEditRec* rec = &e->recs[r];
            if( rec->type != WE_REC_WALL_OFFSET || rec->x != x || rec->z != z ||
                rec->level != level )
                continue;
            if( best < 0 || we_order_cmp(e, i, best) > 0 )
            {
                best = i;
                offset = rec->v[0];
            }
        }
    }
    return offset;
}

/* ---- flush: light ------------------------------------------------------- */

static struct ToriDraw_Model*
we_element_model(
    struct WorldBuilder* builder,
    int element_id)
{
    struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(builder->scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
        return NULL;
    return el->model.u.model.model;
}

/*
 * merge_normals with a choice of which side takes the merge.
 *
 * The bake merges every share-lit pair once and writes both models. A flush
 * that relights a victim next to a placement it did NOT rebuild must give the
 * victim its half of that merge and leave the other model alone -- the other's
 * colours already carry the same merge from the bake, against identical
 * geometry. Same arithmetic as merge_normals, line for line, gated per side.
 */
static void
we_merge_normals_sided(
    struct WorldBuilderSlh* slh,
    struct ToriDraw_Model* model,
    struct ToriDraw_Normal* vertex_normals,
    struct ToriDraw_Normal* lighting_vertex_normals,
    struct ToriDraw_Model* other_model,
    struct ToriDraw_Normal* other_vertex_normals,
    struct ToriDraw_Normal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z,
    bool hide_faces,
    int write_model,
    int write_other)
{
    int merged_vertex_count = 0;
    int model_vc = model->vertex_count;
    int other_vc = other_model->vertex_count;
    vertexint_t* model_vx = model->vertices_x;
    vertexint_t* model_vy = model->vertices_y;
    vertexint_t* model_vz = model->vertices_z;
    vertexint_t* other_vx = other_model->vertices_x;
    vertexint_t* other_vy = other_model->vertices_y;
    vertexint_t* other_vz = other_model->vertices_z;

    g_merge_index++;

    slh_reserve(slh, other_vc);
    slh->build++;
    uint32_t const bucket_mask = (uint32_t)slh->bucket_cap - 1;
    for( int other_vertex = 0; other_vertex < other_vc; other_vertex++ )
    {
        if( other_vertex_normals[other_vertex].face_count == 0 )
            continue;
        uint32_t b =
            slh_hash(other_vx[other_vertex], other_vy[other_vertex], other_vz[other_vertex]) &
            bucket_mask;
        if( slh->serial[b] != slh->build )
        {
            slh->serial[b] = slh->build;
            slh->next[other_vertex] = -1;
        }
        else
            slh->next[other_vertex] = (int32_t)slh->head[b];
        slh->head[b] = (uint32_t)other_vertex;
    }

    for( int vertex = 0; vertex < model_vc; vertex++ )
    {
        struct ToriDraw_Normal* a = &vertex_normals[vertex];
        int x;
        int y;
        int z;
        uint32_t b;

        if( a->face_count == 0 )
            continue;
        x = model_vx[vertex] - check_offset_x;
        y = model_vy[vertex] - check_offset_y;
        z = model_vz[vertex] - check_offset_z;
        b = slh_hash(x, y, z) & bucket_mask;
        if( slh->serial[b] != slh->build )
            continue;

        for( int32_t other_vertex = (int32_t)slh->head[b]; other_vertex != -1;
             other_vertex = slh->next[other_vertex] )
        {
            struct ToriDraw_Normal* bn;

            if( x != other_vx[other_vertex] || y != other_vy[other_vertex] ||
                z != other_vz[other_vertex] )
                continue;
            bn = &other_vertex_normals[other_vertex];
            if( write_model )
            {
                struct ToriDraw_Normal* al = &lighting_vertex_normals[vertex];
                al->x += bn->x;
                al->y += bn->y;
                al->z += bn->z;
                al->face_count += bn->face_count;
                al->merged++;
            }
            if( write_other )
            {
                struct ToriDraw_Normal* bl = &other_lighting_vertex_normals[other_vertex];
                bl->x += a->x;
                bl->y += a->y;
                bl->z += a->z;
                bl->face_count += a->face_count;
                bl->merged++;
            }
            merged_vertex_count++;
            g_vertex_a_merge_index[vertex] = g_merge_index;
            g_vertex_b_merge_index[other_vertex] = g_merge_index;
        }
    }

    if( merged_vertex_count < 3 || !hide_faces )
        return;
    if( write_model )
        hide_merged_faces(model, g_vertex_a_merge_index);
    if( write_other )
        hide_merged_faces(other_model, g_vertex_b_merge_index);
}

/* An element whose base normals a one-sided merge borrowed; freed after. */
struct WeLoan
{
    int* ids;
    int count;
    int cap;
};

static void
we_loan_note(
    struct WeLoan* loan,
    int element_id)
{
    for( int i = 0; i < loan->count; i++ )
        if( loan->ids[i] == element_id )
            return;
    loan->ids = we_grow(loan->ids, &loan->cap, loan->count + 1, sizeof(int));
    loan->ids[loan->count++] = element_id;
}

/*
 * One merge of the bake's merge_column: gatherer `a` (a LIGHT record of
 * placement ia) against partner `b` (of placement ib), each side written only
 * when its placement is being relit this flush.
 */
static void
we_merge_pair(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    const struct WorldEditRec* a,
    int a_victim,
    const struct WorldEditRec* b,
    int b_victim,
    int b_tile_x,
    int b_tile_z,
    int b_tile_level,
    struct WeLoan* loan)
{
    struct World* world = builder->world;
    struct ToriDraw_Model* dm;
    struct ToriDraw_Model* adjacent_dm;
    int check_offset_x;
    int check_offset_z;
    int check_offset_height;

    (void)e;
    dm = we_element_model(builder, a->element_id);
    adjacent_dm = we_element_model(builder, b->element_id);
    if( !dm || !adjacent_dm )
        return;
    if( !ToriDraw_ModelIsLightable(adjacent_dm) || !ToriDraw_ModelIsLightable(dm) )
        return;

    check_offset_x = (b_tile_x - a->x) * 128 + (b->v[0] - a->v[0]) * 64;
    check_offset_z = (b_tile_z - a->z) * 128 + (b->v[1] - a->v[1]) * 64;
    check_offset_height = heightmap_get_center(world->heightmap, b_tile_x, b_tile_z, b_tile_level) -
                          heightmap_get_center(world->heightmap, a->x, a->z, a->level);

    /* A side not being relit still lends its BASE normals: borrow them. */
    if( !a_victim && !dm->normals )
    {
        sharelight_ensure_normals(dm);
        we_loan_note(loan, a->element_id);
    }
    if( !b_victim && !adjacent_dm->normals )
    {
        sharelight_ensure_normals(adjacent_dm);
        we_loan_note(loan, b->element_id);
    }
    sharelight_ensure_normals(dm);
    sharelight_ensure_normals(adjacent_dm);
    assert(dm->normals);
    assert(dm->merged_normals);
    assert(adjacent_dm->merged_normals);

    we_merge_normals_sided(
        &builder->slh,
        dm,
        dm->normals->vertex_normals,
        dm->merged_normals->vertex_normals,
        adjacent_dm,
        adjacent_dm->normals->vertex_normals,
        adjacent_dm->merged_normals->vertex_normals,
        check_offset_x,
        check_offset_height,
        check_offset_z,
        sharelight_should_hide_faces_for_merge(a->level, b_tile_level),
        a_victim,
        b_victim);
}

/* Every merge the bake's merge_column makes with gatherer record `a`. */
static void
we_merge_gatherer(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    const struct WorldEditRec* a,
    int a_victim,
    int only_victim_partners,
    struct WeLoan* loan)
{
    struct TileCoord adjacent[ADJACENT_TILES_COUNT];
    int count = gather_adjacent_tiles(
        e->scene_size,
        e->scene_size,
        adjacent,
        ADJACENT_TILES_COUNT,
        a->x,
        a->z,
        a->level,
        a->v[0],
        a->v[1]);

    for( int j = 0; j < count; j++ )
    {
        struct TileCoord t = adjacent[j];
        for( int i = e->tile_head[we_tile_idx(e, t.x, t.z, t.level)]; i != -1;
             i = e->placements[i].next_in_tile )
        {
            const struct WorldEditPlacement* pb = &e->placements[i];
            int b_victim = pb->mark == e->epoch;

            if( pb->state != WE_P_LIVE && pb->state != WE_P_ADD )
                continue;
            if( only_victim_partners && !b_victim )
                continue;
            if( !a_victim && !b_victim )
                continue;
            for( int r = pb->rec_first; r < pb->rec_first + pb->rec_count; r++ )
            {
                const struct WorldEditRec* b = &e->recs[r];
                if( b->type != WE_REC_LIGHT || !b->k || b->element_id == a->element_id )
                    continue;
                /* The sharelight map files an element at the tile it was
                 * pushed with, which is the placement's origin. */
                if( b->x != t.x || b->z != t.z || b->level != t.level )
                    continue;
                we_merge_pair(builder, e, a, a_victim, b, b_victim, t.x, t.z, t.level, loan);
            }
        }
    }
}

/* apply_and_free_column's per-element body. */
static void
we_apply_share_light(
    struct WorldBuilder* builder,
    const struct WorldEditRec* rec)
{
    struct ToriDraw_Model* dm = we_element_model(builder, rec->element_id);
    struct ToriDraw_LightProfile const* p;
    int light_ambient;
    int light_attenuation;
    int light_magnitude;
    int attenuation;

    if( !dm || !ToriDraw_ModelIsLightable(dm) )
        return;
    assert(dm->normals);
    assert(dm->merged_normals);
    p = ToriDraw_LightSceneProfile();
    light_ambient = p->ambient + rec->v[2];
    light_attenuation = p->attenuation + rec->v[3];
    light_magnitude =
        (int)sqrt((double)(p->src_x * p->src_x + p->src_y * p->src_y + p->src_z * p->src_z));
    attenuation = (light_attenuation * light_magnitude) >> 8;
    ToriDraw_ApplyLighting(
        dm->face_colors_a,
        dm->face_colors_b,
        dm->face_colors_c,
        dm->merged_normals->vertex_normals,
        dm->normals->face_normals,
        dm->face_indices_a,
        dm->face_indices_b,
        dm->face_indices_c,
        dm->face_count,
        dm->face_colors,
        dm->face_alphas,
        dm->face_textures,
        dm->face_infos,
        light_ambient,
        attenuation,
        p->src_x,
        p->src_y,
        p->src_z,
        dm->vertices_x,
        dm->vertices_y,
        dm->vertices_z);
    ToriDraw_ModelFreeNormals(dm);
}

/* defaultlight_build's per-element body. */
static void
we_apply_default_light(
    struct WorldBuilder* builder,
    const struct WorldEditRec* rec)
{
    struct ToriDraw_Model* dm = we_element_model(builder, rec->element_id);
    struct ToriDraw_ModelHandle hnd;

    if( !dm || !ToriDraw_ModelIsLightable(dm) )
        return;
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = dm;
    /* sharelight_map_push(ambient, attenuation = contrast) feeds
     * LightModelScene(attenuation, ambient). */
    ToriDraw_LightModelScene(hnd, rec->v[3], rec->v[2]);
    ToriDraw_ModelFreeNormals(dm);
}

/*
 * World_build_lighting for the victims: every share-lit victim merges with
 * every partner the bake would have merged it with -- victims on both sides,
 * kept placements on the partner's side only -- then lights; the default-lit
 * victims light on their own.
 */
static void
we_relight_victims(
    struct WorldBuilder* builder,
    struct WorldEdit* e)
{
    struct WeLoan loan = { 0 };

    /* Fresh normals on every share-lit victim before any merge reads them,
     * as the bake's lookahead alloc has them all before its merges. */
    for( int v = 0; v < e->victim_count; v++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[e->victims[v]];
        if( pl->state == WE_P_REMOVE || pl->state == WE_P_DEAD )
            continue;
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
        {
            struct ToriDraw_Model* dm;
            if( e->recs[r].type != WE_REC_LIGHT || !e->recs[r].k )
                continue;
            dm = we_element_model(builder, e->recs[r].element_id);
            if( dm )
                sharelight_ensure_normals(dm);
        }
    }

    /* Victim gatherers: every partner. */
    for( int v = 0; v < e->victim_count; v++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[e->victims[v]];
        if( pl->state == WE_P_REMOVE || pl->state == WE_P_DEAD )
            continue;
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
            if( e->recs[r].type == WE_REC_LIGHT && e->recs[r].k )
                we_merge_gatherer(builder, e, &e->recs[r], 1, 0, &loan);
    }

    /* Kept gatherers whose reach takes in a victim: the victim's half. A kept
     * gatherer is within reach of some victim's origin (see
     * we_victims_share_neighbours for the bound). */
    for( int v = 0; v < e->victim_count; v++ )
    {
        const struct WorldEditPlacement* pv = &e->placements[e->victims[v]];
        int reach = e->max_share_size + 1;

        if( pv->state == WE_P_REMOVE || pv->state == WE_P_DEAD || !pv->in_scene ||
            !we_has_light_share(e, e->victims[v]) )
            continue;
        for( int l = pv->p.level - 1; l <= pv->p.level; l++ )
        {
            if( l < 0 )
                continue;
            for( int ox = pv->p.scene_x - reach; ox <= pv->p.scene_x + 1; ox++ )
                for( int oz = pv->p.scene_z - reach; oz <= pv->p.scene_z + 1; oz++ )
                {
                    if( !we_in_scene(e, ox, oz) )
                        continue;
                    for( int i = e->tile_head[we_tile_idx(e, ox, oz, l)]; i != -1;
                         i = e->placements[i].next_in_tile )
                    {
                        const struct WorldEditPlacement* pc = &e->placements[i];
                        if( pc->state != WE_P_LIVE || pc->mark == e->epoch )
                            continue;
                        /* Each kept gatherer once: tag it with the epoch's
                         * negative, which no victim mark can equal. */
                        if( pc->mark == ~e->epoch )
                            continue;
                        e->placements[i].mark = ~e->epoch;
                        for( int r = pc->rec_first; r < pc->rec_first + pc->rec_count; r++ )
                            if( e->recs[r].type == WE_REC_LIGHT && e->recs[r].k )
                                we_merge_gatherer(builder, e, &e->recs[r], 0, 1, &loan);
                    }
                }
        }
    }

    for( int i = 0; i < loan.count; i++ )
    {
        struct ToriDraw_Model* dm = we_element_model(builder, loan.ids[i]);
        if( dm )
            ToriDraw_ModelFreeNormals(dm);
    }
    ToriMem_Free(ToriMem_Scene(), loan.ids);

    for( int v = 0; v < e->victim_count; v++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[e->victims[v]];
        if( pl->state == WE_P_REMOVE || pl->state == WE_P_DEAD )
            continue;
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
        {
            if( e->recs[r].type != WE_REC_LIGHT )
                continue;
            if( e->recs[r].k )
                we_apply_share_light(builder, &e->recs[r]);
            else
                we_apply_default_light(builder, &e->recs[r]);
        }
    }
}

/* ---- flush: terrain ------------------------------------------------------ */

/* lightmap_build's sunlight at one corner, before shade. Corners on the scene
 * edge stay 0 there (its loop is 1..size-2), and so here. */
static int
we_raw_light(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level)
{
    struct Heightmap* hm = builder->world->heightmap;
    struct Lightmap* lm = builder->lightmap;
    int magnitude;
    int intensity;
    int dx;
    int dz;
    int len;
    int nx;
    int ny;
    int nz;
    int dot;
    int sunlight;

    if( x < 1 || z < 1 || x >= lm->width - 1 || z >= lm->height - 1 )
        return 0;
    magnitude = (int)sqrt((double)(WE_LIGHT_DIR_X * WE_LIGHT_DIR_X + WE_LIGHT_DIR_Y * WE_LIGHT_DIR_Y +
                                   WE_LIGHT_DIR_Z * WE_LIGHT_DIR_Z));
    intensity = (magnitude * WE_LIGHT_ATTENUATION) >> 8;
    dx = heightmap_get(hm, x + 1, z, level) - heightmap_get(hm, x - 1, z, level);
    dz = heightmap_get(hm, x, z + 1, level) - heightmap_get(hm, x, z - 1, level);
    len = (int)sqrt((double)(dz * dz + dx * dx + WE_LIGHT_HEIGHT_SCALE));
    nx = dx * 256 / len;
    ny = WE_LIGHT_HEIGHT_SCALE / len;
    nz = dz * 256 / len;
    dot = nx * WE_LIGHT_DIR_X + ny * WE_LIGHT_DIR_Y + nz * WE_LIGHT_DIR_Z;
    sunlight = dot / intensity + WE_LIGHT_AMBIENT;
    if( sunlight < 0 )
        sunlight = 0;
    else if( sunlight > 255 )
        sunlight = 255;
    return sunlight;
}

static struct HSL
we_hsl(
    struct WorldEdit* e,
    uint32_t rgb)
{
    struct WorldEditHslMemo* m = &e->hsl_memo[(rgb * 2654435761u) >> 22];
    if( !m->used || m->rgb != rgb )
    {
        m->used = 1;
        m->rgb = rgb;
        m->hsl = palette_rgb_to_hsl24((int)rgb);
    }
    return m->hsl;
}

/*
 * Re-blend the tiles of [x0,x1] x [z0,z1] on one level: blendmap_build's
 * average over each tile's window (WE_BLEND_RADIUS), as direct sums. The
 * bake's running sums add the same integers, so the quotients agree exactly.
 * Marks a tile whose colour moved for a remesh.
 */
static void
we_reblend_rect(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int level,
    int x0,
    int z0,
    int x1,
    int z1)
{
    struct Blendmap* bm = builder->blendmap;
    int w = bm->width;
    int h = bm->height;

    if( x0 < 0 )
        x0 = 0;
    if( z0 < 0 )
        z0 = 0;
    if( x1 > w - 1 )
        x1 = w - 1;
    if( z1 > h - 1 )
        z1 = h - 1;

    for( int x = x0; x <= x1; x++ )
    {
        for( int z = z0; z <= z1; z++ )
        {
            int idx = x + z * w + level * w * h;
            int32_t hsl16 = -1;

            if( bm->underlaymap[idx] > 0 )
            {
                int chroma = 0;
                int sat = 0;
                int light = 0;
                int luminance = 0;
                int number = 0;

                for( int wx = x - WE_BLEND_RADIUS + 1; wx <= x + WE_BLEND_RADIUS; wx++ )
                {
                    if( wx < 0 || wx >= w )
                        continue;
                    for( int wz = z - WE_BLEND_RADIUS + 1; wz <= z + WE_BLEND_RADIUS; wz++ )
                    {
                        uint32_t rgb;
                        struct HSL hsl;
                        if( wz < 0 || wz >= h )
                            continue;
                        rgb = bm->underlaymap[wx + wz * w + level * w * h];
                        if( rgb == 0 )
                            continue;
                        hsl = we_hsl(e, rgb);
                        chroma += hsl.chroma;
                        sat += hsl.sat;
                        light += hsl.light;
                        luminance += hsl.luminance;
                        number++;
                    }
                }
                {
                    int avg_hue = ((chroma * 256) / luminance) & 0xFF;
                    int avg_sat = (sat / number) & 0xFF;
                    int avg_light = (light / number) & 0xFF;
                    hsl16 = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);
                }
            }
            if( bm->blendmap[idx] != hsl16 )
            {
                bm->blendmap[idx] = hsl16;
                we_mark(e, x, z, level, WE_D_MESH);
                e->stats.tiles_reblended++;
            }
        }
    }
}

/* A bridge column: its minimap planes were pushed down a level at the bake
 * (world_builder_pushdown_minimap), so nothing may be stamped into it at its
 * cache level -- the column is rebuilt and pushed again instead. */
static int
we_link_below_column(
    struct WorldBuilder* builder,
    int x,
    int z)
{
    return (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0;
}

static void we_add_tile_walls(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int x,
    int z,
    int level);

/* A bridge column's minimap, from scratch: every cache level's terrain colours
 * and wall lines, then the bake's push-down. */
static void
we_minimap_column(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int x,
    int z)
{
    struct Minimap* mm = builder->world->minimap;

    if( !mm )
        return;
    for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
    {
        int walls = minimap_tile_wall(mm, x, z, l);
        minimap_set_tile_color(mm, x, z, l, 0, MINIMAP_FOREGROUND);
        minimap_set_tile_color(mm, x, z, l, 0, MINIMAP_BACKGROUND);
        minimap_set_tile_shape(mm, x, z, l, 0, 0);
        if( walls != 0 )
            minimap_del_tile_wall(mm, x, z, l, (enum MinimapWallFlag)walls);
    }
    for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
    {
        /* The colours come from the same tile build the mesh does. */
        if( x >= 1 && z >= 1 && x < e->scene_size - 1 && z < e->scene_size - 1 )
        {
            struct ToriDraw_Model* td = NULL;
            world_terrain_build_tile(builder, x, z, l, &td);
            if( td )
                ToriDraw_ModelFree(td);
        }
        we_add_tile_walls(builder, e, x, z, l);
    }
    minimap_push_down_tiles(mm, x, z);
    builder->world->minimap_seq++;
}

/* One tile's terrain element, re-derived: the body of the bake's mesh loop. */
static void
we_remesh_tile(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int x,
    int z,
    int level)
{
    struct World* world = builder->world;
    struct ToriDraw_Model* td = NULL;
    int active;
    int element_id;

    /* The bake meshes 1..size-2 only. */
    if( x < 1 || z < 1 || x >= e->scene_size - 1 || z >= e->scene_size - 1 )
        return;
    e->stats.tiles_remeshed++;

    if( we_link_below_column(builder, x, z) )
    {
        /* Built without stamping the minimap, which the column rebuild
         * re-derives in its pushed-down order. */
        struct Minimap* mm = world->minimap;
        world->minimap = NULL;
        active = world_terrain_build_tile(builder, x, z, level, &td);
        world->minimap = mm;
        we_mark(e, x, z, 0, WE_D_MMCOL);
    }
    else
        active = world_terrain_build_tile(builder, x, z, level, &td);
    if( !active && world->minimap && !we_link_below_column(builder, x, z) )
    {
        /* Nothing stamps an inactive tile; a fresh scene's minimap holds 0. */
        minimap_set_tile_color(world->minimap, x, z, level, 0, MINIMAP_FOREGROUND);
        minimap_set_tile_color(world->minimap, x, z, level, 0, MINIMAP_BACKGROUND);
        minimap_set_tile_shape(world->minimap, x, z, level, 0, 0);
    }

    element_id = World_TerrainElementAt(world, x, z, level);
    if( !td )
    {
        if( element_id >= 0 )
        {
            ToriDraw_SceneElementRemove(builder->scene, element_id);
            World_EntityPoolRelease(
                &world->entities.terrain, World_TerrainTileIdx(world, x, z, level));
            if( world->painter )
                painter_set_terrain_element(world->painter, x, z, level, -1);
        }
    }
    else
    {
        struct ToriDraw_ModelHandle hnd = {
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = td,
        };
        ToriDraw_ModelNoteTextureWants(td);
        if( element_id < 0 )
        {
            element_id = ElementId_Raw(ElementId_Make(
                TORIDRAW_ELEMENT_KIND_TERRAIN,
                ToriDraw_SceneElementAddPool(builder->scene, builder->static_pool)));
            assert(element_id >= 0);
            ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);
            ToriDraw_SceneElementSetPosition(
                builder->scene, element_id, x * WORLD_TILE_SIZE, 0, z * WORLD_TILE_SIZE, 0);
            World_TerrainSet(world, element_id, x, z, level);
        }
        else
            ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);
    }

    /* The bake's pruning of each paint tile's terrain-level set, for this
     * column: a paint tile emits its mesh level's terrain when one exists. */
    if( world->painter )
    {
        for( int g = 0; g < painter_max_levels(world->painter); g++ )
        {
            int ml = painters_tile_get_mesh_level(painter_tile_at(world->painter, x, z, g));
            unsigned want = 0;
            if( ml < WORLD_MAP_TERRAIN_LEVELS && World_TerrainElementAt(world, x, z, ml) >= 0 )
                want = 1u << ml;
            if( painter_tile_get_terrain_levels(world->painter, x, z, g) != want )
                painter_tile_set_terrain_levels(world->painter, x, z, g, want);
        }
    }
}

/* ---- flush: collision, minimap, flags ------------------------------------ */

static void
we_restamp_collision(
    struct WorldBuilder* builder,
    struct WorldEdit* e)
{
    struct World* world = builder->world;
    int any = 0;
    int x0 = e->scene_size;
    int z0 = e->scene_size;
    int x1 = -1;
    int z1 = -1;

    /* Clear what the scene build put down on every marked tile, every
     * collision level (a link-below column stamps a level lower). */
    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_COLL) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        for( int cl = 0; cl < COLLISION_LEVELS; cl++ )
        {
            struct CollisionMap* cm = world->collision_maps[cl];
            int idx;
            int edge;
            if( !cm || x >= cm->size_x || z >= cm->size_z )
                continue;
            idx = collision_map_index_at(cm, x, z);
            edge = x == 0 || z == 0 || x == cm->size_x - 1 || z == cm->size_z - 1;
            cm->flags[idx] =
                (cm->flags[idx] & ~COLL_FLAG_BOUNDS) | (edge ? COLL_FLAG_BOUNDS : COLL_FLAG_OPEN);
        }
        any = 1;
        e->stats.collision_tiles++;
        if( x < x0 )
            x0 = x;
        if( z < z0 )
            z0 = z;
        if( x > x1 )
            x1 = x;
        if( z > z1 )
            z1 = z;
    }
    if( !any )
        return;

    /* Terrain blocks on the cleared tiles (world_collision_apply_terrain). */
    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_COLL) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        for( int level = 0; level < COLLISION_LEVELS; level++ )
        {
            int true_level = level;
            if( (flag_map_get(builder->flag_map, x, z, level) & RSCACHE_FLOFLAG_BLOCK) == 0 )
                continue;
            if( (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
                true_level--;
            if( true_level < 0 || !world->collision_maps[true_level] )
                continue;
            collision_map_add_floor(world->collision_maps[true_level], x, z);
        }
    }

    /* Every placement that can stamp a cleared tile: the stamps are ORs, so
     * re-stamping what lands outside the cleared set changes nothing. */
    for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
        for( int ox = x0 - 1 - e->max_size; ox <= x1 + 1; ox++ )
            for( int oz = z0 - 1 - e->max_size; oz <= z1 + 1; oz++ )
            {
                if( !we_in_scene(e, ox, oz) )
                    continue;
                for( int i = e->tile_head[we_tile_idx(e, ox, oz, l)]; i != -1;
                     i = e->placements[i].next_in_tile )
                {
                    const struct WorldEditPlacement* pl = &e->placements[i];
                    struct ToriRS_Location* base;
                    struct ToriRS_Location resolved;
                    struct ToriRS_Location* cfg;
                    struct ToriRS_MapLoc ml;

                    if( pl->state != WE_P_LIVE && pl->state != WE_P_ADD )
                        continue;
                    base = CacheProvider_LocationGet(builder->cache, pl->p.loc_id);
                    if( !base )
                        continue;
                    cfg = world_builder_resolve_loc_for_place(builder, base, &resolved);
                    if( !cfg )
                        continue;
                    ml.loc_id = pl->p.loc_id;
                    ml.shape_select = pl->p.shape;
                    ml.orientation = pl->p.angle;
                    ml.chunk_pos_x = pl->p.scene_x;
                    ml.chunk_pos_z = pl->p.scene_z;
                    ml.chunk_pos_level = pl->p.level;
                    world_collision_add_loc(builder, &ml, cfg, pl->p.scene_x, pl->p.scene_z);
                }
            }
}

/* A tile whose locs changed: its minimap wall lines and ambient sounds come
 * from what stands there now. */
/* world_builder_minimap_add_loc's wall lines for the placements standing on
 * one tile, at that tile's cache level. */
static void
we_add_tile_walls(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int x,
    int z,
    int level)
{
    struct World* world = builder->world;

    if( !world->minimap )
        return;
    for( int i = e->tile_head[we_tile_idx(e, x, z, level)]; i != -1;
         i = e->placements[i].next_in_tile )
    {
        const struct WorldEditPlacement* pl = &e->placements[i];
        struct ToriRS_Location* base;
        struct ToriRS_Location* cfg;
        int flags;

        if( pl->state != WE_P_LIVE && pl->state != WE_P_ADD )
            continue;
        base = CacheProvider_LocationGet(builder->cache, pl->p.loc_id);
        cfg = base ? world_builder_resolve_loc(builder, base) : NULL;
        if( !cfg || cfg->map_scene_id != -1 )
            continue;
        flags = scenery_minimap_wall_flags(pl->p.shape, pl->p.angle, cfg->is_interactive);
        if( flags != 0 )
            minimap_add_tile_wall(world->minimap, x, z, level, flags);
    }
}

static void
we_refresh_tile_locs(
    struct WorldBuilder* builder,
    struct WorldEdit* e,
    int x,
    int z,
    int level)
{
    struct World* world = builder->world;

    if( we_link_below_column(builder, x, z) )
        we_mark(e, x, z, 0, WE_D_MMCOL);
    else if( world->minimap )
    {
        int walls = minimap_tile_wall(world->minimap, x, z, level);
        if( walls != 0 )
            minimap_del_tile_wall(world->minimap, x, z, level, (enum MinimapWallFlag)walls);
        we_add_tile_walls(builder, e, x, z, level);
    }
    World_RemoveAreaSoundAt(world, x, z, level);

    for( int i = e->tile_head[we_tile_idx(e, x, z, level)]; i != -1;
         i = e->placements[i].next_in_tile )
    {
        const struct WorldEditPlacement* pl = &e->placements[i];
        struct ToriRS_Location* base;
        struct ToriRS_MapLoc ml;
        struct ToriRS_Location* cfg;

        if( pl->state != WE_P_LIVE && pl->state != WE_P_ADD )
            continue;
        base = CacheProvider_LocationGet(builder->cache, pl->p.loc_id);
        cfg = base ? world_builder_resolve_loc(builder, base) : NULL;
        if( !cfg )
            continue;
        ml.loc_id = pl->p.loc_id;
        ml.shape_select = pl->p.shape;
        ml.orientation = pl->p.angle;
        ml.chunk_pos_x = x;
        ml.chunk_pos_z = z;
        ml.chunk_pos_level = level;
        world_builder_add_loc_area_sound(builder, &ml, cfg, x, z);
    }
    world->minimap_seq++;
}

/* RebuildCenterzoneEnd's draw-level pass, for one column. */
static void
we_column_draw_levels(
    struct WorldBuilder* builder,
    int x,
    int z)
{
    struct World* world = builder->world;
    int link_l1;

    if( !world->painter )
        return;
    link_l1 = (flag_map_get(builder->flag_map, x, z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0;
    for( int g = 0; g < painter_max_levels(world->painter); g++ )
    {
        int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
        uint8_t st = (uint8_t)flag_map_get(builder->flag_map, x, z, src);
        painter_tile_set_draw_level(
            world->painter, x, z, g, RSCache_MapFloorVisBelowDrawLevel(st, src, link_l1));
    }
}

/* ---- flush --------------------------------------------------------------- */

/*
 * A re-placed placement records afresh and its old records become garbage; a
 * long session would otherwise grow the pool by every stroke. When the garbage
 * outweighs the live records, copy the live ones down, placement by placement
 * (each placement's records stay contiguous).
 */
static void
we_compact_records(struct WorldEdit* e)
{
    int live = 0;
    int out = 0;
    struct WorldEditRec* packed;

    for( int i = 0; i < e->placement_count; i++ )
        if( e->placements[i].state != WE_P_DEAD )
            live += e->placements[i].rec_count;
    if( e->rec_count < 2 * live + 4096 )
        return;
    packed = ToriMem_Alloc(ToriMem_Scene(), (size_t)(live > 0 ? live : 1) * sizeof(*packed));
    assert(packed);
    for( int i = 0; i < e->placement_count; i++ )
    {
        struct WorldEditPlacement* pl = &e->placements[i];
        if( pl->state == WE_P_DEAD )
        {
            pl->rec_first = 0;
            pl->rec_count = 0;
            continue;
        }
        if( pl->rec_count > 0 )
            memcpy(&packed[out], &e->recs[pl->rec_first], (size_t)pl->rec_count * sizeof(*packed));
        pl->rec_first = out;
        out += pl->rec_count;
    }
    ToriMem_Free(ToriMem_Scene(), e->recs);
    e->recs = packed;
    e->rec_count = out;
    e->rec_cap = live > 0 ? live : 1;
}

static void
we_clear_pending(struct WorldEdit* e)
{
    for( int i = 0; i < e->dirty_count; i++ )
        e->dirty[e->dirty_list[i]] = 0;
    e->dirty_count = 0;
    e->changed_count = 0;
    e->link_below_changed = 0;
    e->pending_rebuild = 0;
    e->tiles_set = 0;
}

static int
world_edit_flush(struct WorldBuilder* builder)
{
    struct WorldEdit* e = we_get(builder);
    struct World* world = builder->world;
    double t0;
    int first_dirty_count;
    int any_geometry;

    if( !e->ready )
    {
        int rebuild = e->pending_rebuild;
        e->pending_rebuild = 0;
        return rebuild ? WORLD_BUILDER_FLUSH_REBUILD : WORLD_BUILDER_FLUSH_OK;
    }
    if( e->dirty_count == 0 && e->changed_count == 0 )
        return WORLD_BUILDER_FLUSH_OK;
    if( e->link_below_changed || e->pending_rebuild )
    {
        /* Until the rebake lands, nothing resident matches the provider:
         * later edits only ask for the rebake too. */
        we_clear_pending(e);
        e->ready = 0;
        return WORLD_BUILDER_FLUSH_REBUILD;
    }

    t0 = wb_now_ms();
    e->epoch += 2; /* even: ~epoch (the kept-gatherer tag) never equals one */
    e->victim_count = 0;
    {
        int flushes = e->stats.flushes;
        memset(&e->stats, 0, sizeof(e->stats));
        e->stats.flushes = flushes + 1;
    }
    e->stats.tiles_set = e->tiles_set;

    /* 1. Heights: the four tiles around a moved corner remesh, the four
     *    corners around it relight, and what stands on or over them
     *    re-places. */
    first_dirty_count = e->dirty_count;
    for( int i = 0; i < first_dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_CORNER) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        for( int dx = -1; dx <= 0; dx++ )
            for( int dz = -1; dz <= 0; dz++ )
                we_mark(e, x + dx, z + dz, l, WE_D_MESH);
        we_mark(e, x - 1, z, l, WE_D_LIGHT);
        we_mark(e, x + 1, z, l, WE_D_LIGHT);
        we_mark(e, x, z - 1, l, WE_D_LIGHT);
        we_mark(e, x, z + 1, l, WE_D_LIGHT);
        we_victims_for_corner(builder, e, x, z, l);
    }

    /* 2. Underlays: re-blend the +-5 window of every changed colour, as one
     *    rectangle per level. */
    for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
    {
        int x0 = e->scene_size;
        int z0 = e->scene_size;
        int x1 = -1;
        int z1 = -1;
        for( int i = 0; i < first_dirty_count; i++ )
        {
            int t = e->dirty_list[i];
            int x;
            int z;
            int tl;
            if( (e->dirty[t] & WE_D_BLEND) == 0 )
                continue;
            we_tile_of(e, t, &x, &z, &tl);
            if( tl != l )
                continue;
            if( x < x0 )
                x0 = x;
            if( z < z0 )
                z0 = z;
            if( x > x1 )
                x1 = x;
            if( z > z1 )
                z1 = z;
        }
        if( x1 >= 0 )
            we_reblend_rect(
                builder,
                e,
                l,
                x0 - WE_BLEND_RADIUS,
                z0 - WE_BLEND_RADIUS,
                x1 + WE_BLEND_RADIUS,
                z1 + WE_BLEND_RADIUS);
    }

    /* 3. Loc changes, and the wall decor hanging on a changed wall. */
    for( int i = 0; i < e->changed_count; i++ )
    {
        int index = e->changed[i];
        we_victim_add(e, index);
        we_victims_for_wall(e, index);
        we_mark_footprint(builder, e, index, WE_D_COLL);
    }
    for( int v = 0, n = e->victim_count; v < n; v++ )
        we_victims_for_wall(e, e->victims[v]);

    /* 4. Everything whose lighting merged with a changed placement. */
    for( int v = 0, n = e->victim_count; v < n; v++ )
        we_victims_share_neighbours(builder, e, e->victims[v]);

    /* 5. Take every victim down; put back all but the removed, in map order. */
    for( int v = 0; v < e->victim_count; v++ )
    {
        int index = e->victims[v];
        if( e->placements[index].state == WE_P_LIVE || e->placements[index].state == WE_P_REMOVE )
            we_take_down(builder, e, index);
    }
    we_sort_order(e, e->victims, e->victim_count);
    for( int v = 0; v < e->victim_count; v++ )
    {
        int index = e->victims[v];
        struct WorldEditPlacement* pl = &e->placements[index];
        if( pl->state == WE_P_REMOVE )
        {
            e->stats.placements_removed++;
            continue;
        }
        if( pl->state == WE_P_ADD )
            e->stats.placements_added++;
        else
            e->stats.placements_rebuilt++;
        we_put_back(builder, e, index);
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
            if( e->recs[r].type == WE_REC_SHADE )
                we_mark_shade_tiles(e, &e->recs[r]);
        /* A share-lit loc that was not one before, now standing here. */
        if( pl->state == WE_P_ADD )
            for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
                if( e->recs[r].type == WE_REC_LIGHT && e->recs[r].k )
                {
                    if( e->recs[r].v[0] > e->max_share_size )
                        e->max_share_size = e->recs[r].v[0];
                    if( e->recs[r].v[1] > e->max_share_size )
                        e->max_share_size = e->recs[r].v[1];
                }
    }

    /* 6. What the End batch does to them: contour, decor offsets, light. */
    world_contour_ground(builder);
    for( int v = 0; v < e->victim_count; v++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[e->victims[v]];
        if( pl->state == WE_P_REMOVE )
            continue;
        for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
        {
            const struct WorldEditRec* rec = &e->recs[r];
            if( rec->type != WE_REC_DECOR )
                continue;
            world_builder_decor_offset_apply(
                builder,
                rec->element_id,
                rec->k2,
                rec->k,
                we_wall_offset_at(e, rec->x, rec->z, rec->level));
        }
    }
    /* The removed are out of the tile chains before the relight, so nothing
     * merges with them. */
    for( int v = 0; v < e->victim_count; v++ )
    {
        int index = e->victims[v];
        if( e->placements[index].state == WE_P_REMOVE )
        {
            we_unlink(e, index);
            e->placements[index].state = WE_P_DEAD;
            e->placements[index].rec_count = 0;
        }
    }
    we_relight_victims(builder, e);

    /* 7. Shadows: every marked tile is cleared and the survivors' shadows
     *    replayed over it in map order, the last writer winning as in the
     *    bake. A tile whose shade moved relights the corners that read it. */
    {
        int x0 = e->scene_size;
        int z0 = e->scene_size;
        int x1 = -1;
        int z1 = -1;
        int* replay = NULL;
        int replay_count = 0;
        int replay_cap = 0;
        int* before = NULL;
        int before_cap = 0;
        int shade_count = 0;

        for( int i = 0; i < e->dirty_count; i++ )
        {
            int t = e->dirty_list[i];
            int x;
            int z;
            int l;
            if( (e->dirty[t] & WE_D_SHADE) == 0 )
                continue;
            we_tile_of(e, t, &x, &z, &l);
            before = we_grow(before, &before_cap, shade_count + 1, sizeof(int));
            before[shade_count++] = shademap2_get(builder->shademap, x, z, l);
            builder->shademap->map[x + z * builder->shademap->width +
                                   l * builder->shademap->width * builder->shademap->height] = 0;
            if( x < x0 )
                x0 = x;
            if( z < z0 )
                z0 = z;
            if( x > x1 )
                x1 = x;
            if( z > z1 )
                z1 = z;
        }
        if( shade_count > 0 )
        {
            for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
                for( int ox = x0 - e->max_size; ox <= x1; ox++ )
                    for( int oz = z0 - e->max_size; oz <= z1; oz++ )
                    {
                        if( !we_in_scene(e, ox, oz) )
                            continue;
                        for( int i = e->tile_head[we_tile_idx(e, ox, oz, l)]; i != -1;
                             i = e->placements[i].next_in_tile )
                        {
                            replay = we_grow(replay, &replay_cap, replay_count + 1, sizeof(int));
                            replay[replay_count++] = i;
                        }
                    }
            we_sort_order(e, replay, replay_count);
            for( int k = 0; k < replay_count; k++ )
            {
                const struct WorldEditPlacement* pl = &e->placements[replay[k]];
                for( int r = pl->rec_first; r < pl->rec_first + pl->rec_count; r++ )
                {
                    const struct WorldEditRec* rec = &e->recs[r];
                    if( rec->type != WE_REC_SHADE )
                        continue;
                    /* The cells shademap2_set_sized / _wall / _wall_corner
                     * write, kept to the ones this flush cleared: every other
                     * cell already holds its final value. */
                    {
                        int cells[16][3];
                        int ncells = 0;
                        int x = rec->x;
                        int z = rec->z;
                        int l2 = rec->level;
                        if( rec->k == WORLD_EDIT_SHADE_SIZED )
                        {
                            for( int dx = 0; dx < rec->v[0]; dx++ )
                                for( int dz = 0; dz < rec->v[1]; dz++ )
                                {
                                    int cx = x + dx;
                                    int cz = z + dz;
                                    if( we_in_scene(e, cx, cz) &&
                                        (e->dirty[we_tile_idx(e, cx, cz, l2)] & WE_D_SHADE) != 0 )
                                        builder->shademap
                                            ->map[cx + cz * builder->shademap->width +
                                                  l2 * builder->shademap->width *
                                                      builder->shademap->height] = rec->v[2];
                                }
                            continue;
                        }
                        /* shademap2_set_wall / _wall_corner, cell list. */
                        if( rec->k == WORLD_EDIT_SHADE_WALL )
                        {
                            switch( rec->v[0] & 3 )
                            {
                            case 0:
                                cells[ncells][0] = x, cells[ncells][1] = z, ncells++;
                                cells[ncells][0] = x, cells[ncells][1] = z + 1, ncells++;
                                break;
                            case 1:
                                cells[ncells][0] = x, cells[ncells][1] = z + 1, ncells++;
                                cells[ncells][0] = x + 1, cells[ncells][1] = z + 1, ncells++;
                                break;
                            case 2:
                                cells[ncells][0] = x + 1, cells[ncells][1] = z, ncells++;
                                cells[ncells][0] = x + 1, cells[ncells][1] = z + 1, ncells++;
                                break;
                            default:
                                cells[ncells][0] = x, cells[ncells][1] = z, ncells++;
                                cells[ncells][0] = x + 1, cells[ncells][1] = z, ncells++;
                                break;
                            }
                        }
                        else
                        {
                            switch( rec->v[0] & 3 )
                            {
                            case 0:
                                cells[ncells][0] = x, cells[ncells][1] = z + 1, ncells++;
                                break;
                            case 1:
                                cells[ncells][0] = x + 1, cells[ncells][1] = z + 1, ncells++;
                                break;
                            case 2:
                                cells[ncells][0] = x + 1, cells[ncells][1] = z, ncells++;
                                break;
                            default:
                                cells[ncells][0] = x, cells[ncells][1] = z, ncells++;
                                break;
                            }
                        }
                        for( int c = 0; c < ncells; c++ )
                        {
                            int cx = cells[c][0];
                            int cz = cells[c][1];
                            if( we_in_scene(e, cx, cz) &&
                                (e->dirty[we_tile_idx(e, cx, cz, l2)] & WE_D_SHADE) != 0 )
                                builder->shademap
                                    ->map[cx + cz * builder->shademap->width +
                                          l2 * builder->shademap->width *
                                              builder->shademap->height] = rec->v[2];
                        }
                    }
                }
            }
            /* Corners reading a moved shade relight. */
            shade_count = 0;
            for( int i = 0; i < e->dirty_count; i++ )
            {
                int t = e->dirty_list[i];
                int x;
                int z;
                int l;
                if( (e->dirty[t] & WE_D_SHADE) == 0 )
                    continue;
                we_tile_of(e, t, &x, &z, &l);
                if( shademap2_get(builder->shademap, x, z, l) != before[shade_count++] )
                {
                    we_mark(e, x, z, l, WE_D_LIGHT);
                    we_mark(e, x - 1, z, l, WE_D_LIGHT);
                    we_mark(e, x + 1, z, l, WE_D_LIGHT);
                    we_mark(e, x, z - 1, l, WE_D_LIGHT);
                    we_mark(e, x, z + 1, l, WE_D_LIGHT);
                }
            }
        }
        ToriMem_Free(ToriMem_Scene(), replay);
        ToriMem_Free(ToriMem_Scene(), before);
    }

    /* 8. Relight the marked corners; a corner whose light moved remeshes the
     *    four tiles sharing it. */
    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        int light;
        if( (e->dirty[t] & WE_D_LIGHT) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        light = world_terrain_shaded_light(builder, x, z, l, we_raw_light(builder, x, z, l));
        e->stats.corners_relit++;
        if( lightmap_get(builder->lightmap, x, z, l) == light )
            continue;
        lightmap_set(builder->lightmap, x, z, l, (uint8_t)light);
        for( int dx = -1; dx <= 0; dx++ )
            for( int dz = -1; dz <= 0; dz++ )
                we_mark(e, x + dx, z + dz, l, WE_D_MESH);
    }

    /* 9. Remesh. */
    any_geometry = e->victim_count > 0;
    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_MESH) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        we_remesh_tile(builder, e, x, z, l);
        any_geometry = 1;
    }

    /* 10. Collision, minimap walls and sounds of changed loc tiles, flags. */
    we_restamp_collision(builder, e);
    for( int i = 0; i < e->changed_count; i++ )
    {
        const struct WorldEditPlacement* pl = &e->placements[e->changed[i]];
        if( pl->in_scene )
            we_refresh_tile_locs(builder, e, pl->p.scene_x, pl->p.scene_z, pl->p.level);
    }
    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_FLAGS) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        we_column_draw_levels(builder, x, z);
    }

    for( int i = 0; i < e->dirty_count; i++ )
    {
        int t = e->dirty_list[i];
        int x;
        int z;
        int l;
        if( (e->dirty[t] & WE_D_MMCOL) == 0 )
            continue;
        we_tile_of(e, t, &x, &z, &l);
        we_minimap_column(builder, e, x, z);
    }

    /* 11. Planar occluders are a whole-window greedy merge of marks the bake
     *     consumed; until a rebake re-merges them, an edited window draws
     *     without them rather than with stale ones. */
    if( any_geometry && !e->occluders_dropped && world->painter )
    {
        struct SceneOccluders* occ = painter_get_occluders(world->painter);
        if( occ )
            scene_occluders_clear(occ);
        e->occluders_dropped = 1;
    }

    for( int i = 0; i < e->changed_count; i++ )
        if( e->placements[e->changed[i]].state == WE_P_ADD )
            e->placements[e->changed[i]].state = WE_P_LIVE;
    if( any_geometry )
        world->geometry_serial++;
    if( world->minimap && e->stats.tiles_remeshed > 0 )
        world->minimap_seq++;
    we_clear_pending(e);
    we_compact_records(e);
    e->stats.ms = wb_now_ms() - t0;
    return WORLD_BUILDER_FLUSH_OK;
}

static void
world_edit_rebuild_window(
    struct WorldBuilder* builder,
    const struct WorldBuilderWindow* window)
{
    /* The same bake as STATIC; keep_build_maps and the hooks are the
     * difference, and they are set for as long as the builder is EDIT. */
    world_builder_static_rebuild_window(builder, window);
}

const struct WorldBuilderOps g_world_builder_ops_edit = {
    .name = "edit",
    .rebuild_window = world_edit_rebuild_window,
    .tile_set = world_edit_tile_set,
    .loc_place = world_edit_loc_place,
    .loc_remove = world_edit_loc_remove,
    .flush = world_edit_flush,
};

/* ---- layout switch, queries ------------------------------------------------ */

void
WorldBuilder_SetLayout(
    struct WorldBuilder* builder,
    enum WorldBuilderLayout layout)
{
    assert(builder);
    if( layout == WORLD_BUILDER_LAYOUT_EDIT )
    {
        if( !builder->edit )
        {
            builder->edit = ToriMem_Calloc(ToriMem_Scene(), 1, sizeof(*builder->edit));
            assert(builder->edit);
            builder->edit->capture = -1;
        }
        builder->ops = &g_world_builder_ops_edit;
        builder->keep_build_maps = 1;
        return;
    }
    assert(layout == WORLD_BUILDER_LAYOUT_STATIC);
    if( builder->edit )
    {
        struct WorldEdit* e = builder->edit;
        ToriMem_Free(ToriMem_Scene(), e->floors);
        ToriMem_Free(ToriMem_Scene(), e->tile_head);
        ToriMem_Free(ToriMem_Scene(), e->dirty);
        ToriMem_Free(ToriMem_Scene(), e->dirty_list);
        ToriMem_Free(ToriMem_Scene(), e->squares);
        ToriMem_Free(ToriMem_Scene(), e->placements);
        ToriMem_Free(ToriMem_Scene(), e->recs);
        ToriMem_Free(ToriMem_Scene(), e->changed);
        ToriMem_Free(ToriMem_Scene(), e->victims);
        ToriMem_Free(ToriMem_Scene(), e);
        builder->edit = NULL;
    }
    builder->ops = &g_world_builder_ops_static;
    builder->keep_build_maps = 0;
}

int
WorldEdit_Ready(const struct WorldBuilder* builder)
{
    assert(builder);
    return builder->edit && builder->edit->ready;
}

uint32_t
WorldEdit_BakeSerial(const struct WorldBuilder* builder)
{
    assert(builder);
    assert(builder->edit);
    return builder->edit->bake_serial;
}

int
WorldEdit_SquarePlacement(
    const struct WorldBuilder* builder,
    int map_x,
    int map_z,
    int index)
{
    const struct WorldEdit* e;
    int square = -1;
    int placement;

    assert(builder);
    assert(builder->edit);
    e = builder->edit;
    for( int i = 0; i < e->square_count; i++ )
        if( e->squares[i].map_x == map_x && e->squares[i].map_z == map_z )
            square = i;
    if( square < 0 || index < 0 || index >= e->squares[square].bake_count )
        return WORLD_BUILDER_HANDLE_NONE;
    placement = e->squares[square].first_placement + index;
    if( e->placements[placement].state != WE_P_LIVE )
        return WORLD_BUILDER_HANDLE_NONE;
    return we_make_handle(e, placement);
}

int
WorldEdit_SquareInWindow(
    const struct WorldBuilder* builder,
    int map_x,
    int map_z)
{
    const struct WorldEdit* e;

    assert(builder);
    assert(builder->edit);
    e = builder->edit;
    if( !e->ready )
        return 0;
    for( int i = 0; i < e->square_count; i++ )
        if( e->squares[i].map_x == map_x && e->squares[i].map_z == map_z )
            return e->squares[i].in_bake;
    return 0;
}

int
WorldEdit_FindPlacement(
    const struct WorldBuilder* builder,
    int scene_x,
    int scene_z,
    int level,
    int shape)
{
    const struct WorldEdit* e;
    int best = -1;

    assert(builder);
    assert(builder->edit);
    assert(level >= 0);
    assert(level < WORLD_MAP_TERRAIN_LEVELS);
    e = builder->edit;
    /* Nothing is placed before a bake. */
    if( !e->ready )
        return WORLD_BUILDER_HANDLE_NONE;
    assert(we_in_scene(e, scene_x, scene_z));
    for( int i = e->tile_head[we_tile_idx(e, scene_x, scene_z, level)]; i != -1;
         i = e->placements[i].next_in_tile )
    {
        const struct WorldEditPlacement* pl = &e->placements[i];
        if( (pl->state != WE_P_LIVE && pl->state != WE_P_ADD) || pl->p.shape != shape )
            continue;
        if( best < 0 || we_order_cmp(e, i, best) < 0 )
            best = i;
    }
    return best < 0 ? WORLD_BUILDER_HANDLE_NONE : we_make_handle(e, best);
}

int
WorldEdit_PlacementGet(
    const struct WorldBuilder* builder,
    int handle,
    struct World_LocPlacement* out)
{
    int index;

    assert(builder);
    assert(builder->edit);
    assert(out);
    index = we_handle_index(builder->edit, handle);
    if( index < 0 )
        return 0;
    if( builder->edit->placements[index].state != WE_P_LIVE &&
        builder->edit->placements[index].state != WE_P_ADD )
        return 0;
    *out = builder->edit->placements[index].p;
    return 1;
}

void
WorldEdit_LastFlushStats(
    const struct WorldBuilder* builder,
    struct WorldEditFlushStats* out)
{
    assert(builder);
    assert(builder->edit);
    assert(out);
    *out = builder->edit->stats;
}

#endif
