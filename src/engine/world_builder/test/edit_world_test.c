/**
 * The EDIT world layout's one promise, checked.
 *
 * A window of authored squares from the content tree is baked twice: once on
 * the EDIT layout, which then takes a scripted run of random edits -- heights,
 * underlays, overlays, flags, locs removed and placed, brush strokes across the
 * seams between squares -- each stroke flushed the way the editor flushes a
 * frame; and once, at the end, on the game's STATIC layout from the same edited
 * squares. The two scenes must hold the same element models, vertex for vertex
 * and colour for colour, at the same positions, over the same heights, tile
 * flags, collision and minimap.
 *
 * Along the way it checks that the per-tile derive (Editor_TileDerive) agrees
 * with the square derive on every tile, before and after the edits, and that
 * the EDIT bake itself is the STATIC bake (same scene, before any edit).
 *
 * It also prints one brush stroke's flush cost from the layout's own counters:
 * a 20-tile height drag across the seam of two squares.
 *
 *   edit_world_test <content-dir> <cache-dir> [seed] [strokes] [repo-root] [tmp-dir]
 *
 * With a repo root it also reads the two map-editor manifests and a game
 * manifest and checks the `[world:boot] layout=` each states.
 *
 * With a tmp dir it runs the same promise through the editor itself: the
 * squares are copied there, an embedded ToriRSMapEd serves them, edits go in
 * as commands (strokes, undo, redo) and come back as the mirror's echo, and
 * the client's EDIT flush path (app_map_editor_edit_step, the App-free core of
 * app_map_editor.c) carries them to the world. Writes only under tmp-dir.
 *
 * With -DEDIT_WORLD_TEST_FINGERPRINT_ONLY it builds against a tree without the
 * EDIT layout and only prints the STATIC fingerprint of the unedited window --
 * how the STATIC refactor is checked against the commit before it.
 *
 * Read-only on both trees.
 */

#include "asyncio.h"
#include "bootmanifest/bootmanifest.h"
#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_derive.h"
#include "editor/editor_doc.h"
#include "editor/editor_jm2.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/collision_map.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/minimap.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#ifndef EDIT_WORLD_TEST_FINGERPRINT_ONLY
#include "engine/world_builder/edit/world_edit.h"
#endif
#include "mem/torirs_mem.h"
#include "painters/painters.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <rscache.h>
#include <toridraw.h>
#include <toridraw_element_id.h>
#include <toridraw_scene.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SQUARE_COUNT 4
static const int k_squares[SQUARE_COUNT][2] = { { 50, 50 }, { 51, 50 }, { 50, 51 }, { 51, 51 } };
#define BASE_X 50
#define BASE_Z 50

static int g_failed;
static int g_checks;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL: " __VA_ARGS__);                                                          \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- harness ------------------------------------------------------------- */

struct Harness
{
    struct ToriRS_IOBatch* io;
    struct ToriRS_TaskQueue* queue;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct RSCache_Dat2Disk* disk;
    struct PlatformX_IO* px;
    struct RSCache profile;
};

struct TestWorld
{
    struct ToriDraw_Scene* scene;
    struct World* world;
    struct VarPManager varp;
    struct WorldBuilder* builder;
};

static void
run_task(
    struct Harness* h,
    struct ToriRS_Task* task)
{
    struct TaskRunner runner = {
        .queue = h->queue,
        .io = h->io,
        .px = h->px,
        .parallel = 1,
    };
    enum TaskRunnerStat stat;

    if( !task )
        return;
    ToriRS_TaskQueue_Add(h->queue, task);
    do
    {
        stat = TaskRunner_Step(&runner);
    } while( stat != TASK_RUNNER_IDLE );
}

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    assert(data);
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static int
load_square(
    const char* content_dir,
    struct Editor_Square* square,
    int map_x,
    int map_z)
{
    char path[1200];
    size_t size = 0;
    char* text;
    struct Editor_ParseResult parsed;

    Editor_SquareInit(square, map_x, map_z);
    snprintf(path, sizeof(path), "%s/maps/m%d_%d.jm2", content_dir, map_x, map_z);
    text = slurp(path, &size);
    if( !text )
        return 0;
    parsed = Editor_Jm2Parse(square, text, size);
    free(text);
    if( parsed.status != EDITOR_PARSE_OK )
        return 0;
    snprintf(path, sizeof(path), "%s/maps/m%d_%d.jl2", content_dir, map_x, map_z);
    text = slurp(path, &size);
    if( text )
    {
        parsed = Editor_Jl2Parse(square, text, size);
        free(text);
        if( parsed.status != EDITOR_PARSE_OK )
            return 0;
    }
    return 1;
}

/* The provider records the editor seeds (app_map_editor.c does the same). */
static void
seed_square(
    struct Harness* h,
    const struct Editor_Square* square)
{
    int map_id = CacheProvider_MapId(square->map_x, square->map_z);
    struct ToriRS_MapTerrain* terrain = CacheProvider_MapTerrainGet(h->provider, map_id);
    struct ToriRS_MapLocs* locs = CacheProvider_MapSceneryGet(h->provider, map_id);

    if( !terrain )
    {
        terrain = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*terrain));
        assert(terrain);
        CacheProvider_MapTerrainAdd(h->provider, map_id, terrain);
    }
    Editor_SquareDeriveTerrain(square, &h->profile, terrain);
    if( !locs )
    {
        locs = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*locs));
        assert(locs);
        CacheProvider_MapSceneryAdd(h->provider, map_id, locs);
    }
    ToriMem_Free(ToriMem_Assets(), locs->locs);
    locs->locs = NULL;
    Editor_SquareDeriveLocs(square, locs);
}

static void
world_new(
    struct Harness* h,
    struct TestWorld* w)
{
    memset(w, 0, sizeof(*w));
    w->scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    w->world = World_New();
    VarPManager_Init(&w->varp);
    w->builder = WorldBuilder_New(w->world, h->provider, w->scene, &w->varp);
}

static void
world_drain(struct TestWorld* w)
{
    int count = World_EventsCount(w->world);
    for( int i = 0; i < count; i++ )
    {
        const struct World_Event* ev = World_EventsPeek(w->world, i);
        if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
            ToriDraw_SceneElementRemove(w->scene, ev->element_id);
    }
    World_EventsClear(w->world);
}

static void
world_load(
    struct Harness* h,
    struct TestWorld* w)
{
    int chunks[SQUARE_COUNT * 2];
    for( int i = 0; i < SQUARE_COUNT; i++ )
    {
        chunks[i * 2] = k_squares[i][0];
        chunks[i * 2 + 1] = k_squares[i][1];
    }
    world_drain(w);
    run_task(
        h,
        CreateTask_WorldLoad(
            h->provider, w->builder, h->queue, chunks, SQUARE_COUNT, -1, -1, 104, NULL, NULL, NULL));
}

