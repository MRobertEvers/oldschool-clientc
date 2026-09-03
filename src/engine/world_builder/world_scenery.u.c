#ifndef WORLD_SCENERY_U_C
#define WORLD_SCENERY_U_C

#include "engine/cache_provider.h"
#include "painters/painters.h"
#include "engine/toridraw_model_from_torirs.h"
#include "minimap.h"
#include "occluder_buildmap.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "toridraw_light_model.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_scene.h"
#include "varp/varp_manager.h"
#include "world_builder.h"
#include "world_scenery_mapfuncs.h"
#include <rscache.h>

// clang-format off
#include "world_contour_ground.u.c"
// clang-format on

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALL_DECOR_YAW_ADJUST 256
#define WORLD_TILE_SIZE 128
#define WORLD_CURRENT_LEVEL 0

static const int WALL_DECOR_ROTATION_OFFSET_X[] = { 1, 0, -1, 0 };
static const int WALL_DECOR_ROTATION_OFFSET_Z[] = { 0, -1, 0, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[] = { 1, -1, -1, 1 };
static const int WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[] = { -1, -1, 1, 1 };

static const int ROTATION_WALL_TYPE[] = {
    WALL_SIDE_WEST, WALL_SIDE_NORTH, WALL_SIDE_EAST, WALL_SIDE_SOUTH
};
static const int ROTATION_WALL_CORNER_TYPE[] = {
    WALL_CORNER_NORTHWEST,
    WALL_CORNER_NORTHEAST,
    WALL_CORNER_SOUTHEAST,
    WALL_CORNER_SOUTHWEST,
};

static struct ToriRS_Location*
world_builder_resolve_loc(
    struct WorldBuilder* builder,
    struct ToriRS_Location* base_loc)
{
    int resolved_id;

    assert(base_loc);
    if( base_loc->transform_count <= 0 || !base_loc->transforms )
        return base_loc;

    resolved_id = VarPManager_ResolveTransform(
        builder->varp,
        base_loc->transforms,
        base_loc->transform_count,
        base_loc->transform_varbit,
        base_loc->transform_varp);
    if( resolved_id < 0 )
        return NULL;

    return CacheProvider_LocationGet(builder->cache, resolved_id);
}

/**
 * Resolve a multiloc for placement.
 *
 * The transform selects the MODEL; the fields the placement math reads stay
 * the BASE definition's. The reference resolves inside getModel and leaves the
 * caller reading sizeX/sizeY and the anim id off the base — and placement is
 * `tile*128 + size*64`, so borrowing the target's footprint re-centres the
 * model over the wrong span (a base-4x1 loc placed as its 1x1 target lands 192
 * units west of where it belongs, geometry still four tiles wide).
 *
 * The anim id does NOT follow that rule, and used to. What makes a transformed
 * loc a DynamicObject is the BASE `animationId` *or* the presence of a
 * transform table, and the frame it draws comes from the def the transform
 * resolved to -- the reference re-reads the transformed `animationId` and
 * re-arms when it differs, which is how a state that animates only in one rung
 * animates at all. Taking the base's unconditionally meant a base with no
 * `anim=` of its own froze every animated child: 474 multiloc families in this
 * cache are exactly that shape (`blast_furnace_dispenser`, `golem_portal`, the
 * mourning doors, and every canoe station -- the felled tree never fell and the
 * canoe in the water never bobbed).
 *
 * The base stays as the FALLBACK rather than being dropped, so the 445 families
 * whose shell carries the anim and whose children carry none keep animating
 * exactly as before. What changes is "the child has one and the base does not"
 * -- and "both have one", where the state's own anim is the one that belongs to
 * the state being drawn.
 *
 * Writes through `storage` (caller-owned, so this allocates nothing) and
 * returns it. NULL means the varbit selected a state with no loc — the caller
 * must skip the instance, not fall back to the base.
 */
static struct ToriRS_Location*
world_builder_resolve_loc_for_place(
    struct WorldBuilder* builder,
    struct ToriRS_Location* base_loc,
    struct ToriRS_Location* storage)
{
    struct ToriRS_Location* resolved;

    assert(base_loc);

    resolved = world_builder_resolve_loc(builder, base_loc);
    if( !resolved )
        return NULL;

    if( WB_ENV_SCENERY_DEBUG() &&
        (resolved->size_x != base_loc->size_x || resolved->size_z != base_loc->size_z) )
        fprintf(
            stderr,
            "  multiloc footprint: loc %d base %dx%d -> target %d is %dx%d "
            "(base wins; target size would shift it %d units west/south)\n",
            base_loc->id,
            base_loc->size_x,
            base_loc->size_z,
            resolved->id,
            resolved->size_x,
            resolved->size_z,
            64 * (base_loc->size_x - resolved->size_x));

    *storage = *resolved;
    if( resolved->seq_id < 0 )
        storage->seq_id = base_loc->seq_id;
    storage->size_x = base_loc->size_x;
    storage->size_z = base_loc->size_z;
    return storage;
}

static void
calculate_wall_decor_offset(
    struct ToriDraw_Position* position,
    int orientation,
    int offset,
    bool diagonal)
{
    assert(orientation >= 0);
    assert(orientation < 4);

    int x_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_X[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_X[orientation];
    int z_multiplier = diagonal ? WALL_DECOR_ROTATION_DIAGONAL_OFFSET_Z[orientation]
                                : WALL_DECOR_ROTATION_OFFSET_Z[orientation];
    position->x += offset * x_multiplier;
    position->z += offset * z_multiplier;
}

void
world_builder_apply_wall_decor_offsets(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    assert(world);
    assert(builder->decor_buildmap);
    assert(builder->scene);

    int scene_size = world->_scene_size;
    for( int sx = 0; sx < scene_size; sx++ )
    {
        for( int sz = 0; sz < scene_size; sz++ )
        {
            for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
            {
                int wall_width =
                    decor_buildmap_get_wall_offset(builder->decor_buildmap, sx, sz, level);
                struct DecorElementsOnWall* elements =
                    decor_buildmap_get_elements(builder->decor_buildmap, sx, sz, level);

                for( int i = 0; i < elements->count; i++ )
                {
                    int element_id = elements->element_id[i];
                    int displacement_kind = elements->displacement_kind[i];
                    int orientation = elements->orientation[i];
                    struct ToriDraw_SceneElement* scene_element =
                        ToriDraw_SceneElementGet(builder->scene, element_id);
                    if( !scene_element )
                        continue;

                    bool diagonal = false;
                    int offset = 0;
                    switch( displacement_kind )
                    {
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET:
                        offset += wall_width;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET:
                        offset += (wall_width / 16) * 8 + 45;
                        diagonal = true;
                        break;
                    case DECOR_DISPLACEMENT_KIND_STRAIGHT:
                        offset += 0;
                        break;
                    case DECOR_DISPLACEMENT_KIND_DIAGONAL:
                        offset += 45;
                        diagonal = true;
                        break;
                    default:
                        break;
                    }

                    calculate_wall_decor_offset(
                        &scene_element->world_position, orientation, offset, diagonal);
                }
            }
        }
    }
}

/*
 * Shade / occluder / decor accumulators: build-only, and the scenery_add_*
 * family below has two callers.
 *
 * A static build allocates all three in WorldBuilder_Rebuild*Begin and frees
 * them once their bake has been applied. A runtime loc spawn
 * (WorldBuilder_ApplyLocChange -> scenery_add, `scenery_runtime_spawn`) reuses
 * that same code with no build in flight: there is no accumulator to write to,
 * and nothing downstream would read one — the spawned loc is drawn by
 * world_cycle's per-frame scenery pass and lit on the spot
 * (scenery_register_sharelight). "Is a build in flight?" is knowledge the
 * builder holds and the maps do not, so the test lives here, at the one point
 * every scenery_add_* writer passes through, rather than in the map modules —
 * they assert their pointer, so a genuine bad pointer still stops there.
 */
static void
scenery_shade_wall(
    struct WorldBuilder* builder,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade)
{
    if( !builder->shademap )
        return;
    shademap2_set_wall(builder->shademap, sx, sz, slevel, orientation, shade);
}

static void
scenery_shade_wall_corner(
    struct WorldBuilder* builder,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade)
{
    if( !builder->shademap )
        return;
    shademap2_set_wall_corner(builder->shademap, sx, sz, slevel, orientation, shade);
}

static void
scenery_shade_sized(
    struct WorldBuilder* builder,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    int shade)
{
    if( !builder->shademap )
        return;
    shademap2_set_sized(builder->shademap, sx, sz, slevel, size_x, size_z, shade);
}

static void
scenery_occluder_mark(struct WorldBuilder* builder, int x, int z, int level, uint16_t bits)
{
    if( !builder->occluder_buildmap )
        return;
    occluder_buildmap_or_mark(builder->occluder_buildmap, x, z, level, bits);
}

static void
scenery_decor_set_wall_offset(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int wall_offset)
{
    if( !builder->decor_buildmap )
        return;
    decor_buildmap_set_wall_offset(builder->decor_buildmap, x, z, level, wall_offset);
}

static void
scenery_decor_add_element(
    struct WorldBuilder* builder,
    int x,
    int z,
    int level,
    int element_id,
    int orientation,
    enum DecorDisplacementKind displacement_kind)
{
    if( !builder->decor_buildmap )
        return;
    decor_buildmap_add_element(
        builder->decor_buildmap, x, z, level, element_id, orientation, displacement_kind);
}

static inline enum MinimapWallFlag
orientation_wall_flag(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_WEST;
    case 1:
        return MINIMAP_WALL_NORTH;
    case 2:
        return MINIMAP_WALL_EAST;
    case 3:
        return MINIMAP_WALL_SOUTH;
    default:
        return MINIMAP_WALL_NONE;
    }
}

static inline enum MinimapWallFlag
orientation_wall_flag_diagonal(int orientation)
{
    switch( orientation & 0x3 )
    {
    case 0:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 1:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    case 2:
        return MINIMAP_WALL_NORTHEAST_SOUTHWEST;
    case 3:
        return MINIMAP_WALL_NORTHWEST_SOUTHEAST;
    default:
        return MINIMAP_WALL_NONE;
    }
}

/* Minimap wall-line bits for one loc (reference drawDetail wall branches):
 * straight walls one edge, L-walls two edges, diagonal walls one diagonal;
 * corners draw nothing here. An interactive (active) wall — a door — shifts
 * into the DOOR_* bits so the bake draws its line red instead of white
 * (reference: typecode > 0 picks activeRgb). Shared by the static gather and
 * the runtime loc change. */
static int
scenery_minimap_wall_flags(
    int shape,
    int orientation,
    int interactive)
{
    int flags;
    switch( shape )
    {
    case RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE:
        flags = orientation_wall_flag(orientation);
        break;
    case RSCACHE_LOC_SHAPE_WALL_TWO_SIDES:
        flags = orientation_wall_flag(orientation) |
                orientation_wall_flag((orientation + 1) & 0x3);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DIAGONAL:
        flags = orientation_wall_flag_diagonal(orientation);
        break;
    default:
        return 0;
    }
    if( interactive )
        flags <<= MINIMAP_DOOR_SHIFT;
    return flags;
}

/*
 * True when a loc's built model is identical for every placement of the same
 * (resolved id, shape, rotation) — the set the scenery model prototype cache
 * may serve (scenery_load_model). Three placement-dependent effects disqualify:
 * contour ground deforms the vertices to the tile heights under each instance,
 * sharelight merges vertex normals with whatever happens to stand next door,
 * and a seq animates the instance's own copy every frame.
 *
 * Both scenery_load_model (build + pre-light + cache) and
 * scenery_register_sharelight (skip the End-batch lighting for pre-lit models)
 * key off this one predicate; if they ever disagree a model is lit twice or
 * not at all.
 */
/* Why a placement had to build its own model (TORIRS_SCENERY_CENSUS=1). */
int g_wb_share_hit;
int g_wb_share_clone;
int g_wb_share_proto;
int g_wb_share_no_contour;
int g_wb_share_no_sharelight;
int g_wb_share_no_seq;

static bool
scenery_loc_model_shareable(const struct ToriRS_Location* loc)
{
    assert(loc);
    /* seq test spelled as the shape helpers spell theirs
     * (`seq_id != -1 ? orientation : 0`), so "has a deferred draw-time angle"
     * and "not shareable" can never disagree on an odd negative id. */
    return loc->contour_ground_type == 0 && loc->sharelight == 0 && loc->seq_id == -1;
}

/*
 * Key a loc's geometry within one build: the resolved config id, which shape of
 * it was selected, and the rotation baked into the vertices.
 *
 * Everything a build does to a loc model past that point -- the model ids it
 * merges, the recolours and retextures, the mirror, the resize, the offset --
 * is read off the same config, so two placements agreeing on these three agree
 * on the finished geometry. Placement enters only through contouring, borrowed
 * lighting and animation, and those are exactly what
 * scenery_loc_model_shareable rules out.
 *
 * One key, two stores. A whole-model prototype and a lendable face-buffer set
 * can exist for the same loc, and they used to be told apart by a tag bit in
 * this key that both callers had to remember to set -- with a faces-only entry
 * returned to a whole-model Acquire being a model with no vertices. They are
 * different types in different stores now (ToriDraw_SceneSharedModels,
 * ToriDraw_SceneSharedFaces) and cannot collide.
 */
static int64_t
scenery_model_key(
    int loc_id,
    int shape_select,
    int rotation)
{
    return ((int64_t)loc_id << 9) | ((int64_t)(shape_select & 0x1F) << 4) |
           (int64_t)(rotation & 0xF);
}

static void
scenery_register_sharelight(
    struct WorldBuilder* builder,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z,
    int level,
    int element_id,
    int size_x,
    int size_z)
{

    /* Prototype-cacheable locs were lit when their model was built
     * (scenery_load_model) — the End-batch defaultlight pass would only
     * recompute the identical colours. */
    if( scenery_loc_model_shareable(config_loc) )
        return;

    /* Runtime loc spawn (zone LOC_ADD_CHANGE): the sharelight accumulator is
     * build-only (already freed), so the batch defaultlight_build pass will
     * never see this model — light it now or it renders black. Reference: a
     * runtime loc.getModel bakes the default per-loc light for non-sharelight
     * locs (calculateNormals doNotShareLight=true -> light()); the adjacency
     * normal merge + final light for sharelight locs is World.shareLight, a
     * static-build-only pass — a runtime sharelight spawn stays UNLIT there
     * until the next rebuild. torirs deliberately default-lights those too
     * rather than reproduce that artifact. */
    if( builder->scenery_runtime_spawn )
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(builder->scene, element_id);
        if( el && ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model &&
            ToriDraw_ModelIsLightable(el->model.u.model.model) )
        {
            struct ToriDraw_ModelHandle hnd = {
                .kind = TORIDRAWMK_MODEL,
                .u.model.model = el->model.u.model.model,
            };
            ToriDraw_LightModelScene(hnd, config_loc->contrast, config_loc->ambient);
            ToriDraw_ModelFreeNormals(el->model.u.model.model);
        }
        return;
    }

    sharelight_map_push(
        builder->sharelight_map,
        config_loc->sharelight != 0,
        scene_x,
        scene_z,
        level,
        element_id,
        size_x,
        size_z,
        config_loc->ambient,
        config_loc->contrast);
}

