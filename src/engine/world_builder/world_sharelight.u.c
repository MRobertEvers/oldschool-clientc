#ifndef WORLD_SHARELIGHT_U_C
#define WORLD_SHARELIGHT_U_C

#include "heightmap.h"
#include "sharelight_map.h"
#include "toridraw_light_model.h"
#include "toridraw_lighting.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_shared_model.h"
#include "world_builder.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TileCoord
{
    int x;
    int z;
    int level;
};

static int
gather_adjacent_tiles(
    int world_width,
    int world_height,
    struct TileCoord* out,
    int out_size,
    int tile_x,
    int tile_z,
    int tile_level,
    int element_size_x,
    int element_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + (element_size_x);
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + (element_size_z);

    bool huh = false;

    int count = 0;

    /* State the capacity requirement up front rather than only catching the
     * write that runs off the end: this names the element footprint that is
     * too large, which is the thing that has to change. */
    assert(
        (element_size_x + 2) * (element_size_z + 2) + element_size_x + element_size_z + 2 <=
            out_size &&
        "map element footprint exceeds the share-light adjacency buffer");

    /* #region agent log */
    {
        static int reported = 0;
        if( !reported && (element_size_x > 6 || element_size_z > 6) )
        {
            reported = 1;
            fprintf(
                stderr,
                "sharelight: map element footprint %dx%d needs %d adjacency slots "
                "(buffer %d)\n",
                element_size_x,
                element_size_z,
                (element_size_x + 2) * (element_size_z + 2) + element_size_x +
                    element_size_z + 2,
                out_size);
        }
    }
    /* #endregion */

    for( int level = tile_level; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                if( x < 0 || z < 0 || x >= world_width || z >= world_height || level < 0 ||
                    level >= WORLD_MAP_TERRAIN_LEVELS )
                    continue;
                if( (!huh && x < max_tile_x && z < max_tile_z && (z >= tile_z || x >= tile_x)) )
                    continue;

                assert(count < out_size);
                out[count++] = (struct TileCoord){ .x = x, .z = z, .level = level };
            }
        }

        min_tile_x -= 1;
        huh = true;
    }

    return count;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

/*
 * Scratch hash for merge_normals: buckets the OTHER model's vertex positions so
 * each of this model's vertices probes a chain instead of scanning every other
 * vertex. Grow-only, reused across the whole build (a rebuild calls
 * merge_normals tens of thousands of times; per-call malloc would dominate).
 *
 * head[] entries are stamped with the build serial rather than cleared, so a
 * new pair costs O(vertices inserted), not O(table).
 */
static uint32_t* g_slh_head; /* bucket -> (serial, first chain index) */
static int32_t* g_slh_next; /* per-inserted-vertex chain link, -1 ends */
static uint32_t* g_slh_serial; /* bucket stamp; matches g_slh_build if live */
static int g_slh_bucket_cap;
static int g_slh_next_cap;
static uint32_t g_slh_build;

static inline uint32_t
slh_hash(
    int x,
    int y,
    int z)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    h ^= h >> 15;
    return h;
}

static void
slh_reserve(
    int vertex_count)
{
    int want_buckets = 16;
    while( want_buckets < vertex_count * 2 )
        want_buckets <<= 1;
    if( want_buckets > g_slh_bucket_cap )
    {
        free(g_slh_head);
        free(g_slh_serial);
        g_slh_head = malloc((size_t)want_buckets * sizeof(*g_slh_head));
        assert(g_slh_head);
        g_slh_serial = calloc((size_t)want_buckets, sizeof(*g_slh_serial));
        assert(g_slh_serial);
        g_slh_bucket_cap = want_buckets;
        g_slh_build = 0;
    }
    if( vertex_count > g_slh_next_cap )
    {
        free(g_slh_next);
        g_slh_next_cap = vertex_count * 2;
        g_slh_next = malloc((size_t)g_slh_next_cap * sizeof(*g_slh_next));
        assert(g_slh_next);
    }
}

static int*
ToriDraw_ModelFaceInfosEnsureZero(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->face_infos && model->face_count > 0 )
    {
        model->face_infos = calloc((size_t)model->face_count, sizeof(int));
        assert(model->face_infos);
    }
    return model->face_infos;
}