/* ---- scene comparison ---------------------------------------------------- */

static uint64_t
fnv(
    uint64_t hash,
    const void* data,
    size_t size)
{
    const unsigned char* p = data;
    for( size_t i = 0; i < size; i++ )
    {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t
model_hash(
    uint64_t hash,
    const struct ToriDraw_Model* m)
{
    if( !m )
        return fnv(hash, "null", 4);
    hash = fnv(hash, &m->vertex_count, sizeof(m->vertex_count));
    hash = fnv(hash, &m->face_count, sizeof(m->face_count));
    hash = fnv(hash, m->vertices_x, sizeof(*m->vertices_x) * (size_t)m->vertex_count);
    hash = fnv(hash, m->vertices_y, sizeof(*m->vertices_y) * (size_t)m->vertex_count);
    hash = fnv(hash, m->vertices_z, sizeof(*m->vertices_z) * (size_t)m->vertex_count);
    hash = fnv(hash, m->face_indices_a, sizeof(*m->face_indices_a) * (size_t)m->face_count);
    hash = fnv(hash, m->face_indices_b, sizeof(*m->face_indices_b) * (size_t)m->face_count);
    hash = fnv(hash, m->face_indices_c, sizeof(*m->face_indices_c) * (size_t)m->face_count);
    if( m->face_colors_a )
    {
        hash = fnv(hash, m->face_colors_a, sizeof(*m->face_colors_a) * (size_t)m->face_count);
        hash = fnv(hash, m->face_colors_b, sizeof(*m->face_colors_b) * (size_t)m->face_count);
        hash = fnv(hash, m->face_colors_c, sizeof(*m->face_colors_c) * (size_t)m->face_count);
    }
    if( m->face_textures )
        hash = fnv(hash, m->face_textures, sizeof(*m->face_textures) * (size_t)m->face_count);
    for( int f = 0; f < m->face_count; f++ )
    {
        /* A NULL face_infos and an all-zero one draw the same. */
        int info = m->face_infos ? m->face_infos[f] : 0;
        hash = fnv(hash, &info, sizeof(info));
    }
    return hash;
}

static const struct ToriDraw_Model*
element_model(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, element_id);
    if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) )
        return NULL;
    return el->model.u.model.model;
}

static uint64_t
element_hash(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, element_id);
    uint64_t hash = 1469598103934665603ull;
    int pos[4];

    if( !el )
        return 0;
    hash = model_hash(hash, element_model(scene, element_id));
    pos[0] = el->world_position.x;
    pos[1] = el->world_position.y;
    pos[2] = el->world_position.z;
    pos[3] = el->world_position.yaw;
    return fnv(hash, pos, sizeof(pos));
}

struct SceneryKey
{
    int x;
    int z;
    int level;
    int loc_id;
    int shape;
    int angle;
    uint64_t hash;
    int element_id;
};

static int
scenery_key_cmp(
    const void* pa,
    const void* pb)
{
    const struct SceneryKey* a = pa;
    const struct SceneryKey* b = pb;
    if( a->level != b->level )
        return a->level - b->level;
    if( a->x != b->x )
        return a->x - b->x;
    if( a->z != b->z )
        return a->z - b->z;
    if( a->shape != b->shape )
        return a->shape - b->shape;
    if( a->loc_id != b->loc_id )
        return a->loc_id - b->loc_id;
    if( a->angle != b->angle )
        return a->angle - b->angle;
    if( a->hash != b->hash )
        return a->hash < b->hash ? -1 : 1;
    return 0;
}

static struct SceneryKey*
collect_scenery(
    struct TestWorld* w,
    int* out_count)
{
    struct World_EntityPool* pool = &w->world->entities.scenery;
    struct SceneryKey* keys = NULL;
    int count = 0;
    int cap = 0;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
        if( !sc )
            continue;
        if( count == cap )
        {
            cap = cap ? cap * 2 : 1024;
            keys = realloc(keys, (size_t)cap * sizeof(*keys));
            assert(keys);
        }
        keys[count].x = sc->grid_position.x;
        keys[count].z = sc->grid_position.z;
        keys[count].level = sc->grid_position.level;
        keys[count].loc_id = sc->loc_id;
        keys[count].shape = sc->shape;
        keys[count].angle = sc->angle;
        keys[count].hash = element_hash(w->scene, sc->element_id);
        keys[count].element_id = sc->element_id;
        count++;
    }
    qsort(keys, (size_t)count, sizeof(*keys), scenery_key_cmp);
    *out_count = count;
    return keys;
}

/* One hash over everything the comparison below reads. */
static uint64_t
scene_fingerprint(struct TestWorld* w)
{
    struct World* world = w->world;
    int ss = world->_scene_size;
    uint64_t hash = 1469598103934665603ull;
    struct SceneryKey* keys;
    int count;

    for( int l = 0; l < 4; l++ )
        for( int z = 0; z < ss; z++ )
            for( int x = 0; x < ss; x++ )
            {
                int e = World_TerrainElementAt(world, x, z, l);
                uint64_t eh = e >= 0 ? element_hash(w->scene, e) : 0;
                hash = fnv(hash, &eh, sizeof(eh));
            }
    keys = collect_scenery(w, &count);
    for( int i = 0; i < count; i++ )
    {
        /* Field by field: the struct's tail padding is not data. */
        int f[6] = { keys[i].x,      keys[i].z,     keys[i].level,
                     keys[i].loc_id, keys[i].shape, keys[i].angle };
        hash = fnv(hash, f, sizeof(f));
        hash = fnv(hash, &keys[i].hash, sizeof(keys[i].hash));
    }
    free(keys);
    return hash;
}

static void
compare_worlds(
    const char* what,
    struct TestWorld* a,
    struct TestWorld* b)
{
    struct World* wa = a->world;
    struct World* wb = b->world;
    int ss = wa->_scene_size;
    int terrain_bad = 0;
    int terrain_n = 0;
    int height_bad = 0;
    int flags_bad = 0;
    int coll_bad = 0;
    int mm_bad = 0;
    int paint_bad = 0;
    int scenery_bad = 0;
    struct SceneryKey* ka;
    struct SceneryKey* kb;
    int na;
    int nb;

    CHECK(wa->_scene_size == wb->_scene_size, "%s: scene sizes %d vs %d", what, wa->_scene_size,
        wb->_scene_size);
    if( wa->_scene_size != wb->_scene_size )
        return;