/* Runtime wall spawn: painter_add_wall is suppressed (its static slot is
 * baked), so capture the (WALL_A/B, side) the build path would have used on
 * the pool entry — world_cycle's per-frame re-registration replays it via
 * painter_add_wall instead of painter_add_normal_scenery, keeping the wall on
 * the correct side of the tile in the painter's draw order. */
static void
scenery_record_runtime_wall(
    struct WorldBuilder* builder,
    int element_id,
    int wall_ab,
    int side)
{
    struct WorldEntity_Scenery* scenery;
    if( !builder->scenery_runtime_spawn || element_id < 0 )
        return;
    scenery = World_SceneryGetByElementId(builder->world, element_id);
    if( scenery )
    {
        scenery->painter_wall_ab = wall_ab;
        scenery->painter_wall_side = side;
    }
}

/* Runtime ground-decor spawn: painter_add_ground_decor is suppressed (its
 * static slot is baked), so mark the pool entry — world_cycle's per-frame
 * re-registration replays it via painter_add_ground_decor_dynamic instead of
 * painter_add_normal_scenery, which is what puts the puddle in the tile's base
 * step and therefore underneath anything standing on it. */
static void
scenery_record_runtime_ground_decor(
    struct WorldBuilder* builder,
    int element_id)
{
    struct WorldEntity_Scenery* scenery;
    if( !builder->scenery_runtime_spawn || element_id < 0 )
        return;
    scenery = World_SceneryGetByElementId(builder->world, element_id);
    if( scenery )
        scenery->painter_ground_decor = 1;
}

/* Loc recolour endpoints <= this are texture ids (retexture), not HSL colours.
 * The reference stores a textured face's texture id in the same faceColour field
 * a recolour pass remaps, so the two operations are one pass partitioned purely
 * by value range: texture ids occupy 0..50, HSL colours are always > 50. */
#define LOC_RECOLOUR_TEXTURE_MAX 50

void
world_builder_prerotate_placement(
    int quarter_turns,
    int* resize_x,
    int* resize_z,
    int* offset_x,
    int* offset_z)
{
    /* ToriDraw_ModelOrient / Model.rotate90 is x' = z, z' = -x, so the inverse
     * is x' = -z, z' = x. Applied once per deferred quarter turn. */
    for( int i = 0; i < (quarter_turns & 3); i++ )
    {
        int const ox = -*offset_z;
        int const sx = *resize_z;
        *offset_z = *offset_x;
        *offset_x = ox;
        *resize_z = *resize_x;
        *resize_x = sx;
    }
}