/**
 * Hide the seam: every face whose three corners all landed on the neighbour
 * stops being drawn, because the neighbour's own face is there instead.
 *
 * This is the write the topology loan is shaped around. face_infos is the one
 * face array a sharelight placement does NOT borrow, and the only reason it
 * does not is this loop -- lend it and one segment's seam disappears at every
 * placement of the same wall in the scene. The assert is what says so at the
 * write rather than three rooms away.
 */
static void
hide_merged_faces(
    struct ToriDraw_Model* model,
    const int* vertex_merge_index)
{
    int face_count;
    faceint_t const* fa;
    faceint_t const* fb;
    faceint_t const* fc;
    int* infos;
    int face;

    assert(model);
    assert(vertex_merge_index);

    face_count = model->face_count;
    fa = model->face_indices_a;
    fb = model->face_indices_b;
    fc = model->face_indices_c;
    infos = NULL;

    for( face = 0; face < face_count; face++ )
    {
        if( vertex_merge_index[fa[face]] != g_merge_index ||
            vertex_merge_index[fb[face]] != g_merge_index ||
            vertex_merge_index[fc[face]] != g_merge_index )
            continue;

        if( !infos )
        {
            infos = ToriDraw_ModelFaceInfosEnsureZero(model);
            assert(infos);
        }
        infos[face] = 2;
    }
}

static void
merge_normals(
    struct ToriDraw_Model* model,
    struct ToriDraw_Normal* vertex_normals,
    struct ToriDraw_Normal* lighting_vertex_normals,
    struct ToriDraw_Model* other_model,
    struct ToriDraw_Normal* other_vertex_normals,
    struct ToriDraw_Normal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z,
    bool hide_faces)
{
    g_merge_index++;

    struct ToriDraw_Normal* model_a_normal = NULL;
    struct ToriDraw_Normal* model_b_normal = NULL;
    struct ToriDraw_Normal* model_a_lighting_normal = NULL;
    struct ToriDraw_Normal* model_b_lighting_normal = NULL;
    int x, y, z;

    int merged_vertex_count = 0;

    int model_vc = model->vertex_count;
    vertexint_t* model_vx = model->vertices_x;
    vertexint_t* model_vy = model->vertices_y;
    vertexint_t* model_vz = model->vertices_z;

    int other_vc = other_model->vertex_count;
    vertexint_t* other_vx = other_model->vertices_x;
    vertexint_t* other_vy = other_model->vertices_y;
    vertexint_t* other_vz = other_model->vertices_z;

    /* Bucket the other model's shareable vertices (face_count > 0) by position,
     * then probe once per own vertex. Yields exactly the (vertex, other_vertex)
     * pairs the old full cross scan found — duplicates included, since bucket
     * chains keep every vertex at a position — at O(m + n) instead of O(m * n).
     * Serial-stamped buckets make the table reusable without clearing. */
    slh_reserve(other_vc);
    g_slh_build++;
    uint32_t const bucket_mask = (uint32_t)g_slh_bucket_cap - 1;
    for( int other_vertex = 0; other_vertex < other_vc; other_vertex++ )
    {
        if( other_vertex_normals[other_vertex].face_count == 0 )
            continue;
        uint32_t b =
            slh_hash(other_vx[other_vertex], other_vy[other_vertex], other_vz[other_vertex]) &
            bucket_mask;
        if( g_slh_serial[b] != g_slh_build )
        {
            g_slh_serial[b] = g_slh_build;
            g_slh_next[other_vertex] = -1;
        }
        else
            g_slh_next[other_vertex] = (int32_t)g_slh_head[b];
        g_slh_head[b] = (uint32_t)other_vertex;
    }

    for( int vertex = 0; vertex < model_vc; vertex++ )
    {
        model_a_normal = &vertex_normals[vertex];
        if( model_a_normal->face_count == 0 )
            continue;

        x = model_vx[vertex] - check_offset_x;
        y = model_vy[vertex] - check_offset_y;
        z = model_vz[vertex] - check_offset_z;

        uint32_t b = slh_hash(x, y, z) & bucket_mask;
        if( g_slh_serial[b] != g_slh_build )
            continue;

        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int32_t other_vertex = (int32_t)g_slh_head[b]; other_vertex != -1;
             other_vertex = g_slh_next[other_vertex] )
        {
            if( x != other_vx[other_vertex] || y != other_vy[other_vertex] ||
                z != other_vz[other_vertex] )
                continue;

            model_b_normal = &other_vertex_normals[other_vertex];
            model_b_lighting_normal = &other_lighting_vertex_normals[other_vertex];

            model_a_lighting_normal->x += model_b_normal->x;
            model_a_lighting_normal->y += model_b_normal->y;
            model_a_lighting_normal->z += model_b_normal->z;
            model_a_lighting_normal->face_count += model_b_normal->face_count;
            model_a_lighting_normal->merged++;

            model_b_lighting_normal->x += model_a_normal->x;
            model_b_lighting_normal->y += model_a_normal->y;
            model_b_lighting_normal->z += model_a_normal->z;
            model_b_lighting_normal->face_count += model_a_normal->face_count;
            model_b_lighting_normal->merged++;

            merged_vertex_count++;

            g_vertex_a_merge_index[vertex] = g_merge_index;
            g_vertex_b_merge_index[other_vertex] = g_merge_index;
        }
    }

    if( merged_vertex_count < 3 || !hide_faces )
        return;

    hide_merged_faces(model, g_vertex_a_merge_index);
    hide_merged_faces(other_model, g_vertex_b_merge_index);
}