    for( int l = 0; l < 4; l++ )
    {
        for( int z = 0; z <= ss; z++ )
            for( int x = 0; x <= ss; x++ )
                if( heightmap_get(wa->heightmap, x, z, l) != heightmap_get(wb->heightmap, x, z, l) )
                    height_bad++;
        for( int z = 0; z < ss; z++ )
            for( int x = 0; x < ss; x++ )
            {
                int ea = World_TerrainElementAt(wa, x, z, l);
                int eb = World_TerrainElementAt(wb, x, z, l);
                int idx = x + z * ss + l * ss * ss;
                if( (ea >= 0) != (eb >= 0) ||
                    (ea >= 0 && element_hash(a->scene, ea) != element_hash(b->scene, eb)) )
                {
                    if( terrain_bad < 5 )
                        printf("  %s: terrain differs at %d,%d L%d (%d vs %d)\n", what, x, z, l, ea,
                            eb);
                    terrain_bad++;
                }
                if( ea >= 0 )
                    terrain_n++;
                if( wa->tile_flags[idx] != wb->tile_flags[idx] )
                    flags_bad++;
                if( wa->minimap && wb->minimap &&
                    (minimap_tile_rgb(wa->minimap, x, z, l, 0) != minimap_tile_rgb(wb->minimap, x, z, l, 0) ||
                     minimap_tile_rgb(wa->minimap, x, z, l, 1) != minimap_tile_rgb(wb->minimap, x, z, l, 1) ||
                     minimap_tile_wall(wa->minimap, x, z, l) != minimap_tile_wall(wb->minimap, x, z, l) ||
                     minimap_tile_shape(wa->minimap, x, z, l) != minimap_tile_shape(wb->minimap, x, z, l)) )
                    mm_bad++;
                if( wa->painter && wb->painter && l < painter_max_levels(wa->painter) )
                {
                    struct PaintersTile* pa = painter_tile_at(wa->painter, x, z, l);
                    struct PaintersTile* pb = painter_tile_at(wb->painter, x, z, l);
                    if( painter_tile_get_terrain_levels(wa->painter, x, z, l) !=
                            painter_tile_get_terrain_levels(wb->painter, x, z, l) ||
                        painters_tile_get_visible_gte_level(pa) != painters_tile_get_visible_gte_level(pb) )
                        paint_bad++;
                }
            }
        if( wa->collision_maps[l] && wb->collision_maps[l] )
        {
            struct CollisionMap* ca = wa->collision_maps[l];
            struct CollisionMap* cb = wb->collision_maps[l];
            for( int i = 0; i < ca->size_x * ca->size_z; i++ )
                if( ca->flags[i] != cb->flags[i] )
                    coll_bad++;
        }
    }

    ka = collect_scenery(a, &na);
    kb = collect_scenery(b, &nb);
    CHECK(na == nb, "%s: %d scenery elements vs %d", what, na, nb);
    {
        int i = 0;
        int j = 0;
        while( i < na || j < nb )
        {
            int c = i >= na ? 1 : j >= nb ? -1 : scenery_key_cmp(&ka[i], &kb[j]);
            if( c == 0 )
            {
                i++;
                j++;
                continue;
            }
            if( scenery_bad < 8 )
            {
                const struct SceneryKey* k = c < 0 ? &ka[i] : &kb[j];
                printf("  %s: scenery only in %s: loc %d shape %d angle %d at %d,%d L%d\n", what,
                    c < 0 ? "EDIT" : "STATIC", k->loc_id, k->shape, k->angle, k->x, k->z, k->level);
            }
            scenery_bad++;
            if( c < 0 )
                i++;
            else
                j++;
        }
    }
    free(ka);
    free(kb);

    CHECK(height_bad == 0, "%s: %d height corners differ", what, height_bad);
    CHECK(terrain_bad == 0, "%s: %d of %d terrain tiles differ", what, terrain_bad, terrain_n);
    CHECK(flags_bad == 0, "%s: %d tile flags differ", what, flags_bad);
    CHECK(coll_bad == 0, "%s: %d collision cells differ", what, coll_bad);
    CHECK(mm_bad == 0, "%s: %d minimap tiles differ", what, mm_bad);
    CHECK(paint_bad == 0, "%s: %d paint tiles differ (terrain levels / draw level)", what, paint_bad);
    CHECK(scenery_bad == 0, "%s: %d scenery elements unmatched (models or positions)", what,
        scenery_bad);
    printf("%s: %d terrain tiles, %d scenery elements compared\n", what, terrain_n, na);
}

/* ---- the per-tile derive -------------------------------------------------- */

#ifndef EDIT_WORLD_TEST_FINGERPRINT_ONLY
/* Field by field: the struct has padding the two derives leave different. */
static int
floor_equal(
    const struct ToriRS_MapFloor* a,
    const struct ToriRS_MapFloor* b)
{
    return a->overlay_id == b->overlay_id && a->underlay_id == b->underlay_id &&
           a->height == b->height && a->settings == b->settings && a->shape == b->shape &&
           a->rotation == b->rotation && a->has_authored_height == b->has_authored_height &&
           a->authored_height == b->authored_height;
}

static void
check_tile_derive(
    const char* what,
    struct Harness* h,
    struct Editor_Square* squares)
{
    struct ToriRS_MapTerrain* derived = malloc(sizeof(*derived));
    int bad = 0;

    assert(derived);
    int n = 0;

    for( int s = 0; s < SQUARE_COUNT; s++ )
    {
        Editor_SquareDeriveTerrain(&squares[s], &h->profile, derived);
        for( int x = 0; x < EDITOR_SQUARE_X; x++ )
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
            {
                struct ToriRS_MapFloor column[4];
                Editor_TileDeriveColumn(&squares[s], x, z, column);
                for( int l = 0; l < 4; l++ )
                {
                    const struct ToriRS_MapFloor* want =
                        &derived->tiles_xyz[Editor_TileIndex(x, z, l)];
                    struct ToriRS_MapFloor one;
                    Editor_TileDerive(&squares[s], x, z, l, &one);
                    n++;
                    if( !floor_equal(want, &column[l]) || !floor_equal(want, &one) )
                        bad++;
                }
            }
    }
    free(derived);
    CHECK(bad == 0, "%s: per-tile derive disagrees with the square derive on %d of %d tiles", what,
        bad, n);
    printf("%s: per-tile derive checked on %d tiles\n", what, n);
}

/* ---- random edits --------------------------------------------------------- */