static void
apply_transforms(
    struct ToriRS_Location* loc,
    struct ToriDraw_Model* model,
    int orientation,
    int deferred_angle)
{
    /* Client-TS LocType.getModel calls Model.recolour(src,dst), which remaps
     * faceColour; textured faces render with faceColour AS the texture id
     * (Model.ts:2138/2154). One recolour pass therefore both recolours gouraud
     * faces and retextures textured faces, distinguished only by the value
     * range. The C decoder split the texture id out of face_colors into
     * face_textures (model.c:324, colour reset to 127), so replicate the split
     * explicitly: a pair with both endpoints <= 50 is a texture swap; anything
     * else is an HSL recolour. True for the old revision this cache uses; when
     * it stopped being the case upstream is unknown. */
    for( int i = 0; i < loc->recolor_count; i++ )
    {
        int from = loc->recolors_from[i];
        int to = loc->recolors_to[i];
        if( from <= LOC_RECOLOUR_TEXTURE_MAX && to <= LOC_RECOLOUR_TEXTURE_MAX )
            ToriDraw_ModelRetexture(model, from, to);
        else
            ToriDraw_ModelRecolor(model, from, to);
    }

    for( int i = 0; i < loc->retexture_count; i++ )
        ToriDraw_ModelRetexture(model, loc->retextures_from[i], loc->retextures_to[i]);

    bool mirrored = (loc->mirrored != (orientation > 3));
    bool oriented = orientation != 0;
    bool scaled = loc->resize_x != 128 || loc->resize_height != 128 || loc->resize_z != 128;
    bool translated = loc->offset_x != 0 || loc->offset_y != 0 || loc->offset_z != 0;

    /* Reference order is rotate -> resize -> translate. When the rotation is
     * deferred to a draw-time yaw (animated locs), applying resize/offset here
     * would put them in the unrotated frame; pre-rotate so the deferred yaw
     * carries them where the reference puts them. Exactly right at the
     * animation's rest pose, and a no-op for the uniform resizes and y-only
     * offsets that every loc in the shipped caches happens to use. */
    int resize_x = loc->resize_x;
    int resize_z = loc->resize_z;
    int offset_x = loc->offset_x;
    int offset_z = loc->offset_z;

    world_builder_prerotate_placement(
        deferred_angle, &resize_x, &resize_z, &offset_x, &offset_z);

    if( mirrored )
        ToriDraw_ModelMirror(model);
    if( oriented )
        ToriDraw_ModelOrient(model, orientation);
    if( scaled )
        ToriDraw_ModelScale(model, resize_x, resize_z, loc->resize_height);
    if( translated )
        ToriDraw_ModelTranslate(model, offset_x, loc->offset_y, offset_z);
}

/* TORIRS_SCENERY_DEBUG: scene elements this build actually produced. An instance
 * reaching scenery_add is not the same as geometry landing in the scene — the
 * per-shape helpers can drop it at several points below. */
static int g_scenery_dbg_elements;

#define SCENERY_DBG_RING                                                                           \
    ((int)(sizeof(((struct WorldBuilder*)0)->scenery_dbg_element) /                                 \
           sizeof(((struct WorldBuilder*)0)->scenery_dbg_element[0])))

/* Record everything the scene slot was derived from on the pool entry
 * (WorldEntity_SceneryDebug), and remember element_id -> pool index so the
 * position pass below can finish the record with where the model actually
 * landed. Always on: the fields are ~60 bytes on an entity that already
 * carries a 5x32 action table, and capturing them only under the env var
 * would mean a misplacement can never be inspected without a rebuild. */
static void
scenery_debug_record(
    struct WorldBuilder* builder,
    struct WorldEntity_Scenery* scenery,
    struct ToriRS_MapLoc* map_tile,
    struct ToriRS_Location* config_loc,
    const struct ToriDraw_Model* model,
    int element_id,
    int pool_idx,
    int size_x,
    int size_z)
{
    struct WorldEntity_SceneryDebug* dbg = &scenery->debug;
    int slot;

    dbg->runtime = builder->scenery_runtime_spawn ? 1 : 0;
    /* A runtime spawn has no source square, and its "chunk" coords are already
     * scene coords (WorldBuilder_ApplyLocChange synthesises the ToriRS_MapLoc
     * from the zone packet's scene tile). */
    dbg->map_square_x = dbg->runtime ? -1 : builder->scenery_mapx;
    dbg->map_square_z = dbg->runtime ? -1 : builder->scenery_mapz;
    dbg->chunk_x = map_tile->chunk_pos_x;
    dbg->chunk_z = map_tile->chunk_pos_z;
    dbg->chunk_level = map_tile->chunk_pos_level;
    dbg->base_tile_x = builder->world->_base_tile_x;
    dbg->base_tile_z = builder->world->_base_tile_z;
    dbg->config_size_x = config_loc->size_x;
    dbg->config_size_z = config_loc->size_z;
    dbg->draw_size_x = size_x;
    dbg->draw_size_z = size_z;
    dbg->draw_x = 0;
    dbg->draw_y = 0;
    dbg->draw_z = 0;
    dbg->draw_yaw = 0;

    /* Post-transform geometry extent — the model as it will actually draw. */
    dbg->model_min_x = dbg->model_max_x = 0;
    dbg->model_min_z = dbg->model_max_z = 0;
    if( model && model->vertex_count > 0 )
    {
        dbg->model_min_x = dbg->model_max_x = model->vertices_x[0];
        dbg->model_min_z = dbg->model_max_z = model->vertices_z[0];
        for( int v = 1; v < model->vertex_count; v++ )
        {
            if( model->vertices_x[v] < dbg->model_min_x )
                dbg->model_min_x = model->vertices_x[v];
            if( model->vertices_x[v] > dbg->model_max_x )
                dbg->model_max_x = model->vertices_x[v];
            if( model->vertices_z[v] < dbg->model_min_z )
                dbg->model_min_z = model->vertices_z[v];
            if( model->vertices_z[v] > dbg->model_max_z )
                dbg->model_max_z = model->vertices_z[v];
        }
    }

    slot = builder->scenery_dbg_next % SCENERY_DBG_RING;
    builder->scenery_dbg_element[slot] = element_id;
    builder->scenery_dbg_pool[slot] = pool_idx;
    builder->scenery_dbg_next = slot + 1;
}

/* Second half of the record: where the element was actually placed. Split
 * because the position is only known after the model loads (it needs the
 * heightmap), and one shape helper positions two elements. */
static void
scenery_debug_note_position(
    struct WorldBuilder* builder,
    int element_id,
    int x,
    int y,
    int z,
    int yaw)
{
    for( int i = 0; i < SCENERY_DBG_RING; i++ )
    {
        struct WorldEntity_Scenery* scenery;
        if( builder->scenery_dbg_element[i] != element_id )
            continue;
        scenery = World_EntityPoolGet(
            &builder->world->entities.scenery, builder->scenery_dbg_pool[i]);
        /* The ring outlives the entry only if the pool recycled it, which the
         * build path never does mid-shape — verify anyway rather than stamp a
         * different loc's record. */
        if( !scenery || scenery->element_id != element_id )
            return;
        scenery->debug.draw_x = x;
        scenery->debug.draw_y = y;
        scenery->debug.draw_z = z;
        scenery->debug.draw_yaw = yaw;
        return;
    }
}

/* Convert + merge + transform one loc model, exactly as scenery_load_model
 * always has — split out so the prototype-cache hit path can skip it whole.
 * NULL: shape absent, model not loaded, or empty geometry (the instance is
 * silently dropped, as before). */