/*
 * Largest map-element footprint, in tiles, that share-lighting supports.
 *
 * gather_adjacent_tiles emits the element's own far edge on its own level, then
 * its whole footprint plus a one-tile skirt on the level above, so it can write
 *
 *     (size + 2) * (size + 2) + 2 * size + 2
 *
 * entries. The old fixed 96 was only good up to a 6x6 element — a 7x7 already
 * needs 97 — and the buffer it fills is a stack array in merge_column, so
 * overrunning it corrupts that frame rather than anything a sanitizer watches.
 * The 2012 QBD arena has larger platform locs, and the assert below caught one.
 */
#define SHARELIGHT_MAX_ELEMENT_TILES 32
#define ADJACENT_TILES_COUNT                                                  \
    ((SHARELIGHT_MAX_ELEMENT_TILES + 2) * (SHARELIGHT_MAX_ELEMENT_TILES + 2) + \
     2 * SHARELIGHT_MAX_ELEMENT_TILES + 2)

static void
defaultlight_build(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    struct ToriDraw_SceneElement* scene_element = NULL;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;

    for( int sx = 0; sx < builder->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->defaultlight_head; pi != -1;
                     pi = builder->sharelight_map->pool[pi].next )
                {
                    map_element = &builder->sharelight_map->pool[pi].element;
                    if( map_element->element_idx == -1 )
                        continue;

                    scene_element =
                        ToriDraw_SceneElementGet(builder->scene, map_element->element_idx);
                    if( !scene_element || !ToriDraw_ModelKindIsFull(scene_element->model.kind) ||
                        !scene_element->model.u.model.model )
                        continue;

                    struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                    if( !ToriDraw_ModelIsLightable(dm) )
                        continue;

                    struct ToriDraw_ModelHandle hnd = {
                        .kind = TORIDRAWMK_MODEL,
                        .u.model.model = dm,
                    };
                    ToriDraw_LightModelScene(
                        hnd, map_element->light_attenuation, map_element->light_ambient);
                    ToriDraw_ModelFreeNormals(dm);
                }
            }
        }
    }
}

#define SHARELIGHT_MERGE_LOOKAHEAD 6

/* Alloc order matters: CalculateVertexNormals seeds merged_normals from the
 * base normals only if merged is already allocated. */
static void
sharelight_ensure_normals(struct ToriDraw_Model* dm)
{
    assert(dm);
    if( dm->normals )
        return;
    ToriDraw_ModelAllocNormals(dm);
    ToriDraw_ModelAllocMergedNormals(dm);
    ToriDraw_ModelCalculateVertexNormals(dm);
}

static void
alloc_normals_for_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct ToriDraw_SceneElement* scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;
            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = ToriDraw_SceneElementGet(builder->scene, map_element->element_idx);
                if( !scene_element || !ToriDraw_ModelKindIsFull(scene_element->model.kind) ||
                    !scene_element->model.u.model.model )
                    continue;
                sharelight_ensure_normals(scene_element->model.u.model.model);
            }
        }
    }
}

