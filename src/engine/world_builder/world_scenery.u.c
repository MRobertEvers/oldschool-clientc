#ifndef WORLD_SCENERY_U_C
#define WORLD_SCENERY_U_C

#include "engine/cache_provider.h"
#include "painters/painters.h"
#include "engine/toridraw_model_from_torirs.h"
#include "minimap.h"
#include "shademap.h"
#include "sharelight_map.h"
#include "toridraw_light_model.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_scene.h"
#include "varp/varp_manager.h"
#include "world_builder.h"
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

    if( !base_loc || base_loc->transform_count <= 0 || !base_loc->transforms )
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
    struct World* world = builder->world;

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
        if( el && el->model.kind == TORIDRAWMK_MODEL && el->model.u.model.model &&
            ToriDraw_ModelIsLightable(el->model.u.model.model) )
        {
            struct ToriDraw_ModelHandle hnd = {
                .kind = TORIDRAWMK_MODEL,
                .u.model.model = el->model.u.model.model,
            };
            ToriDraw_LightModelDefault(hnd, config_loc->contrast, config_loc->ambient);
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

/* Loc recolour endpoints <= this are texture ids (retexture), not HSL colours.
 * The reference stores a textured face's texture id in the same faceColour field
 * a recolour pass remaps, so the two operations are one pass partitioned purely
 * by value range: texture ids occupy 0..50, HSL colours are always > 50. */
#define LOC_RECOLOUR_TEXTURE_MAX 50

static void
apply_transforms(
    struct ToriRS_Location* loc,
    struct ToriDraw_Model* model,
    int orientation)
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

    if( mirrored )
        ToriDraw_ModelMirror(model);
    if( oriented )
        ToriDraw_ModelOrient(model, orientation);
    if( scaled )
        ToriDraw_ModelScale(model, loc->resize_x, loc->resize_z, loc->resize_height);
    if( translated )
        ToriDraw_ModelTranslate(model, loc->offset_x, loc->offset_y, loc->offset_z);
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
    struct ToriDraw_Model* model,
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
            if( getenv("TORIRS_SCENERY_DEBUG") )
                fprintf(
                    stderr,
                    "  scenery_load_model: loc %d shape %d NOT in config (groups=%d)\n",
                    builder->scenery_base_loc_id,
                    shape_select,
                    config_loc->shapes_and_model_count);
            return -1;
        }
    }

    if( models_count <= 0 )
    {
        if( getenv("TORIRS_SCENERY_DEBUG") )
            fprintf(
                stderr,
                "  scenery_load_model: loc %d shape %d NO MODEL IDS (shapes=%s)\n",
                builder->scenery_base_loc_id,
                shape_select,
                config_loc->shapes ? "yes" : "no");
        return -1;
    }

    struct ToriDraw_Model* models[10] = { 0 };
    for( int i = 0; i < models_count; i++ )
    {
        struct ToriRS_Model* rs_model = CacheProvider_ModelGet(builder->cache, model_ids[i]);
        if( !rs_model )
        {
            /* Model not preloaded into the cache: skip this scenery loc. */
            if( getenv("TORIRS_SCENERY_DEBUG") )
                fprintf(
                    stderr,
                    "  scenery_load_model: loc %d shape %d model %d NOT LOADED\n",
                    builder->scenery_base_loc_id,
                    shape_select,
                    model_ids[i]);
            for( int j = 0; j < i; j++ )
                ToriDraw_ModelFree(models[j]);
            return -1;
        }
        models[i] = ToriDraw_ModelFromToriRS(rs_model);
        assert(models[i] && "scenery_load_model: failed to convert model instance");
        /* Face textures/UVs are kept: Task_WorldLoad preloads the referenced
         * texture ids and app_sync_textures publishes them into the scene
         * texture map; the raster skips faces whose texture is absent.
         * TORIRS_STRIP_TEXTURES=1 restores the old stripped behavior (A/B
         * debugging aid for texture regressions). */
        if( getenv("TORIRS_STRIP_TEXTURES") )
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

    /* Recolour pairs partition into texture swaps (endpoints <= 50) and HSL
     * recolours (see apply_transforms). Without the texture-swap half, scenery
     * loses its recolour-driven texture and renders with the wrong texture. */
    apply_transforms(config_loc, model, rotation);

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
        if( getenv("TORIRS_SCENERY_DEBUG") )
            fprintf(
                stderr,
                "  scenery_load_model: loc %d shape %d model EMPTY (v=%d f=%d)\n",
                builder->scenery_base_loc_id,
                shape_select,
                model->vertex_count,
                model->face_count);
        ToriDraw_ModelFree(model);
        return -1;
    }

    /* TORIRS_SCENERY_DEBUG: geometry extent of the first few scenery models. A
     * byte-exact model decode still permits a wrong *interpretation* (axis order,
     * delta scale), which shows up here as an implausible bounding box rather than
     * as a decode failure. Tree-sized scenery should be a few hundred units. */
    static int dbg_seen_locs[1024];
    static int dbg_seen_count;
    bool dbg_fresh_loc = false;
    if( getenv("TORIRS_SCENERY_DEBUG") && dbg_seen_count < 1024 )
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

    ToriDraw_ModelSetBoundsCylinder(model);

    int element_id = ToriDraw_SceneElementAdd(builder->scene);
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

        for( int a = 0; a < 5; a++ )
            snprintf(actions32[a], sizeof(actions32[a]), "%s", config_loc->actions[a]);
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
                    builder, sc, map_tile, config_loc, model, element_id, pool_idx, size_x,
                    size_z);
            }
        }
    }

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = model,
    };
    ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);

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
     * the calloc'd empty sentinel means "decode failed / maya-only" — skip
     * both. SetAnimationSeq captures original vertices; SetAnimation supplies
     * the frames the tick loop and frame emitter read. */
    struct ToriDraw_Animation* anim;

    if( element_id < 0 || seq_id < 0 )
        return;
    anim = ToriDraw_SceneAnimationGet(builder->scene, seq_id);
    if( !anim || anim->frame_count <= 0 || !anim->frames || !anim->base )
        return;
    ToriDraw_SceneElementSetAnimationSeq(builder->scene, element_id, seq_id);
    ToriDraw_SceneElementSetAnimation(builder->scene, element_id, anim, true);
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

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall(
            builder->shademap,
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

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall_corner(
            builder->shademap, scene_x, scene_z, map_loc->chunk_pos_level, orientation, 50);
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

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    scenery_register_sharelight(
        builder, config_loc, scene_x, scene_z, map_loc->chunk_pos_level, element_id2, 1, 1);
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

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        config_loc->wall_width);

    if( config_loc->shadowed )
    {
        shademap2_set_wall_corner(
            builder->shademap,
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
        0);

    decor_buildmap_add_element(
        builder->decor_buildmap,
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
        0);

    decor_buildmap_add_element(
        builder->decor_buildmap,
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
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
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
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
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
        THROUGHWALL);

    painter_add_wall_decor(
        world->painter,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        inside_element_id,
        WALL_B,
        ROTATION_WALL_CORNER_TYPE[inside_orientation],
        THROUGHWALL);

    decor_buildmap_add_element(
        builder->decor_buildmap,
        scene_x,
        scene_z,
        map_loc->chunk_pos_level,
        outside_element_id,
        outside_orientation,
        DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET);

    decor_buildmap_add_element(
        builder->decor_buildmap,
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
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);

    decor_buildmap_set_wall_offset(
        builder->decor_buildmap,
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

    painter_add_normal_scenery(
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, size_x, size_z);
    int shade = size_x * size_z * 11;
    if( shade > 30 )
        shade = 30;
    if( config_loc->shadowed )
    {
        shademap2_set_sized(
            builder->shademap, scene_x, scene_z, map_loc->chunk_pos_level, size_x, size_z, shade);
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
        world->painter, scene_x, scene_z, map_loc->chunk_pos_level, element_id, 1, 1);
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

        /* The mapfunction archive holds 50 frames (reference Client.ts:978). */
        int func = config_loc->map_function_id;
        if( func < 0 || func >= 50 )
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
 * 10-step collision-respecting random walk (bounded ±3 tiles; a fixed set of
 * funcs stays put). Runs once per rebuild, after collision maps are final. */
void
world_builder_minimap_spread_mapfunctions(struct WorldBuilder* builder)
{
    struct World* world = builder->world;

    for( int i = 0; i < world->mapfunc_count; i++ )
    {
        struct World_MapFunctionIcon* icon = &world->mapfuncs[i];
        struct CollisionMap* cm = world->collision_maps[icon->level];
        int func = icon->func;
        if( !cm )
            continue;
        if( func == 22 || func == 29 || func == 34 || func == 36 || func == 46 || func == 47 ||
            func == 48 )
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

        /* Reference drawDetail (Client.ts): a loc carrying a mapscene index
         * plots that Pix8 into the minimap image instead of drawing wall lines.
         * Gather it here, mirroring drawDetail's two sources — wallType (wall
         * shapes 0-3) and sceneType (diagonal-wall 9, centrepiece 10/11, roofs
         * 12-21). Wall-decor (4-8) and floor-decoration (22) never feed a
         * mapscene. The sprite is blitted per baked level in
         * app_rebuild_world_map, which owns the loaded mapscene atlas.
         * width/length are the raw config footprint (drawDetail centers the
         * sprite over loc.width x loc.length without the orientation swap). */
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
                    offset_x,
                    offset_z,
                    map_loc->chunk_pos_level,
                    config_loc->map_scene_id,
                    config_loc->size_x,
                    config_loc->size_z);
            }
            continue;
        }

        /* Recorded per level (no WORLD_CURRENT_LEVEL filter): the bake takes
         * the level it needs, so upper floors get their own wall outlines. */
        {
            int flags = scenery_minimap_wall_flags(
                map_loc->shape_select, map_loc->orientation, config_loc->is_interactive);
            if( flags != 0 )
                minimap_add_tile_wall(
                    world->minimap, offset_x, offset_z, map_loc->chunk_pos_level, flags);
        }
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
    struct World* world = builder->world;
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