static struct ToriDraw_Model*
scenery_build_loc_model(
    struct WorldBuilder* builder,
    struct ToriRS_Location* config_loc,
    int shape_select,
    int rotation)
{
    int model_ids[10] = { 0 };
    int models_count = 0;

    if( !config_loc->shapes )
    {
        int count = config_loc->lengths ? config_loc->lengths[0] : 0;
        for( int i = 0; i < count && models_count < 10; i++ )
        {
            int model_id = config_loc->models[0][i];
            model_ids[models_count++] = model_id;
        }
    }
    else
    {
        bool found = false;
        for( int i = 0; i < config_loc->shapes_and_model_count; i++ )
        {
            if( config_loc->shapes[i] != shape_select )
                continue;
            int count_inner = config_loc->lengths[i];
            for( int j = 0; j < count_inner && models_count < 10; j++ )
            {
                int model_id = config_loc->models[i][j];

                model_ids[models_count++] = model_id;
                found = true;
            }
        }
        if( !found )
        {
            /* Shape not present on this loc config — skip (common for mismatched map/loc data). */
            if( WB_ENV_SCENERY_DEBUG() )
                fprintf(
                    stderr,
                    "  scenery_load_model: loc %d shape %d NOT in config (groups=%d)\n",
                    builder->scenery_base_loc_id,
                    shape_select,
                    config_loc->shapes_and_model_count);
            return NULL;
        }
    }

    if( models_count <= 0 )
    {
        if( WB_ENV_SCENERY_DEBUG() )
            fprintf(
                stderr,
                "  scenery_load_model: loc %d shape %d NO MODEL IDS (shapes=%s)\n",
                builder->scenery_base_loc_id,
                shape_select,
                config_loc->shapes ? "yes" : "no");
        return NULL;
    }

    double t_convert0 = wb_timing_on() ? wb_now_ms() : 0.0;

    struct ToriDraw_Model* models[10] = { 0 };
    for( int i = 0; i < models_count; i++ )
    {
        struct ToriRS_Model* rs_model = CacheProvider_ModelGet(builder->cache, model_ids[i]);
        if( !rs_model )
        {
            /* Model not preloaded into the cache: skip this scenery loc. */
            if( WB_ENV_SCENERY_DEBUG() )
                fprintf(
                    stderr,
                    "  scenery_load_model: loc %d shape %d model %d NOT LOADED\n",
                    builder->scenery_base_loc_id,
                    shape_select,
                    model_ids[i]);
            for( int j = 0; j < i; j++ )
                ToriDraw_ModelFree(models[j]);
            return NULL;
        }
        models[i] = ToriDraw_ModelFromToriRS(rs_model);
        assert(models[i] && "scenery_load_model: failed to convert model instance");
        /* Face textures/UVs are kept: Task_WorldLoad preloads the referenced
         * texture ids and app_sync_textures publishes them into the scene
         * texture map; the raster skips faces whose texture is absent.
         * TORIRS_STRIP_TEXTURES=1 restores the old stripped behavior (A/B
         * debugging aid for texture regressions). */
        if( WB_ENV_STRIP_TEXTURES() )
        {
            if( models[i]->face_textures )
                for( int f = 0; f < models[i]->face_count; f++ )
                    models[i]->face_textures[f] = (faceint_t)-1;
            if( models[i]->face_texture_coords )
                for( int f = 0; f < models[i]->face_count; f++ )
                    models[i]->face_texture_coords[f] = (faceint_t)-1;
        }
    }

    struct ToriDraw_Model* model = NULL;
    if( models_count > 1 )
    {
        model = ToriDraw_ModelMerge(models, models_count);
        for( int i = 0; i < models_count; i++ )
            ToriDraw_ModelFree(models[i]);
    }
    else
        model = models[0];

    if( wb_timing_on() )
    {
        g_wb_t_model_convert_ms += wb_now_ms() - t_convert0;
        g_wb_n_model_builds++;
        g_wb_n_model_srcs += models_count;
    }
    double t_transform0 = wb_timing_on() ? wb_now_ms() : 0.0;

    /* Recolour pairs partition into texture swaps (endpoints <= 50) and HSL
     * recolours (see apply_transforms). Without the texture-swap half, scenery
     * loses its recolour-driven texture and renders with the wrong texture. */
    /* Consume-and-clear: a shape helper that bakes its rotation never sets
     * this, so it cannot leak into the next loc. */
    int const deferred_angle = builder->scenery_deferred_angle;
    builder->scenery_deferred_angle = 0;

    apply_transforms(config_loc, model, rotation, deferred_angle);

    /* HD-only textures come OFF here, after the retexture and before lighting — the
     * position ModelData.light() does it in. 643's ground decor is the visible case:
     * pebble scatters carry an HD material over a dark recoloured base, and the SD
     * client draws the dark base. Before the wants note, so those ids are never
     * requested either. */
    ToriDraw_ModelDropNonSdTextures(builder->cache, model);

    /* Retexturing rewrote face_textures to ids the source models never carried,
     * so the conversion-time report is stale — re-report from the final model. */
    ToriDraw_ModelNoteTextureWants(model);

    if( model->vertex_count <= 0 || model->face_count <= 0 )
    {
        if( WB_ENV_SCENERY_DEBUG() )
            fprintf(
                stderr,
                "  scenery_load_model: loc %d shape %d model EMPTY (v=%d f=%d)\n",
                builder->scenery_base_loc_id,
                shape_select,
                model->vertex_count,
                model->face_count);
        ToriDraw_ModelFree(model);
        return NULL;
    }

    /* TORIRS_SCENERY_DEBUG: geometry extent of the first few scenery models. A
     * byte-exact model decode still permits a wrong *interpretation* (axis order,
     * delta scale), which shows up here as an implausible bounding box rather than
     * as a decode failure. Tree-sized scenery should be a few hundred units. */
    static int dbg_seen_locs[1024];
    static int dbg_seen_count;
    bool dbg_fresh_loc = false;
    if( WB_ENV_SCENERY_DEBUG() && dbg_seen_count < 1024 )
    {
        dbg_fresh_loc = true;
        for( int s = 0; s < dbg_seen_count; s++ )
            if( dbg_seen_locs[s] == builder->scenery_base_loc_id )
            {
                dbg_fresh_loc = false;
                break;
            }
        if( dbg_fresh_loc )
            dbg_seen_locs[dbg_seen_count++] = builder->scenery_base_loc_id;
    }

    if( dbg_fresh_loc )
    {
        int xmin = 1 << 30, xmax = -(1 << 30);
        int ymin = 1 << 30, ymax = -(1 << 30);
        int zmin = 1 << 30, zmax = -(1 << 30);
        for( int v = 0; v < model->vertex_count; v++ )
        {
            if( model->vertices_x[v] < xmin ) xmin = model->vertices_x[v];
            if( model->vertices_x[v] > xmax ) xmax = model->vertices_x[v];
            if( model->vertices_y[v] < ymin ) ymin = model->vertices_y[v];
            if( model->vertices_y[v] > ymax ) ymax = model->vertices_y[v];
            if( model->vertices_z[v] < zmin ) zmin = model->vertices_z[v];
            if( model->vertices_z[v] > zmax ) zmax = model->vertices_z[v];
        }
        fprintf(
            stderr,
            "  scenery_model: loc %d model %d v=%d f=%d x=[%d..%d] y=[%d..%d] z=[%d..%d]\n",
            builder->scenery_base_loc_id,
            model_ids[0],
            model->vertex_count,
            model->face_count,
            xmin, xmax, ymin, ymax, zmin, zmax);
        /* The config that produced that extent. An implausible bounding box is
         * only half the story — offset/resize move and stretch the geometry off
         * the element origin, and `seq` decides whether the angle is baked into
         * the model or applied as a draw-time yaw (two different transform
         * orders). Printed with the extent so the two are read together. */
        fprintf(
            stderr,
            "                 size=%dx%d seq=%d mirror=%d offset=(%d,%d,%d) resize=(%d,%d,%d) "
            "contour=%d shape=%d rot=%d\n",
            config_loc->size_x,
            config_loc->size_z,
            config_loc->seq_id,
            config_loc->mirrored,
            config_loc->offset_x,
            config_loc->offset_y,
            config_loc->offset_z,
            config_loc->resize_x,
            config_loc->resize_height,
            config_loc->resize_z,
            config_loc->contour_ground_type,
            shape_select,
            rotation);
    }

    /*
     * TORIRS_ZBUFFER_LOCS=1: draw every loc model through the depth-tested
     * kernels, the way `zbuffer_model` already does for imported NPCs
     * (app_npc_wants_zbuffer / app_model_apply_import_render_flags).
     *
     * An investigation knob, not a feature: locs have no `zbuffer_model`
     * equivalent, so a backported model whose parts interpenetrate is resolved
     * by the painter's face sort alone — and the rs2012 bake stripped its face
     * priorities on the stated premise that "the lane is drawn with
     * param=zbuffer_model", which is true of its npcs and false of its locs.
     * Flipping this says whether a reported flicker is that gap or something
     * else, without editing content.
     */
    {
        static int zbuffer_locs = -1;
        if( zbuffer_locs < 0 )
        {
            char const* env = getenv("TORIRS_ZBUFFER_LOCS");
            zbuffer_locs = (env && *env && *env != '0') ? 1 : 0;
        }
        if( zbuffer_locs )
            model->flags |= (uint8_t)(TORIDRAW_MODEL_FLAG_ZBUFFER |
                                      TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY);
    }

    ToriDraw_ModelSetBoundsCylinder(model);

    if( wb_timing_on() )
        g_wb_t_model_transform_ms += wb_now_ms() - t_transform0;

    return model;
}