static bool
sharelight_should_hide_faces_for_merge(
    int primary_slevel,
    int adjacent_slevel)
{
    (void)primary_slevel;
    (void)adjacent_slevel;
    return primary_slevel == adjacent_slevel;
}

static void
merge_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct TileCoord adjacent_tiles[ADJACENT_TILES_COUNT];
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapTile* adjacent_map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct SharelightMapElement* adjacent_map_element = NULL;
    struct ToriDraw_SceneElement* scene_element = NULL;
    struct ToriDraw_SceneElement* adjacent_scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;

            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = ToriDraw_SceneElementGet(builder->scene, map_element->element_idx);
                if( !scene_element || !ToriDraw_ModelKindIsFull(scene_element->model.kind) ||
                    !scene_element->model.u.model.model )
                    continue;

                int adjacent_tiles_count = gather_adjacent_tiles(
                    builder->sharelight_map->width,
                    builder->sharelight_map->height,
                    adjacent_tiles,
                    ADJACENT_TILES_COUNT,
                    sx,
                    sz,
                    slevel,
                    map_element->size_x,
                    map_element->size_z);

                for( int j = 0; j < adjacent_tiles_count; j++ )
                {
                    struct TileCoord adjacent_tile_coord = adjacent_tiles[j];
                    adjacent_map_tile = sharelight_map_tile_at(
                        builder->sharelight_map,
                        adjacent_tile_coord.x,
                        adjacent_tile_coord.z,
                        adjacent_tile_coord.level);
                    for( int32_t kidx = adjacent_map_tile->sharelight_head; kidx != -1;
                         kidx = builder->sharelight_map->pool[kidx].next )
                    {
                        adjacent_map_element = &builder->sharelight_map->pool[kidx].element;
                        if( adjacent_map_element->element_idx == map_element->element_idx )
                            continue;

                        adjacent_scene_element = ToriDraw_SceneElementGet(
                            builder->scene, adjacent_map_element->element_idx);
                        if( !adjacent_scene_element ||
                            !ToriDraw_ModelKindIsFull(adjacent_scene_element->model.kind) ||
                            !adjacent_scene_element->model.u.model.model )
                            continue;

                        int check_offset_x =
                            (adjacent_tile_coord.x - sx) * 128 +
                            (adjacent_map_element->size_x - map_element->size_x) * 64;

                        int check_offset_z =
                            (adjacent_tile_coord.z - sz) * 128 +
                            (adjacent_map_element->size_z - map_element->size_z) * 64;

                        int height_center = heightmap_get_center(world->heightmap, sx, sz, slevel);
                        int adjacent_height_center = heightmap_get_center(
                            world->heightmap,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);
                        int check_offset_height = adjacent_height_center - height_center;

                        struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                        struct ToriDraw_Model* adjacent_dm =
                            adjacent_scene_element->model.u.model.model;

                        if( !ToriDraw_ModelIsLightable(adjacent_dm) ||
                            !ToriDraw_ModelIsLightable(dm) )
                            continue;

                        /* The batch alloc only leads by SHARELIGHT_MERGE_LOOKAHEAD
                         * columns, but adjacency reaches the element's whole
                         * footprint (up to SHARELIGHT_MAX_ELEMENT_TILES): a
                         * 7-tile-wide sharelight loc (ToA's Crondis water
                         * source, 7x5) gathers a column the alloc pass has not
                         * visited yet, whose models still have NULL normals. */
                        sharelight_ensure_normals(adjacent_dm);
                        assert(dm->normals);
                        assert(dm->merged_normals);
                        assert(adjacent_dm->merged_normals);

                        bool hide_faces = sharelight_should_hide_faces_for_merge(
                            slevel, adjacent_tile_coord.level);

                        merge_normals(
                            dm,
                            dm->normals->vertex_normals,
                            dm->merged_normals->vertex_normals,
                            adjacent_dm,
                            adjacent_dm->normals->vertex_normals,
                            adjacent_dm->merged_normals->vertex_normals,
                            check_offset_x,
                            check_offset_height,
                            check_offset_z,
                            hide_faces);
                    }
                }
            }
        }
    }
}