static uint32_t g_rng;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rnd_range(
    int lo,
    int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

struct Pools
{
    int underlays[256];
    int underlay_count;
    int overlays[256];
    int overlay_count;
    struct Editor_Loc locs[512];
    int loc_count;
};

struct Session
{
    struct Harness* h;
    struct TestWorld* edit;
    struct Editor_Square* squares;
    /* EDIT handles, parallel to each square's loc list. */
    int* handles[SQUARE_COUNT];
    int handle_cap[SQUARE_COUNT];
    struct Pools pools;
    int rebuilds;
};

static void
refresh_handles(struct Session* s)
{
    for( int q = 0; q < SQUARE_COUNT; q++ )
    {
        struct Editor_Square* sq = &s->squares[q];
        if( s->handle_cap[q] < sq->loc_count + 64 )
        {
            s->handle_cap[q] = sq->loc_count + 1024;
            s->handles[q] = realloc(s->handles[q], (size_t)s->handle_cap[q] * sizeof(int));
            assert(s->handles[q]);
        }
        for( int i = 0; i < sq->loc_count; i++ )
        {
            s->handles[q][i] = WorldEdit_SquarePlacement(s->edit->builder, sq->map_x, sq->map_z, i);
            assert(s->handles[q][i] != WORLD_BUILDER_HANDLE_NONE);
        }
    }
}

static int
square_of_scene(
    int scene_x,
    int scene_z)
{
    int mx = BASE_X + scene_x / 64;
    int mz = BASE_Z + scene_z / 64;
    for( int q = 0; q < SQUARE_COUNT; q++ )
        if( k_squares[q][0] == mx && k_squares[q][1] == mz )
            return q;
    return -1;
}

/* The column changed in the document: derive it, put it in the provider, and
 * hand every level to the EDIT world -- what the editor's flush path does. */
static void
push_column(
    struct Session* s,
    int scene_x,
    int scene_z)
{
    int q = square_of_scene(scene_x, scene_z);
    struct Editor_Square* sq;
    struct ToriRS_MapFloor column[4];
    struct ToriRS_MapTerrain* terrain;
    int lx = scene_x % 64;
    int lz = scene_z % 64;

    if( q < 0 )
        return;
    sq = &s->squares[q];
    Editor_TileDeriveColumn(sq, lx, lz, column);
    terrain = CacheProvider_MapTerrainGet(s->h->provider, CacheProvider_MapId(sq->map_x, sq->map_z));
    for( int l = 0; l < 4; l++ )
    {
        terrain->tiles_xyz[Editor_TileIndex(lx, lz, l)] = column[l];
        WorldBuilder_TileSet(s->edit->builder, scene_x, scene_z, l, &column[l]);
    }
}

static void
push_locs(
    struct Session* s,
    int q)
{
    struct Editor_Square* sq = &s->squares[q];
    struct ToriRS_MapLocs* locs =
        CacheProvider_MapSceneryGet(s->h->provider, CacheProvider_MapId(sq->map_x, sq->map_z));
    ToriMem_Free(ToriMem_Assets(), locs->locs);
    locs->locs = NULL;
    Editor_SquareDeriveLocs(sq, locs);
}

static struct Editor_Tile*
tile_at(
    struct Session* s,
    int scene_x,
    int scene_z,
    int level)
{
    int q = square_of_scene(scene_x, scene_z);
    if( q < 0 )
        return NULL;
    return &s->squares[q].tiles[Editor_TileIndex(scene_x % 64, scene_z % 64, level)];
}

static void
edit_heights(
    struct Session* s,
    int cx,
    int cz,
    int radius,
    int level)
{
    for( int x = cx - radius; x <= cx + radius; x++ )
        for( int z = cz - radius; z <= cz + radius; z++ )
        {
            struct Editor_Tile* t;
            if( x < 0 || z < 0 || x >= 128 || z >= 128 )
                continue;
            t = tile_at(s, x, z, level);
            t->has_height = 1;
            t->height = (uint8_t)rnd_range(1, 48);
            push_column(s, x, z);
        }
}

static void
edit_underlay(
    struct Session* s,
    int cx,
    int cz,
    int radius,
    int level)
{
    int id = (rnd() % 6 == 0) ? 0 : s->pools.underlays[rnd() % (uint32_t)s->pools.underlay_count];
    for( int x = cx - radius; x <= cx + radius; x++ )
        for( int z = cz - radius; z <= cz + radius; z++ )
        {
            if( x < 0 || z < 0 || x >= 128 || z >= 128 )
                continue;
            tile_at(s, x, z, level)->underlay_id = (uint8_t)id;
            push_column(s, x, z);
        }
}

static void
edit_overlay(
    struct Session* s,
    int cx,
    int cz,
    int radius,
    int level)
{
    int remove = rnd() % 5 == 0;
    int id = s->pools.overlays[rnd() % (uint32_t)s->pools.overlay_count];
    int shape = rnd_range(0, 11);
    int rotation = rnd_range(0, 3);
    for( int x = cx - radius; x <= cx + radius; x++ )
        for( int z = cz - radius; z <= cz + radius; z++ )
        {
            struct Editor_Tile* t;
            if( x < 0 || z < 0 || x >= 128 || z >= 128 )
                continue;
            t = tile_at(s, x, z, level);
            t->has_overlay = !remove;
            t->overlay_id = remove ? 0 : (uint16_t)id;
            t->shape = remove ? 0 : (uint8_t)shape;
            t->rotation = remove ? 0 : (uint8_t)rotation;
            push_column(s, x, z);
        }
}

static void
edit_flags(
    struct Session* s,
    int allow_bridge)
{
    int x = rnd_range(1, 126);
    int z = rnd_range(1, 126);
    int level = rnd_range(0, 2);
    static const int bits[] = { 1, 4, 8 };
    int bit = bits[rnd() % 3];
    if( allow_bridge )
    {
        level = 1;
        bit = 2; /* LINK_BELOW: the flush asks for a rebuild */
    }
    tile_at(s, x, z, level)->settings ^= (uint8_t)bit;
    push_column(s, x, z);
}

static void
edit_loc_remove(struct Session* s)
{
    int q = rnd_range(0, SQUARE_COUNT - 1);
    struct Editor_Square* sq = &s->squares[q];
    int i;
    if( sq->loc_count == 0 )
        return;
    i = (int)(rnd() % (uint32_t)sq->loc_count);
    WorldBuilder_LocRemove(s->edit->builder, s->handles[q][i]);
    Editor_SquareLocRemoveAt(sq, i);
    memmove(&s->handles[q][i], &s->handles[q][i + 1], (size_t)(sq->loc_count - i) * sizeof(int));
    push_locs(s, q);
}

static void
edit_loc_add(
    struct Session* s,
    int scene_x,
    int scene_z)
{
    int q = square_of_scene(scene_x, scene_z);
    struct Editor_Square* sq = &s->squares[q];
    struct Editor_Loc loc = s->pools.locs[rnd() % (uint32_t)s->pools.loc_count];
    struct World_LocPlacement p;

    loc.x = scene_x % 64;
    loc.z = scene_z % 64;
    loc.rotation = rnd_range(0, 3);
    /* Map data holds one loc per tile per layer for walls, wall decor and
     * ground decor -- the painter's static slots assert it -- so an add only
     * goes where its layer is free (the editor's place tool replaces the
     * occupant instead). */
    if( World_LocShapeToLayer(loc.shape) != 2 )
        for( int i = 0; i < sq->loc_count; i++ )
            if( sq->locs[i].level == loc.level && sq->locs[i].x == loc.x && sq->locs[i].z == loc.z &&
                World_LocShapeToLayer(sq->locs[i].shape) == World_LocShapeToLayer(loc.shape) )
                return;
    Editor_SquareLocAdd(sq, &loc);
    if( s->handle_cap[q] < sq->loc_count )
    {
        s->handle_cap[q] = sq->loc_count + 1024;
        s->handles[q] = realloc(s->handles[q], (size_t)s->handle_cap[q] * sizeof(int));
        assert(s->handles[q]);
    }
    p.scene_x = scene_x;
    p.scene_z = scene_z;
    p.level = loc.level;
    p.loc_id = loc.loc_id;
    p.shape = loc.shape;
    p.angle = loc.rotation;
    s->handles[q][sq->loc_count - 1] = WorldBuilder_LocPlace(s->edit->builder, &p);
    push_locs(s, q);
}

static void
flush(
    struct Session* s,
    struct WorldEditFlushStats* stats)
{
    int status = WorldBuilder_Flush(s->edit->builder);
    world_drain(s->edit);
    if( status == WORLD_BUILDER_FLUSH_REBUILD )
    {
        /* What the editor does with this answer: bake the window again from
         * the provider, which already holds the edit. */
        world_load(s->h, s->edit);
        refresh_handles(s);
        s->rebuilds++;
        CHECK(WorldEdit_Ready(s->edit->builder), "EDIT ready again after the rebuild");
    }
    if( stats )
        WorldEdit_LastFlushStats(s->edit->builder, stats);
}

static void
collect_pools(struct Session* s)
{
    struct Pools* p = &s->pools;
    memset(p, 0, sizeof(*p));
    for( int q = 0; q < SQUARE_COUNT; q++ )
    {
        struct Editor_Square* sq = &s->squares[q];
        for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
        {
            const struct Editor_Tile* t = &sq->tiles[i];
            int seen = 0;
            if( t->underlay_id > 0 && p->underlay_count < 256 )
            {
                for( int k = 0; k < p->underlay_count; k++ )
                    seen |= p->underlays[k] == t->underlay_id;
                if( !seen )
                    p->underlays[p->underlay_count++] = t->underlay_id;
            }
            seen = 0;
            if( t->overlay_id > 0 && p->overlay_count < 256 )
            {
                for( int k = 0; k < p->overlay_count; k++ )
                    seen |= p->overlays[k] == t->overlay_id;
                if( !seen )
                    p->overlays[p->overlay_count++] = t->overlay_id;
            }
        }
        /* Every 7th loc: a spread of shapes and sizes whose models the load
         * already made resident. */
        for( int i = 0; i < sq->loc_count && p->loc_count < 512; i += 7 )
            p->locs[p->loc_count++] = sq->locs[i];
    }
}
/* ---- through the editor ---------------------------------------------------- */

/* app/app_map_editor.c's EDIT core: the App layer has no header a test may
 * include, so its two entry points are spelled here. */
void
app_map_editor_edit_seed(
    struct Editor* editor,
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    int map_x,
    int map_z);

int
app_map_editor_edit_step(
    struct Editor* editor,
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    struct World* world,
    struct ToriDraw_Scene* scene);

static int
copy_file(
    const char* from,
    const char* to)
{
    size_t size = 0;
    char* data = slurp(from, &size);
    FILE* f;

    if( !data )
        return 0;
    f = fopen(to, "wb");
    if( !f )
    {
        free(data);
        return 0;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    return 1;
}

static struct Editor_Square*
mirror_square(
    struct Editor* ed,
    int scene_x,
    int scene_z)
{
    if( scene_x < 0 || scene_z < 0 || scene_x >= 128 || scene_z >= 128 )
        return NULL;
    return Editor_DocFindSquare(&ed->doc, BASE_X + scene_x / 64, BASE_Z + scene_z / 64);
}

/* One TILE command, as the panel builds it: before = the document's tile. */
static void
mirror_tile(
    struct Editor* ed,
    int scene_x,
    int scene_z,
    int level,
    int field,
    int value,
    int value2,
    int value3)
{
    struct Editor_Square* sq = mirror_square(ed, scene_x, scene_z);
    struct Editor_Cmd cmd;

    if( !sq )
        return;
    Editor_CmdInitSquare(&cmd, EDITOR_CMD_TILE, sq->map_x, sq->map_z);
    cmd.u.tile.level = level;
    cmd.u.tile.x = scene_x % 64;
    cmd.u.tile.z = scene_z % 64;
    cmd.u.tile.before = sq->tiles[Editor_TileIndex(scene_x % 64, scene_z % 64, level)];
    cmd.u.tile.after = cmd.u.tile.before;
    switch( field )
    {
    case 0:
        cmd.u.tile.after.has_height = 1;
        cmd.u.tile.after.height = (uint8_t)value;
        break;
    case 1:
        cmd.u.tile.after.underlay_id = (uint8_t)value;
        break;
    case 2:
        cmd.u.tile.after.has_overlay = value != 0;
        cmd.u.tile.after.overlay_id = (uint16_t)value;
        cmd.u.tile.after.shape = (uint8_t)(value ? value2 : 0);
        cmd.u.tile.after.rotation = (uint8_t)(value ? value3 : 0);
        break;
    default:
        cmd.u.tile.after.settings ^= (uint8_t)value;
        break;
    }
    if( memcmp(&cmd.u.tile.before, &cmd.u.tile.after, sizeof(cmd.u.tile.before)) == 0 )
        return;
    Editor_Apply(ed, &cmd);
}

static void
mirror_phase(
    struct Harness* h,
    const char* content_dir,
    const char* tmp_dir,
    uint32_t seed,
    int strokes)
{
    char path[1200];
    char from[1200];
    struct Editor* ed;
    struct TestWorld m;
    struct TestWorld fresh;
    struct Pools pools;
    int rebakes = 0;
    int undos = 0;
    int applied = 0;

    snprintf(path, sizeof(path), "%s/maps", tmp_dir);
    mkdir(tmp_dir, 0755);
    mkdir(path, 0755);
    for( int q = 0; q < SQUARE_COUNT; q++ )
        for( int f = 0; f < 2; f++ )
        {
            const char* ext = f == 0 ? "jm2" : "jl2";
            snprintf(from, sizeof(from), "%s/maps/m%d_%d.%s", content_dir, k_squares[q][0],
                k_squares[q][1], ext);
            snprintf(path, sizeof(path), "%s/maps/m%d_%d.%s", tmp_dir, k_squares[q][0],
                k_squares[q][1], ext);
            CHECK(copy_file(from, path), "copy %s", from);
        }

    ed = calloc(1, sizeof(*ed));
    assert(ed);
    CHECK(Editor_Open(ed, tmp_dir, NULL, &h->profile), "the embedded ToriRSMapEd opens %s", tmp_dir);

    world_new(h, &m);
    WorldBuilder_SetLayout(m.builder, WORLD_BUILDER_LAYOUT_EDIT);
    for( int q = 0; q < SQUARE_COUNT; q++ )
    {
        CHECK(Editor_LoadSquare(ed, h->provider, k_squares[q][0], k_squares[q][1]),
            "the mirror loads m%d_%d", k_squares[q][0], k_squares[q][1]);
        app_map_editor_edit_seed(ed, h->provider, m.builder, k_squares[q][0], k_squares[q][1]);
    }
    world_load(h, &m);
    CHECK(WorldEdit_Ready(m.builder), "mirror: EDIT ready after its bake");

    /* Assets the load made resident, from the mirror's own documents. */
    {
        struct Session tmp;
        struct Editor_Square squares[SQUARE_COUNT];
        memset(&tmp, 0, sizeof(tmp));
        for( int q = 0; q < SQUARE_COUNT; q++ )
            squares[q] = *Editor_DocFindSquare(&ed->doc, k_squares[q][0], k_squares[q][1]);
        tmp.squares = squares;
        collect_pools(&tmp);
        pools = tmp.pools;
    }

    g_rng = (seed ? seed : 1) ^ 0x9e3779b9u;
    for( int k = 0; k < strokes; k++ )
    {
        int kind = (int)(rnd() % 7);
        int cx = rnd() % 2 ? 63 + rnd_range(-2, 2) : rnd_range(2, 125);
        int cz = rnd() % 2 ? 63 + rnd_range(-2, 2) : rnd_range(2, 125);
        int level = rnd() % 5 == 0 ? 1 : 0;
        int radius = rnd_range(0, 2);

        Editor_StrokeBegin(ed);
        if( kind <= 3 )
        {
            int a = 0;
            int b = 0;
            int c = 0;
            int field = kind <= 1 ? 0 : kind - 1;
            if( field == 1 )
                a = rnd() % 6 == 0 ? 0 : pools.underlays[rnd() % (uint32_t)pools.underlay_count];
            if( field == 2 )
            {
                a = rnd() % 5 == 0 ? 0 : pools.overlays[rnd() % (uint32_t)pools.overlay_count];
                b = rnd_range(0, 11);
                c = rnd_range(0, 3);
            }
            for( int x = cx - radius; x <= cx + radius; x++ )
                for( int z = cz - radius; z <= cz + radius; z++ )
                {
                    if( field == 0 )
                        a = rnd_range(1, 48);
                    mirror_tile(ed, x, z, level, field, a, b, c);
                    applied++;
                }
        }
        else if( kind == 4 )
        {
            /* flags; a bridge flag now and then, which asks for a rebake */
            static const int bits[] = { 1, 4, 8 };
            int bridge = k % 17 == 9;
            mirror_tile(ed, cx, cz, bridge ? 1 : rnd_range(0, 2), 3,
                bridge ? 2 : bits[rnd() % 3], 0, 0);
            applied++;
        }
        else if( kind == 5 )
        {
            struct Editor_Square* sq = mirror_square(ed, cx, cz);
            if( sq && sq->loc_count > 0 )
            {
                struct Editor_Cmd cmd;
                Editor_CmdInitSquare(&cmd, EDITOR_CMD_LOC, sq->map_x, sq->map_z);
                cmd.has_before = 1;
                cmd.u.loc.before = sq->locs[rnd() % (uint32_t)sq->loc_count];
                applied += Editor_Apply(ed, &cmd);
            }
        }
        else
        {
            int x = cx < 4 ? 4 : cx > 123 ? 123 : cx;
            int z = cz < 4 ? 4 : cz > 123 ? 123 : cz;
            struct Editor_Square* sq = mirror_square(ed, x, z);
            struct Editor_Loc loc = pools.locs[rnd() % (uint32_t)pools.loc_count];
            int free_layer = 1;
            loc.x = x % 64;
            loc.z = z % 64;
            loc.rotation = rnd_range(0, 3);
            if( World_LocShapeToLayer(loc.shape) != 2 )
                for( int i = 0; i < sq->loc_count; i++ )
                    if( sq->locs[i].level == loc.level && sq->locs[i].x == loc.x &&
                        sq->locs[i].z == loc.z &&
                        World_LocShapeToLayer(sq->locs[i].shape) == World_LocShapeToLayer(loc.shape) )
                        free_layer = 0;
            if( free_layer )
            {
                struct Editor_Cmd cmd;
                Editor_CmdInitSquare(&cmd, EDITOR_CMD_LOC, sq->map_x, sq->map_z);
                cmd.has_after = 1;
                cmd.u.loc.after = loc;
                applied += Editor_Apply(ed, &cmd);
            }
        }
        Editor_StrokeEnd(ed);
        /* The shared history, as other clients see it. */
        if( k % 5 == 4 )
            undos += Editor_Undo(ed) > 0;
        if( k % 11 == 10 )
            Editor_Redo(ed);

        /* The frame: the client's EDIT flush path. */
        Editor_PumpFacts(ed);
        if( app_map_editor_edit_step(ed, h->provider, m.builder, m.world, m.scene) )
        {
            world_load(h, &m);
            rebakes++;
        }
        world_drain(&m);
    }
    /* A bridge flag through the editor: the step answers with a rebake, and
     * edits after it flush locally again. */
    {
        int before = rebakes;
        mirror_tile(ed, 30, 90, 1, 3, 2, 0, 0);
        Editor_PumpFacts(ed);
        if( app_map_editor_edit_step(ed, h->provider, m.builder, m.world, m.scene) )
        {
            world_load(h, &m);
            rebakes++;
        }
        world_drain(&m);
        CHECK(rebakes == before + 1, "mirror: a LINK_BELOW command asks for a rebake");
        for( int x = 28; x <= 33; x++ )
            mirror_tile(ed, x, 92, 0, 0, rnd_range(1, 48), 0, 0);
        Editor_PumpFacts(ed);
        CHECK(!app_map_editor_edit_step(ed, h->provider, m.builder, m.world, m.scene),
            "mirror: edits after the rebake flush locally");
        world_drain(&m);
    }
    printf("mirror: %d strokes, %d commands applied, %d undone strokes, %d rebakes\n", strokes,
        applied, undos, rebakes);

    /* The provider holds what the documents derive to -- the invariant a
     * window rebake depends on. */
    {
        struct ToriRS_MapTerrain* derived = malloc(sizeof(*derived));
        int bad_tiles = 0;
        int bad_locs = 0;
        assert(derived);
        for( int q = 0; q < SQUARE_COUNT; q++ )
        {
            struct Editor_Square* sq = Editor_DocFindSquare(&ed->doc, k_squares[q][0], k_squares[q][1]);
            int map_id = CacheProvider_MapId(sq->map_x, sq->map_z);
            struct ToriRS_MapTerrain* held = CacheProvider_MapTerrainGet(h->provider, map_id);
            struct ToriRS_MapLocs* held_locs = CacheProvider_MapSceneryGet(h->provider, map_id);
            Editor_SquareDeriveTerrain(sq, &h->profile, derived);
            for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
                if( !floor_equal(&derived->tiles_xyz[i], &held->tiles_xyz[i]) )
                    bad_tiles++;
            if( held_locs->locs_count != sq->loc_count )
                bad_locs++;
            else
                for( int i = 0; i < sq->loc_count; i++ )
                    if( held_locs->locs[i].loc_id != sq->locs[i].loc_id ||
                        held_locs->locs[i].shape_select != sq->locs[i].shape ||
                        held_locs->locs[i].orientation != sq->locs[i].rotation ||
                        held_locs->locs[i].chunk_pos_x != sq->locs[i].x ||
                        held_locs->locs[i].chunk_pos_z != sq->locs[i].z ||
                        held_locs->locs[i].chunk_pos_level != sq->locs[i].level )
                        bad_locs++;
        }
        free(derived);
        CHECK(bad_tiles == 0, "mirror: %d provider tiles differ from the documents' derive", bad_tiles);
        CHECK(bad_locs == 0, "mirror: %d provider loc rows differ from the documents", bad_locs);
    }

    world_new(h, &fresh);
    world_load(h, &fresh);
    compare_worlds("mirror: EDIT through ToriRSMapEd vs STATIC rebuild", &m, &fresh);

    Editor_Close(ed);
    free(ed);
}
#endif /* EDIT_WORLD_TEST_FINGERPRINT_ONLY */

/* ---- main ----------------------------------------------------------------- */

int
main(
    int argc,
    char** argv)
{
    struct Harness h;
    struct Editor_Square squares[SQUARE_COUNT];
    const char* content_dir;
    const char* cache_dir;
    uint32_t seed = 12345;
    int strokes = 60;

    /* Unbuffered: a crash mid-run still shows how far it got. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if( argc < 3 )
    {
        fprintf(stderr, "usage: edit_world_test <content-dir> <cache-dir> [seed] [strokes]\n");
        return 2;
    }
    content_dir = argv[1];
    cache_dir = argv[2];
    if( argc > 3 )
        seed = (uint32_t)strtoul(argv[3], NULL, 10);
    if( argc > 4 )
        strokes = atoi(argv[4]);

#ifndef EDIT_WORLD_TEST_FINGERPRINT_ONLY
    if( argc > 5 )
    {
        static const struct
        {
            const char* file;
            enum BootManifestWorldLayout want;
        } manifests[] = {
            { "manifests/manifest_osrs241_mapeditor.ini", BOOTMANIFEST_WORLD_LAYOUT_EDIT },
            { "manifests/manifest_osrs239_mapeditor.ini", BOOTMANIFEST_WORLD_LAYOUT_EDIT },
            { "manifests/manifest_osrs241.ini", BOOTMANIFEST_WORLD_LAYOUT_STATIC },
        };
        for( size_t i = 0; i < sizeof(manifests) / sizeof(manifests[0]); i++ )
        {
            static struct BootManifest bm;
            char path[1200];
            snprintf(path, sizeof(path), "%s/%s", argv[5], manifests[i].file);
            memset(&bm, 0, sizeof(bm));
            CHECK(BootManifest_LoadFile(&bm, path) == 0, "%s loads", manifests[i].file);
            CHECK(bm.world_layout == manifests[i].want, "%s states layout %d (want %d)",
                manifests[i].file, (int)bm.world_layout, (int)manifests[i].want);
        }
        {
            enum WorldBuilderLayout parsed;
            CHECK(WorldBuilder_LayoutParse("edit", &parsed) && parsed == WORLD_BUILDER_LAYOUT_EDIT,
                "\"edit\" parses");
            CHECK(WorldBuilder_LayoutParse("static", &parsed) && parsed == WORLD_BUILDER_LAYOUT_STATIC,
                "\"static\" parses");
            CHECK(!WorldBuilder_LayoutParse("dynamic", &parsed), "an unknown layout is refused");
        }
    }
#endif

    memset(&h, 0, sizeof(h));
    h.io = ToriRS_IOBatch_New();
    h.queue = ToriRS_TaskQueue_New();
    h.bc = dat2_buildcache_new();
    h.provider = dat2_buildcache_as_provider(h.bc);
    CacheProvider_SetAssetQueue(h.provider, h.queue);
    h.disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !h.disk )
    {
        printf("FAIL: cannot open the dat2 cache at %s\n", cache_dir);
        return 1;
    }
    if( !RSCache_ProfileByName("osrs241", &h.profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
    RSCache_Dat2DiskSetProfile(h.disk, &h.profile);
    CacheProvider_SetProfile(h.provider, &h.profile);
    h.px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(h.px, h.disk);
    ToriDraw_Init();

    for( int q = 0; q < SQUARE_COUNT; q++ )
    {
        if( !load_square(content_dir, &squares[q], k_squares[q][0], k_squares[q][1]) )
        {
            printf("FAIL: cannot read m%d_%d from %s\n", k_squares[q][0], k_squares[q][1],
                content_dir);
            return 1;
        }
        seed_square(&h, &squares[q]);
    }

    /* The STATIC bake of the unedited window. */
    {
        struct TestWorld unedited;
        world_new(&h, &unedited);
        world_load(&h, &unedited);
        printf("static fingerprint (unedited m50_50..m51_51): %016llx\n",
            (unsigned long long)scene_fingerprint(&unedited));
#ifdef EDIT_WORLD_TEST_FINGERPRINT_ONLY
        return 0;
    }
#else

        {
            struct TestWorld edit;
            struct TestWorld fresh;
            struct Session s;
            struct WorldEditFlushStats stats;
            double total_ms = 0.0;
            double max_ms = 0.0;
            int flushes = 0;

            check_tile_derive("unedited", &h, squares);

            /* The EDIT bake is the STATIC bake. */
            world_new(&h, &edit);
            WorldBuilder_SetLayout(edit.builder, WORLD_BUILDER_LAYOUT_EDIT);
            CHECK(WorldBuilder_GetLayout(edit.builder) == WORLD_BUILDER_LAYOUT_EDIT, "layout is edit");
            world_load(&h, &edit);
            CHECK(WorldEdit_Ready(edit.builder), "EDIT is ready after its bake");
            compare_worlds("EDIT bake vs STATIC bake", &edit, &unedited);

            memset(&s, 0, sizeof(s));
            s.h = &h;
            s.edit = &edit;
            s.squares = squares;
            g_rng = seed ? seed : 1;
            refresh_handles(&s);
            collect_pools(&s);
            printf("pools: %d underlays, %d overlays, %d locs\n", s.pools.underlay_count,
                s.pools.overlay_count, s.pools.loc_count);

            /*
             * The measured stroke: a 20-tile height drag across the seam
             * between m50_50 and m51_50 (scene x 63 | 64), one flush, as one
             * frame of a brush drag.
             */
            for( int i = 0; i < 20; i++ )
            {
                int x = 54 + i;
                struct Editor_Tile* t = tile_at(&s, x, 30, 0);
                t->has_height = 1;
                t->height = (uint8_t)(10 + (i % 7) * 3);
                push_column(&s, x, 30);
            }
            flush(&s, &stats);
            printf("brush stroke flush (20 tiles across the m50_50|m51_50 seam): %.3f ms -- "
                   "tiles set %d, corners relit %d, tiles reblended %d, tiles remeshed %d, "
                   "placements re-placed %d\n",
                stats.ms, stats.tiles_set, stats.corners_relit, stats.tiles_reblended,
                stats.tiles_remeshed, stats.placements_rebuilt);

            for( int k = 0; k < strokes; k++ )
            {
                int kind = (int)(rnd() % 9);
                int cx = rnd() % 2 ? 63 + rnd_range(-2, 2) : rnd_range(2, 125);
                int cz = rnd() % 2 ? 63 + rnd_range(-2, 2) : rnd_range(2, 125);
                int level = rnd() % 5 == 0 ? 1 : 0;

                switch( kind )
                {
                case 0:
                case 1:
                    edit_heights(&s, cx, cz, rnd_range(0, 3), level);
                    break;
                case 2:
                    edit_underlay(&s, cx, cz, rnd_range(0, 2), level);
                    break;
                case 3:
                    edit_overlay(&s, cx, cz, rnd_range(0, 1), level);
                    break;
                case 4:
                    edit_flags(&s, k % 23 == 11);
                    break;
                case 5:
                    for( int n = rnd_range(1, 3); n > 0; n-- )
                        edit_loc_remove(&s);
                    break;
                case 6:
                    for( int n = rnd_range(1, 3); n > 0; n-- )
                        edit_loc_add(&s, rnd_range(4, 123), rnd_range(4, 123));
                    break;
                default:
                    edit_heights(&s, cx, cz, rnd_range(0, 2), 0);
                    edit_loc_add(&s, cx < 4 ? 4 : cx > 123 ? 123 : cx, cz < 4 ? 4 : cz > 123 ? 123 : cz);
                    edit_loc_remove(&s);
                    break;
                }
                flush(&s, &stats);
                /* EDIT_WORLD_TEST_BISECT=1: compare after every stroke and stop
                 * at the first that diverges -- slow, for finding a bug. */
                if( getenv("EDIT_WORLD_TEST_BISECT") )
                {
                    struct TestWorld probe;
                    int before = g_failed;
                    world_new(&h, &probe);
                    world_load(&h, &probe);
                    compare_worlds("bisect", &edit, &probe);
                    if( g_failed != before )
                    {
                        printf("bisect: stroke %d (kind %d at %d,%d level %d) diverged\n", k, kind,
                            cx, cz, level);
                        break;
                    }
                }
                if( stats.ms > 0.0 )
                {
                    total_ms += stats.ms;
                    flushes++;
                    if( stats.ms > max_ms )
                        max_ms = stats.ms;
                }
            }
            /* A bridge flag (LINK_BELOW on level 1) is the one edit a flush does
             * not apply locally: it must answer REBUILD, and the rebake shows
             * it. */
            {
                int rebuilds_before = s.rebuilds;
                struct Editor_Tile* t = tile_at(&s, 40, 40, 1);
                t->settings ^= 2;
                push_column(&s, 40, 40);
                flush(&s, &stats);
                CHECK(s.rebuilds == rebuilds_before + 1, "a LINK_BELOW edit asks for a rebuild");
                /* and the layout keeps editing locally after the rebake */
                edit_heights(&s, 41, 41, 1, 0);
                edit_loc_add(&s, 44, 44);
                flush(&s, &stats);
                CHECK(s.rebuilds == rebuilds_before + 1, "edits after the rebake flush locally");
            }

            /* The handle queries: the first live placement at a key, and its
             * record. */
            {
                struct Editor_Square* sq = &squares[0];
                struct World_LocPlacement got;
                int found = 0;
                for( int i = 0; i < sq->loc_count && !found; i++ )
                {
                    const struct Editor_Loc* loc = &sq->locs[i];
                    int first = -1;
                    int h2;
                    for( int k = 0; k < sq->loc_count; k++ )
                        if( sq->locs[k].level == loc->level && sq->locs[k].x == loc->x &&
                            sq->locs[k].z == loc->z && sq->locs[k].shape == loc->shape )
                        {
                            first = k;
                            break;
                        }
                    h2 = WorldEdit_FindPlacement(edit.builder, loc->x, loc->z, loc->level, loc->shape);
                    CHECK(h2 == s.handles[0][first], "FindPlacement names the document's first match");
                    CHECK(WorldEdit_PlacementGet(edit.builder, h2, &got) && got.loc_id == sq->locs[first].loc_id,
                        "PlacementGet reads the placement back");
                    found = 1;
                }
            }

            printf("random strokes: %d (seed %u), %d flushes with work, mean %.3f ms, max %.3f ms, "
                   "%d window rebuilds asked for (LINK_BELOW)\n",
                strokes, seed, flushes, flushes ? total_ms / flushes : 0.0, max_ms, s.rebuilds);

            check_tile_derive("edited", &h, squares);

            /* The STATIC bake of the same edited squares. */
            world_new(&h, &fresh);
            world_load(&h, &fresh);
            compare_worlds("EDIT after edits vs STATIC rebuild", &edit, &fresh);
            printf("static fingerprint (edited): %016llx, edit: %016llx\n",
                (unsigned long long)scene_fingerprint(&fresh),
                (unsigned long long)scene_fingerprint(&edit));
        }
    }

    if( argc > 6 )
        mirror_phase(&h, content_dir, argv[6], seed, strokes);

    printf("edit_world_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
#endif
}