static int
scenery_load_model(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_tile,
    struct ToriRS_Location* config_loc,
    int shape_select,
    int rotation,
    int scene_x,
    int scene_z,
    int size_x,
    int size_z)
{
    struct World* world = builder->world;
    /*
     * The placement's geometry AND who owns it, for the whole function. Both
     * stores take and return this, so there is never a raw ToriDraw_Model* here
     * outliving the build that produced it -- which is what let a spent shell
     * be read after Publish freed it.
     */
    struct ToriDraw_ModelHandle hnd = { 0 };
    int64_t proto_key = 0;
    bool from_cache = false;
    bool const proto_shareable = scenery_loc_model_shareable(config_loc);

    /*
     * Prototype cache: within one build a scene places the same tree, fence or
     * rock hundreds of times, and each instance used to redo the whole
     * convert-merge-transform-light chain. For placement-independent locs
     * (scenery_loc_model_shareable) the finished, LIT model is cached once per
     * (resolved id, shape, rotation) — Client-TS keeps LocType model caches at
     * the same seam — and every later instance is the SAME model, not a copy.
     *
     * One model for N placements is the whole point: the copies were the
     * largest single pool in the process. The scene's shared-model store owns
     * it and each placement holds it (toridraw_shared_model.h), so a runtime
     * removal drops one holder rather than freeing geometry the rest of the
     * scene is still drawing, and the two paths that edit a placed loc's model
     * take ToriDraw_SceneElementModelForWrite to get a private copy first.
     *
     * The store keeps nothing alive by itself, so it needs no clearing seam: a
     * rebuild's ToriDraw_SceneClearPool drops the placements and the entries go
     * with them, which is also what stops a prototype baked from a reloaded loc
     * config from ever being served stale.
     *
     * Pre-lighting is what makes the cache worth having, so a shareable model
     * is lit HERE rather than in the End-batch defaultlight pass;
     * scenery_register_sharelight skips these by the same predicate. The
     * colours are identical either way: default lighting reads only the
     * model's own geometry and the loc's ambient/contrast, and the vertices
     * never change between here and End for a non-contoured, non-animated loc.
     */
    if( proto_shareable )
    {
        proto_key = scenery_model_key(config_loc->id, shape_select, rotation);
        hnd = ToriDraw_SharedModelStoreAcquire(
            ToriDraw_SceneSharedModels(builder->scene), proto_key);
    }
    else
    {
        if( config_loc->contour_ground_type != 0 )
            g_wb_share_no_contour++;
        else if( config_loc->sharelight != 0 )
            g_wb_share_no_sharelight++;
        else
            g_wb_share_no_seq++;
    }
    if( hnd.kind != TORIDRAWMK_NONE )
        g_wb_share_hit++;
    else if( proto_shareable )
        g_wb_share_proto++;

    /*
     * Not shareable whole, but the faces (and everything else the build
     * decides) are the same at every placement of this key -- so if a previous
     * placement has already published a set, clone from its template instead
     * of running the build to produce arrays this placement would immediately
     * hand back. @see ToriDraw_SharedFacesStoreClone.
     *
     * Animated locs are excluded here for the same reason they are excluded
     * from the loan below: an alpha transform rewrites face_alphas in place
     * every frame, which a shared set cannot carry.
     */
    if( hnd.kind == TORIDRAWMK_NONE && !proto_shareable && config_loc->seq_id == -1 &&
        !WB_ENV_NO_FACE_CLONE() )
    {
        hnd = ToriDraw_SharedFacesStoreClone(
            ToriDraw_SceneSharedFaces(builder->scene),
            scenery_model_key(config_loc->id, shape_select, rotation));
        if( hnd.kind != TORIDRAWMK_NONE )
        {
            /* The build this replaced ended with a wants report off its final
             * face_textures; the registry outlives any one model, so the clone
             * reports in its place -- the same rule the whole-model cache hit
             * below follows. */
            ToriDraw_ModelNoteTextureWants(ToriDraw_ModelRead(hnd));
            builder->scenery_deferred_angle = 0;
            g_wb_share_clone++;
        }
    }

    if( hnd.kind != TORIDRAWMK_NONE )
    {
        from_cache = true;
        /* Deferred draw-time rotation is an animated-loc mechanism, and
         * animated locs are never shareable — a non-zero value here means the
         * predicate and the shape helpers disagree about this loc. */
        assert(builder->scenery_deferred_angle == 0);
        /* The wants registry outlives any one model; re-report from the copy
         * exactly as a fresh build reports from its final face_textures. */
        ToriDraw_ModelNoteTextureWants(ToriDraw_ModelRead(hnd));
    }
    else
    {
        struct ToriDraw_Model* built =
            scenery_build_loc_model(builder, config_loc, shape_select, rotation);
        if( !built )
            return -1;

        /* Owned from here, and it says so. Whichever store takes it below
         * spends this handle and returns one of its own kind. */
        hnd = ToriDraw_ModelHandleOwned(built);

        if( proto_shareable )
        {
            if( ToriDraw_ModelIsLightable(built) )
            {
                ToriDraw_LightModelScene(hnd, config_loc->contrast, config_loc->ambient);
                ToriDraw_ModelFreeNormals(built);
            }
            /* Hand the freshly built model to the store and take it straight
             * back as this placement's copy -- the same object, now shared,
             * with this placement as its first holder and a handle that says
             * so. */
            hnd = ToriDraw_SharedModelStorePublish(
                ToriDraw_SceneSharedModels(builder->scene), proto_key, hnd);
        }
        else if( config_loc->seq_id == -1 )
        {
            /*
             * Not shareable whole, but shareable in half. This loc is contoured
             * to the ground or lit from a neighbour, so its vertices and its
             * per-corner colours have to be its own -- but the faces indexing
             * those vertices are the same at every placement of it, and there
             * are far more faces than there is anything else. A census of a
             * settled scene put 6915 such placements over 754 distinct
             * (id, shape, rotation) keys.
             *
             * Safe HERE and not earlier: the build's recolour, retexture and
             * mirror all write the face arrays, so the loan can only be taken
             * once they have finished. Past this point, contouring and the
             * End-batch defaultlight pass touch vertices and per-corner
             * colours, and sharelight touches face_infos -- none of which the
             * loan covers.
             *
             * Animated locs are excluded rather than handled: an alpha
             * transform (ToriDraw_ModelAnimateFrame op 5) writes face_alphas in
             * place every frame, and they are 150 placements out of the 6915.
             *
             * face_infos is not in the lendable set at all, because that is
             * the array the neighbour merge writes: World.shareLight hides the
             * seam faces where two of these meet, and lending it would hide
             * them at every placement of the loc. See
             * TORIDRAW_SHARED_FACE_FIELDS.
             */
            hnd = ToriDraw_SharedFacesStoreBorrow(
                ToriDraw_SceneSharedFaces(builder->scene),
                scenery_model_key(config_loc->id, shape_select, rotation),
                hnd);
        }
        /* else: nothing shared -- animated, or shareable neither whole nor in
         * half -- and `hnd` already says this placement owns what it built. */
    }

    assert(ToriDraw_ModelKindIsFull(hnd.kind));

    if( wb_census_on() )
    {
        /* Duplicates are counted at what they WOULD cost unshared -- the
         * bytes a copy of this model needs -- so the dup line still reads as
         * the size of the saving. Actual retained bytes are the proto line
         * alone: a key with N placements now holds one model, built on the
         * miss below. */
        size_t const bytes = ToriDraw_ModelHeapBytes(ToriDraw_ModelRead(hnd));
        if( from_cache )
        {
            g_wb_census_dup_n++;
            g_wb_census_dup_b += bytes;
        }
        else if( proto_shareable )
        {
            g_wb_census_proto_n++;
            g_wb_census_proto_b += bytes;
        }
        else
        {
            g_wb_census_unique_n++;
            g_wb_census_unique_b += bytes;
        }
    }

    /* The builder's own static pool, not the scene's default one: a boat
     * deck's geometry has to be freeable without touching the mainland's
     * (WorldBuilder_SetSceneView). */
    int element_id = ElementId_Raw(ElementId_Make(
        TORIDRAW_ELEMENT_KIND_SCENERY,
        ToriDraw_SceneElementAddPool(builder->scene, builder->static_pool)));
    assert(element_id >= 0 && "world_load_scenery_model: invalid element_id");

    /* ToriRS actions are [5][64]; the entity facet stores [5][32]. Repack at
     * the matching stride — passing the loc array directly reads slots 1-4 at
     * wrong offsets (garbled menu action names). */
    {
        char actions32[5][32];
        int pool_idx;
        int angle = map_tile->orientation & 0x3;
        /*
         * ROUTE footprint, which is not the render footprint. The reference
         * interactWithLoc measures the approach against `loc.width/length`
         * (angle-swapped) for centrepieces AND ground decor alike, while ground
         * decor *renders* on one tile — so deriving it from config_loc here is
         * both the reference's rule and the only place that sees it for every
         * shape. Walls register a size they never route by (their approach is
         * the shape+angle wall test), which is harmless and keeps this one
         * derivation.
         */
        int route_size_x = config_loc->size_x > 0 ? config_loc->size_x : 1;
        int route_size_z = config_loc->size_z > 0 ? config_loc->size_z : 1;
        /* LocType.forceapproach, rotated into the placed frame exactly as
         * Client.interactWithLoc does before handing it to tryMove. */
        int force_approach = config_loc->force_approach & 0xf;
        if( angle == 1 || angle == 3 )
        {
            int tmp = route_size_x;
            route_size_x = route_size_z;
            route_size_z = tmp;
        }
        if( angle != 0 )
            force_approach =
                ((force_approach << angle) & 0xf) + (force_approach >> (4 - angle));

        /* Plain bounded copy: snprintf("%s") ran the printf engine five times
         * per model, which showed in a whole-rebuild profile. */
        for( int a = 0; a < 5; a++ )
        {
            strncpy(actions32[a], config_loc->actions[a], sizeof(actions32[a]) - 1);
            actions32[a][sizeof(actions32[a]) - 1] = '\0';
        }
        /* Register the MAP orientation (0-3), not the render rotation: the
         * reference typecode2 stores angle<<6 with the map angle, and both
         * consumers of this field want that — the tryMove wall approach
         * (app_scenery_approach) and the runtime loc-change collision del
         * (WorldBuilder_ApplyLocChange). An L-wall's render rotations
         * (orientation+4, (orientation+1)&3) would del the wrong edges.
         *
         * The shape is the MAP shape too (map_tile->shape_select), not the
         * shape whose models were selected: scenery_add_normal loads shape 10's
         * model list for a shape-11 diagonal centrepiece, and registering 10
         * there would misreport the placed loc. */
        pool_idx = World_SceneryRegister(
            world,
            element_id,
            map_tile->loc_id,
            scene_x,
            scene_z,
            map_tile->chunk_pos_level,
            route_size_x,
            route_size_z,
            map_tile->shape_select,
            angle,
            force_approach,
            config_loc->name,
            actions32,
            config_loc->is_interactive);
        if( pool_idx >= 0 )
        {
            struct WorldEntity_Scenery* sc =
                World_EntityPoolGet(&world->entities.scenery, pool_idx);
            if( sc )
            {
                /* A runtime LOC_ADD_CHANGE spawn: the painter's static set is
                 * already baked, so flag this entry for per-frame painter
                 * re-registration (world_cycle) instead of relying on the
                 * build-time static registration below, which
                 * painter_reset_to_static truncates. */
                if( builder->scenery_runtime_spawn )
                    sc->runtime_spawn = 1;
                scenery_debug_record(
                    builder, sc, map_tile, config_loc, ToriDraw_ModelRead(hnd), element_id,
                    pool_idx, size_x, size_z);
            }
        }
    }

    /* `hnd` came from whichever store served this placement, or from
     * ToriDraw_ModelHandleOwned when none did -- so the type it carries was
     * never a claim this function made up. */
    ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);

    /* LocType.raiseobject: stamp model minY (max of -vy) onto every tile of the
     * sprite footprint so later zone OBJ_ADD stacks can sit on the table
     * (Client-TS objRaise). Only shapes that Client-TS places via addScenery /
     * setSprite contribute — walls, wall-decor, and ground decor never feed
     * setObj, so stamping them lifts items at full wall height over empty dirt. */
    if( config_loc->raiseobject == 1 &&
        (shape_select == RSCACHE_LOC_SHAPE_WALL_DIAGONAL ||
         shape_select == RSCACHE_LOC_SHAPE_SCENERY ||
         shape_select == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL ||
         (shape_select >= RSCACHE_LOC_SHAPE_ROOF_SLOPED &&
          shape_select <= RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER)) )
    {
        const struct ToriDraw_Model* m = ToriDraw_ModelRead(hnd);
        int raise = 0;
        int level = map_tile->chunk_pos_level;
        for( int v = 0; v < m->vertex_count; v++ )
        {
            int h = -(int)m->vertices_y[v];
            if( h > raise )
                raise = h;
        }
        for( int dx = 0; dx < size_x; dx++ )
        {
            for( int dz = 0; dz < size_z; dz++ )
                World_ObjRaiseSetMax(world, scene_x + dx, scene_z + dz, level, raise);
        }
    }

    if( config_loc->contour_ground_type != 0 )
    {
        contour_ground_q_push(
            &builder->contour_ground_queue,
            element_id,
            map_tile->loc_id,
            shape_select,
            rotation,
            size_x,
            size_z,
            map_tile->chunk_pos_level);
    }

    g_scenery_dbg_elements++;
    return element_id;
}