static void
apply_and_free_column(
    struct WorldBuilder* builder,
    int sx)
{
    struct World* world = builder->world;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct ToriDraw_SceneElement* scene_element = NULL;

    for( int sz = 0; sz < builder->sharelight_map->height; sz++ )
    {
        for( int slevel = 0; slevel < WORLD_MAP_TERRAIN_LEVELS; slevel++ )
        {
            map_tile = sharelight_map_tile_at(builder->sharelight_map, sx, sz, slevel);
            if( !map_tile )
                continue;

            for( int32_t pi = map_tile->sharelight_head; pi != -1;
                 pi = builder->sharelight_map->pool[pi].next )
            {
                map_element = &builder->sharelight_map->pool[pi].element;
                scene_element = ToriDraw_SceneElementGet(builder->scene, map_element->element_idx);
                if( !scene_element || !ToriDraw_ModelKindIsFull(scene_element->model.kind) ||
                    !scene_element->model.u.model.model )
                    continue;

                /* Scene regime base + per-loc offsets. Contrast arrives
                 * pre-scaled from the loc decoder — see LightModelScene. */
                {
                    struct ToriDraw_LightProfile const* p = ToriDraw_LightSceneProfile();
                    int light_ambient = p->ambient + map_element->light_ambient;
                    int light_attenuation = p->attenuation + map_element->light_attenuation;
                    int lightsrc_x = p->src_x;
                    int lightsrc_y = p->src_y;
                    int lightsrc_z = p->src_z;

                    int light_magnitude =
                        (int)sqrt((double)(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y +
                                           lightsrc_z * lightsrc_z));
                    int attenuation = (light_attenuation * light_magnitude) >> 8;

                    struct ToriDraw_Model* dm = scene_element->model.u.model.model;
                    if( !ToriDraw_ModelIsLightable(dm) )
                        continue;
                    /* Every sharelight element's column is alloc'd before its
                     * apply pass; NULL here means the alloc/merge/apply
                     * lifecycle broke, not a legitimate unlit model. */
                    assert(dm->normals);
                    assert(dm->merged_normals);
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
                        lightsrc_x,
                        lightsrc_y,
                        lightsrc_z,
                        dm->vertices_x,
                        dm->vertices_y,
                        dm->vertices_z);

                    ToriDraw_ModelFreeNormals(dm);
                }
            }
        }
    }
}

static void
world_build_lighting(struct WorldBuilder* builder)
{
    assert(builder->sharelight_map);

    int scene_size = builder->sharelight_map->width;
    double t_alloc = 0.0;
    double t_merge = 0.0;
    double t_apply = 0.0;
    double tp;

    int initial_cols =
        scene_size < SHARELIGHT_MERGE_LOOKAHEAD + 1 ? scene_size : SHARELIGHT_MERGE_LOOKAHEAD + 1;
    tp = wb_timing_on() ? wb_now_ms() : 0.0;
    for( int sx = 0; sx < initial_cols; sx++ )
        alloc_normals_for_column(builder, sx);
    if( wb_timing_on() )
        t_alloc += wb_now_ms() - tp;

    for( int sx = 0; sx < scene_size; sx++ )
    {
        int alloc_sx = sx + SHARELIGHT_MERGE_LOOKAHEAD;
        if( alloc_sx < scene_size && alloc_sx >= initial_cols )
        {
            tp = wb_timing_on() ? wb_now_ms() : 0.0;
            alloc_normals_for_column(builder, alloc_sx);
            if( wb_timing_on() )
                t_alloc += wb_now_ms() - tp;
        }

        tp = wb_timing_on() ? wb_now_ms() : 0.0;
        merge_column(builder, sx);
        if( wb_timing_on() )
        {
            double t = wb_now_ms();
            t_merge += t - tp;
            tp = t;
        }

        if( sx >= 1 )
            apply_and_free_column(builder, sx - 1);
        if( wb_timing_on() )
            t_apply += wb_now_ms() - tp;
    }

    tp = wb_timing_on() ? wb_now_ms() : 0.0;
    apply_and_free_column(builder, scene_size - 1);

    defaultlight_build(builder);

    if( wb_timing_on() )
        fprintf(
            stderr,
            "rebuild_timing: lighting alloc=%.1fms merge=%.1fms apply=%.1fms tail=%.1fms\n",
            t_alloc,
            t_merge,
            t_apply,
            wb_now_ms() - tp);
}

#endif