static void
scenery_element_position_init(
    struct WorldBuilder* builder,
    int element_id,
    int scene_x,
    int scene_z,
    int level,
    int size_x,
    int size_z,
    int yaw)
{
    struct World* world = builder->world;
    struct HeightmapHeights heights;
    heightmap_get_heights_sized(
        world->heightmap, scene_x, scene_z, level, size_x, size_z, &heights);

    ToriDraw_SceneElementSetPosition(
        builder->scene,
        element_id,
        scene_x * WORLD_TILE_SIZE + 64 * size_x,
        heights.height_center,
        scene_z * WORLD_TILE_SIZE + 64 * size_z,
        yaw % 2048);

    scenery_debug_note_position(
        builder,
        element_id,
        scene_x * WORLD_TILE_SIZE + 64 * size_x,
        heights.height_center,
        scene_z * WORLD_TILE_SIZE + 64 * size_z,
        yaw % 2048);
}

static void
scenery_load_animation(
    struct WorldBuilder* builder,
    int element_id,
    int seq_id)
{
    /* v1 ToriAuxLibTD_ElementSetSequenceId: bind the preloaded animation onto
     * the element. Task_WorldLoad registers each referenced loc sequence in
     * the scene before the rebuild; a NULL lookup means "not preloaded" and
     * the calloc'd empty sentinel means "decode failed" — skip both.
     * SetAnimationSeq captures original vertices; SetAnimation supplies the
     * frames the tick loop and frame emitter read. */
    struct ToriDraw_Animation* anim;
    struct ToriDraw_SceneElement* element;

    if( element_id < 0 || seq_id < 0 )
        return;
    anim = ToriDraw_SceneAnimationGet(builder->scene, seq_id);
    /* Under TORIRS_SCENERY_DEBUG, say whether the bind took. "The loc does not
     * animate" has two halves -- the seq never reached the element, or it did
     * and the tick is not advancing it -- and only this line separates them. */
    if( WB_ENV_SCENERY_DEBUG() )
        fprintf(
            stderr,
            "  loc anim: element=%d seq=%d frames=%d\n",
            element_id,
            seq_id,
            anim ? anim->frame_count : -1);
    if( !anim || anim->frame_count <= 0 || ((!anim->frames || !anim->base) && !anim->skeletal) )
        return;
    element = ToriDraw_SceneElementGet(builder->scene, element_id);
    ToriDraw_SceneElementSetAnimationSeq(builder->scene, element_id, seq_id);
    ToriDraw_SceneElementSetAnimation(builder->scene, element_id, anim, true);
    if( element )
    {
        element->animation = anim;
        element->is_skeletal = anim->skeletal != NULL;
        element->skeletal_animation = anim->skeletal;
        element->skeletal_play_frames = element->is_skeletal ? anim->frame_count : 0;
    }
}

static void
scenery_add_wall_single(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;

    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);
    scenery_record_runtime_wall(builder, element_id, WALL_A, ROTATION_WALL_TYPE[orientation]);

    /* Planar occluder marks (Client-TS ClientBuild.addLoc WALL_STRAIGHT). */
    if( config_loc->occlude )
    {
        switch( orientation )
        {
        case 0: /* WEST */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            break;
        case 1: /* NORTH */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z + 1,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            break;
        case 2: /* EAST */
            scenery_occluder_mark(
                builder,
                scene_x + 1,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            break;
        case 3: /* SOUTH */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            break;
        }
    }

    scenery_decor_set_wall_offset(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        scenery_shade_wall(
            builder,
            scene_x,
            scene_z,
            map_loc->chunk_pos_level,
            map_loc->orientation,
            50);
    }

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_tri_corner(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_WALL_TRI_CORNER, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);
    scenery_record_runtime_wall(
        builder, element_id, WALL_A, ROTATION_WALL_CORNER_TYPE[orientation]);

    scenery_decor_set_wall_offset(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        scenery_shade_wall_corner(
            builder, scene_x, scene_z, map_loc->chunk_pos_level, orientation, 50);
    }

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_two_sides(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int orientation = map_loc->orientation;
    int rotation = orientation + 4;
    int next_orientation = (orientation + 1) & 0x3;
    int next_rotation = (rotation + 1) & 0x3;

    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_WALL_TWO_SIDES, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation]);
    scenery_record_runtime_wall(builder, element_id, WALL_A, ROTATION_WALL_TYPE[orientation]);
    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);

    int element_id2 = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_TWO_SIDES,
        next_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id2 < 0 )
        return;
    scenery_element_position_init(
        builder, element_id2, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id2,
        WALL_B,
        ROTATION_WALL_TYPE[next_orientation]);
    scenery_record_runtime_wall(
        builder, element_id2, WALL_B, ROTATION_WALL_TYPE[next_orientation]);

    scenery_decor_set_wall_offset(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id2, 1, 1);

    /* Planar occluder marks (modern deob class85 WALL_L / shape 2). Both arms
     * use the all-levels wall composites (585 / 1170); the 2004 Client-TS
     * 0x109 west-arm typo is not present in the modern client. */
    if( config_loc->occlude )
    {
        switch( orientation )
        {
        case 0: /* WEST */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z + 1,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            break;
        case 1: /* NORTH */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z + 1,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            scenery_occluder_mark(
                builder,
                scene_x + 1,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            break;
        case 2: /* EAST */
            scenery_occluder_mark(
                builder,
                scene_x + 1,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            break;
        case 3: /* SOUTH — both marks land on the same tile in the reference */
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_Z_ALL_LEVELS);
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
            break;
        }
    }
}

static void
scenery_add_wall_rect_corner(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int orientation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_WALL_RECT_CORNER, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_wall(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation]);
    scenery_record_runtime_wall(
        builder, element_id, WALL_A, ROTATION_WALL_CORNER_TYPE[orientation]);

    scenery_decor_set_wall_offset(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        scenery_shade_wall_corner(
            builder,
            scene_x,
            scene_z,
            map_loc->chunk_pos_level,
            map_loc->orientation,
            50);
    }
    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_inside(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = config_loc->seq_id != -1 ? 512 * orientation : 0;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? orientation : 0;
    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_outside(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = config_loc->seq_id != -1 ? 512 * orientation : 0;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? orientation : 0;
    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);

    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_TYPE[orientation],
        0,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_outside(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? orientation : 0;
    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_inside(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int orientation = (map_loc->orientation + 2) & 0x3;
    int rotation = config_loc->seq_id != -1 ? 0 : orientation;
    int yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? orientation : 0;
    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( element_id < 0 )
        return;

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);
    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[orientation],
        THROUGHWALL,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_wall_decor_diagonal_double(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int outside_orientation = map_loc->orientation;
    int inside_orientation = (outside_orientation + 2) & 0x3;
    int outside_rotation = config_loc->seq_id != -1 ? 0 : outside_orientation;
    int inside_rotation = config_loc->seq_id != -1 ? 0 : inside_orientation;
    int outside_yaw = WALL_DECOR_YAW_ADJUST;
    int inside_yaw = WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
    {
        outside_yaw += 512 * outside_orientation;
        inside_yaw += 512 * inside_orientation;
    }

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? outside_orientation : 0;
    int outside_element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        outside_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( outside_element_id < 0 )
        return;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? inside_orientation : 0;
    int inside_element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE,
        inside_rotation,
        scene_x,
        scene_z,
        1,
        1);
    if( inside_element_id < 0 )
        return;

    scenery_element_position_init(
        builder, outside_element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, outside_yaw);
    scenery_element_position_init(
        builder, inside_element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, inside_yaw);
    scenery_load_animation(builder, outside_element_id, config_loc->seq_id);
    scenery_load_animation(builder, inside_element_id, config_loc->seq_id);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        outside_element_id,
        WALL_A,
        ROTATION_WALL_CORNER_TYPE[outside_orientation],
        THROUGHWALL,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, outside_element_id));

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        inside_element_id,
        WALL_B,
        ROTATION_WALL_CORNER_TYPE[inside_orientation],
        THROUGHWALL,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, inside_element_id));

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        outside_element_id,
        outside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    scenery_decor_add_element(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        inside_element_id,
        inside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, outside_element_id, 1, 1);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, inside_element_id, 1, 1);
}

static void
scenery_add_wall_diagonal(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_WALL_DIAGONAL, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);

    painter_add_normal_scenery(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        1,
        1,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    scenery_decor_set_wall_offset(
        builder,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_normal(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = config_loc->seq_id != -1 ? 0 : map_loc->orientation;
    int orientation = map_loc->orientation;
    int size_x = config_loc->size_x;
    int size_z = config_loc->size_z;
    int yaw = 0;

    if( map_loc->orientation == 1 || map_loc->orientation == 3 )
    {
        int tmp = size_x;
        size_x = size_z;
        size_z = tmp;
    }

    if( map_loc->shape_select == RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL )
        yaw += WALL_DECOR_YAW_ADJUST;
    if( config_loc->seq_id != -1 )
        yaw += 512 * orientation;

    builder->scenery_deferred_angle = config_loc->seq_id != -1 ? orientation : 0;
    int element_id = scenery_load_model(
        builder,
        map_loc,
        config_loc,
        RSCACHE_LOC_SHAPE_SCENERY,
        rotation,
        scene_x,
        scene_z,
        size_x,
        size_z);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, size_x, size_z, yaw);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_normal_scenery_ex(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        size_x,
        size_z,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id),
        (size_x * size_z > 1) ? (uint8_t)PNTR_SCENERY_STACK_BASE : 0);
    int shade = size_x * size_z * 11;
    if( shade > 30 )
        shade = 30;
    if( config_loc->shadowed )
    {
        scenery_shade_sized(
            builder, scene_x, scene_z, map_loc->chunk_pos_level, size_x, size_z, shade);
    }

    scenery_register_sharelight(
        builder,
        config_loc,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        size_x,
        size_z);
}

static void
scenery_add_roof(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, map_loc->shape_select, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;

    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    painter_add_normal_scenery(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        element_id,
        1,
        1,
        ToriDraw_SceneElementOcclusionHeight(builder->scene, element_id));

    /* Roof occluder marks: shapes 12..17 except 13 (ROOF_DIAGONAL_WITH_ROOFEDGE
     * / ROOF_SLOPED_OUTER_CORNER), only above level 0. Gate on the shape
     * numbers — the repo's enum names differ from Client-TS for the same ids. */
    {
        int shape = map_loc->shape_select;
        if( shape >= RSCACHE_LOC_SHAPE_ROOF_SLOPED && shape <= RSCACHE_LOC_SHAPE_ROOF_FLAT &&
            shape != RSCACHE_LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER &&
            map_loc->chunk_pos_level > 0 )
        {
            scenery_occluder_mark(
                builder,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                OCCLUDER_MARK_FLOOR_ALL_LEVELS);
        }
    }

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

static void
scenery_add_floor_decoration(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int rotation = map_loc->orientation;
    int element_id = scenery_load_model(
        builder, map_loc, config_loc, RSCACHE_LOC_SHAPE_FLOOR_DECORATION, rotation, scene_x, scene_z, 1, 1);
    if( element_id < 0 )
        return;
    scenery_element_position_init(
        builder, element_id, scene_x, scene_z, map_loc->chunk_pos_level, 1, 1, 0);
    scenery_load_animation(builder, element_id, config_loc->seq_id);

    painter_add_ground_decor(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id);
    scenery_record_runtime_ground_decor(builder, element_id);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
}

/* Gather loc "mapfunction" minimap icons for one chunk (reference
 * minimapBuildBuffer gather, Client.ts:5566-5617 — it reads gdType, i.e.
 * ground-decoration locs only). Raw loc tiles are recorded here; the
 * collision-aware random-walk spread runs after the full rebuild when the
 * collision maps are complete. */
void
world_builder_minimap_add_chunk_mapfunctions(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;

    int map_id = CacheProvider_MapId(mapx, mapz);
    struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(builder->cache, map_id);
    if( !map_locs )
        return;

    int scene_size = world->_scene_size;

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct ToriRS_MapLoc* map_loc = &map_locs->locs[i];
        if( map_loc->shape_select != RSCACHE_LOC_SHAPE_FLOOR_DECORATION )
            continue;
        if( map_loc->chunk_pos_level < 0 || map_loc->chunk_pos_level >= COLLISION_LEVELS )
            continue;

        int offset_x = World_ToSceneX(world, mapx, map_loc->chunk_pos_x);
        int offset_z = World_ToSceneZ(world, mapz, map_loc->chunk_pos_z);
        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
            continue;

        struct ToriRS_Location* config_loc =
            CacheProvider_LocationGet(builder->cache, map_loc->loc_id);
        if( !config_loc )
            continue;
        config_loc = world_builder_resolve_loc(builder, config_loc);
        if( !config_loc )
            continue;

        /* Loc mapfunction: dat1 atlas frame index, or dat2 mapelement id
         * (resolved to a sprite at draw time). Negative means unset. */
        int func = config_loc->map_function_id;
        if( func < 0 )
            continue;
        if( world->mapfunc_count >= WORLD_MAPFUNC_MAX )
            return;

        struct World_MapFunctionIcon* icon = &world->mapfuncs[world->mapfunc_count++];
        icon->x = offset_x;
        icon->z = offset_z;
        icon->level = map_loc->chunk_pos_level;
        icon->func = func;
    }
}

/* Post-build pass: nudge icons off their loc tile with the reference's
 * 10-step collision-respecting random walk (bounded ±3 tiles; on dat1 a fixed
 * set of atlas frames stays put — see world_scenery_mapfuncs.h). Runs once per
 * rebuild, after collision maps are final. */
void
world_builder_minimap_spread_mapfunctions(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    /* The exemption list is dat1 atlas frame indices; a dat2 `func` is a
     * mapelement id from an unrelated numbering, so it cannot be tested
     * against them. */
    bool const dat1 = RSCache_IsDat1(CacheProvider_Profile(builder->cache));

    for( int i = 0; i < world->mapfunc_count; i++ )
    {
        struct World_MapFunctionIcon* icon = &world->mapfuncs[i];
        struct CollisionMap* cm = world->collision_maps[icon->level];
        int func = icon->func;
        if( !cm )
            continue;
        if( dat1 && World_MapFunctionDat1StaysPut(func) )
            continue;

        int x = icon->x;
        int z = icon->z;
        int stx = x;
        int stz = z;
        for( int step = 0; step < 10; step++ )
        {
            int r = rand() & 3;
            if( r == 0 && stx > 0 && stx > x - 3 && collision_map_can_step_west(cm, stx, stz) )
                stx--;
            if( r == 1 && stx < cm->size_x - 1 && stx < x + 3 &&
                collision_map_can_step_east(cm, stx, stz) )
                stx++;
            if( r == 2 && stz > 0 && stz > z - 3 && collision_map_can_step_south(cm, stx, stz) )
                stz--;
            if( r == 3 && stz < cm->size_z - 1 && stz < z + 3 &&
                collision_map_can_step_north(cm, stx, stz) )
                stz++;
        }
        icon->x = stx;
        icon->z = stz;
    }
}

/*
 * One loc's contribution to the minimap: either a mapscene sprite or a wall
 * line, never both (reference drawDetail).
 *
 * Extracted from the square walker so the INSTANCE build can call it too.
 * The square walker maps a loc through `World_ToSceneX/Z`, which assumes the
 * contiguous map-square grid — an instance is assembled from remapped 8x8
 * chunks, so every loc came out off-scene and was skipped, and an instanced
 * minimap had no wall outlines and no mapscene icons at all. Everything the
 * registration needs is per-loc, so the fix is to pass the scene position the
 * caller already computed instead of re-deriving one.
 *
 * `map_loc->orientation` must already be the placed (rotated) angle and
 * `chunk_pos_level` the destination level — the instance path rewrites both on
 * its copy before calling.
 */
/*
 * One loc's mapfunction icon, at a scene position the caller already knows.
 *
 * Same split, and for the same reason, as world_builder_minimap_add_loc: the
 * square walker maps a loc through World_ToSceneX/Z, which only describes the
 * contiguous map-square grid, so an instance assembled from remapped 8x8 chunks
 * got no mapfunction icons at all.
 */
static void
world_builder_minimap_add_loc_mapfunction(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc const* map_loc,
    struct ToriRS_Location const* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;
    int func;

    if( map_loc->shape_select != RSCACHE_LOC_SHAPE_FLOOR_DECORATION )
        return;
    if( map_loc->chunk_pos_level < 0 || map_loc->chunk_pos_level >= COLLISION_LEVELS )
        return;
    if( scene_x < 0 || scene_z < 0 || scene_x >= scene_size || scene_z >= scene_size )
        return;

    /* Loc mapfunction: dat1 atlas frame index, or dat2 mapelement id (resolved
     * to a sprite at draw time). Negative means unset. */
    func = config_loc->map_function_id;
    if( func < 0 )
        return;
    if( world->mapfunc_count >= WORLD_MAPFUNC_MAX )
        return;

    {
        struct World_MapFunctionIcon* icon = &world->mapfuncs[world->mapfunc_count++];
        icon->x = scene_x;
        icon->z = scene_z;
        icon->level = map_loc->chunk_pos_level;
        icon->func = func;
    }
}

/*
 * One loc's ambient sound, if it has one.
 *
 * Registered from the same two placement paths the minimap gathers use, and for
 * the same reason: this is the only point that has the placed loc *and* its
 * resolved config. Recovering it later would mean walking the map squares a
 * second time and re-running the varbit transform, and a multiloc's transform
 * can change which sound it emits.
 *
 * The south-west tile and the footprint are both recorded, because the
 * reference measures the listener's distance to the emitter's *box*: a
 * four-tile waterfall is at distance zero from anywhere along its length, and
 * collapsing it to a centre point makes it quieter at the ends than in the
 * middle, which a waterfall conspicuously is not.
 */
static void
world_builder_add_loc_area_sound(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc const* map_loc,
    struct ToriRS_Location const* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;
    struct World_AreaSound emitter;
    int size_x;
    int size_z;

    if( config_loc->ambient_sound_id < 0 && config_loc->ambient_sound_id_count <= 0 )
        return;
    if( map_loc->chunk_pos_level < 0 || map_loc->chunk_pos_level >= COLLISION_LEVELS )
        return;
    if( scene_x < 0 || scene_z < 0 || scene_x >= scene_size || scene_z >= scene_size )
        return;

    /* Orientation swaps the footprint, exactly as it does for collision. */
    size_x = config_loc->size_x;
    size_z = config_loc->size_z;
    if( (map_loc->orientation & 1) != 0 )
    {
        int swap = size_x;
        size_x = size_z;
        size_z = swap;
    }

    memset(&emitter, 0, sizeof(emitter));
    emitter.x = scene_x;
    emitter.z = scene_z;
    emitter.size_x = size_x > 0 ? size_x : 1;
    emitter.size_z = size_z > 0 ? size_z : 1;
    emitter.level = map_loc->chunk_pos_level;
    emitter.loc_id = config_loc->id;
    emitter.sound_id = config_loc->ambient_sound_id;
    emitter.sound_ids = config_loc->ambient_sound_ids;
    emitter.sound_id_count = config_loc->ambient_sound_id_count;
    emitter.ticks_min = config_loc->ambient_sound_ticks_min;
    emitter.ticks_max = config_loc->ambient_sound_ticks_max;
    emitter.distance = config_loc->ambient_sound_distance;
    /*
     * `ambient_sound_retain` is the fourth field of loc opcode 78/79 and the
     * name is a misnomer inherited from cache tooling: `class91` uses it as the
     * radius inside which the emitter is at full volume, not as a linger time.
     */
    emitter.inner = config_loc->ambient_sound_retain;

    World_AddAreaSound(world, &emitter);
}

static void
world_builder_minimap_add_loc(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc const* map_loc,
    struct ToriRS_Location const* config_loc,
    int scene_x,
    int scene_z)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;

    if( !world->minimap )
        return;
    if( scene_x < 0 || scene_z < 0 || scene_x >= scene_size || scene_z >= scene_size )
        return;

    /* Reference drawDetail (Client.ts): a loc carrying a mapscene index plots
     * that Pix8 into the minimap image instead of drawing wall lines. Two
     * sources — wallType (wall shapes 0-3) and sceneType (diagonal-wall 9,
     * centrepiece 10/11, roofs 12-21). Wall-decor (4-8) and floor-decoration
     * (22) never feed a mapscene. The sprite is blitted per baked level in
     * app_rebuild_world_map, which owns the loaded mapscene atlas. width/length
     * are the raw config footprint (drawDetail centers the sprite over
     * loc.width x loc.length without the orientation swap). */
    if( config_loc->map_scene_id != -1 )
    {
        int const shape = map_loc->shape_select;
        if( (shape >= RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE &&
             shape <= RSCACHE_LOC_SHAPE_WALL_RECT_CORNER) ||
            (shape >= RSCACHE_LOC_SHAPE_WALL_DIAGONAL &&
             shape <= RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER) )
        {
            World_AddMapSceneIcon(
                world,
                scene_x,
                scene_z,
                map_loc->chunk_pos_level,
                config_loc->map_scene_id,
                config_loc->size_x,
                config_loc->size_z);
        }
        return;
    }

    /* Recorded per level (no WORLD_CURRENT_LEVEL filter): the bake takes the
     * level it needs, so upper floors get their own wall outlines. */
    {
        int flags = scenery_minimap_wall_flags(
            map_loc->shape_select, map_loc->orientation, config_loc->is_interactive);
        if( flags != 0 )
            minimap_add_tile_wall(
                world->minimap, scene_x, scene_z, map_loc->chunk_pos_level, flags);
    }
}

void
world_builder_minimap_add_chunk_walls(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;
    if( !world->minimap )
        return;

    int map_id = CacheProvider_MapId(mapx, mapz);
    struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(builder->cache, map_id);
    if( !map_locs )
        return;

    int scene_size = world->_scene_size;

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct ToriRS_MapLoc* map_loc = &map_locs->locs[i];
        int offset_x = World_ToSceneX(world, mapx, map_loc->chunk_pos_x);
        int offset_z = World_ToSceneZ(world, mapz, map_loc->chunk_pos_z);

        if( offset_x < 0 || offset_z < 0 || offset_x >= scene_size || offset_z >= scene_size )
            continue;

        struct ToriRS_Location* config_loc =
            CacheProvider_LocationGet(builder->cache, map_loc->loc_id);
        if( !config_loc )
            continue;

        config_loc = world_builder_resolve_loc(builder, config_loc);
        if( !config_loc )
            continue;

        world_builder_minimap_add_loc(builder, map_loc, config_loc, offset_x, offset_z);
        world_builder_add_loc_area_sound(builder, map_loc, config_loc, offset_x, offset_z);
    }
}

static void
scenery_add(
    struct WorldBuilder* builder,
    struct ToriRS_MapLoc* map_loc,
    struct ToriRS_Location* config_loc,
    int scene_x,
    int scene_z)
{
    switch( map_loc->shape_select )
    {
    case RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE:
        scenery_add_wall_single(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_TRI_CORNER:
        scenery_add_wall_tri_corner(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_TWO_SIDES:
        scenery_add_wall_two_sides(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_RECT_CORNER:
        scenery_add_wall_rect_corner(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DECOR_INSIDE:
        scenery_add_wall_decor_inside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DECOR_OUTSIDE:
        scenery_add_wall_decor_outside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE:
        scenery_add_wall_decor_diagonal_outside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE:
        scenery_add_wall_decor_diagonal_inside(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE:
        scenery_add_wall_decor_diagonal_double(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_WALL_DIAGONAL:
        scenery_add_wall_diagonal(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_SCENERY:
    case RSCACHE_LOC_SHAPE_SCENERY_DIAGONAL:
        scenery_add_normal(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_FLAT:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
    case RSCACHE_LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        scenery_add_roof(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    case RSCACHE_LOC_SHAPE_FLOOR_DECORATION:
        scenery_add_floor_decoration(builder, map_loc, config_loc, scene_x, scene_z);
        break;
    }
}

#endif
