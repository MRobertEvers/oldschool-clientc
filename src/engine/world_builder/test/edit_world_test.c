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
#include "editor/editor_host.h"
#include "editor/editor_panel.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanel_ops.h"
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
#include "torirsmaped/torirs_maped.h"
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

/* ---- locs: delete, undo, redo through the editor ------------------------------
 *
 * The owner's report (2026-11-02): "delete and the undo redo buffer are buggy
 * when adding or removing locs". Every add / remove / undo / redo sequence a
 * user makes, through the embedded authority, the mirror's echo and the
 * client's EDIT flush, checked on BOTH sides: the document (the loc list, and
 * the .jl2 Save would write -- byte-identical after an undo) and the world
 * (the scenery the scene holds on the tile: id, shape, angle; nothing left
 * behind, nothing doubled; a restored loc's element selects and deletes).
 * The STATIC rebuild that closes mirror_phase then holds the world to the
 * document over every tile. */

struct LocUndo
{
    struct Harness* h;
    struct Editor* ed;
    struct TestWorld* m;
    int* rebakes;
};

static void
lu_step(struct LocUndo* lu)
{
    Editor_PumpFacts(lu->ed);
    if( app_map_editor_edit_step(lu->ed, lu->h->provider, lu->m->builder, lu->m->world, lu->m->scene) )
    {
        world_load(lu->h, lu->m);
        (*lu->rebakes)++;
    }
    world_drain(lu->m);
}

static char*
lu_jl2(const struct Editor_Square* sq)
{
    size_t size = Editor_Jl2Emit(sq, NULL, 0);
    char* text = malloc(size + 1);
    assert(text);
    Editor_Jl2Emit(sq, text, size + 1);
    return text;
}

static void
lu_scene_of(
    const struct LocUndo* lu,
    const struct Editor_Square* sq,
    int x,
    int z,
    int* out_sx,
    int* out_sz)
{
    *out_sx = World_ToSceneX(lu->m->world, sq->map_x, x);
    *out_sz = World_ToSceneZ(lu->m->world, sq->map_z, z);
}

/** Scenery on a scene tile: every entity, or (loc_id >= 0) the ones equal to
 *  that loc, shape and angle. The element id of the last match. */
static int
lu_world_count(
    const struct LocUndo* lu,
    int sx,
    int sz,
    int level,
    int loc_id,
    int shape,
    int angle,
    int* out_element)
{
    struct World_EntityPool* pool = &lu->m->world->entities.scenery;
    int n = 0;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
        if( !sc || sc->grid_position.x != sx || sc->grid_position.z != sz || sc->grid_position.level != level )
            continue;
        if( loc_id >= 0 && (sc->loc_id != loc_id || sc->shape != shape || (sc->angle & 3) != angle) )
            continue;
        if( out_element )
            *out_element = sc->element_id;
        n++;
    }
    return n;
}

static int
lu_world_has(
    const struct LocUndo* lu,
    const struct Editor_Square* sq,
    const struct Editor_Loc* loc,
    int* out_element)
{
    int sx;
    int sz;
    lu_scene_of(lu, sq, loc->x, loc->z, &sx, &sz);
    return lu_world_count(lu, sx, sz, loc->level, loc->loc_id, loc->shape, loc->rotation, out_element);
}

static int
lu_doc_count(
    const struct Editor_Square* sq,
    int level,
    int x,
    int z)
{
    int n = 0;
    for( int i = 0; i < sq->loc_count; i++ )
        if( sq->locs[i].level == level && sq->locs[i].x == x && sq->locs[i].z == z )
            n++;
    return n;
}

/** A loc the EDIT world places locally: config present, no multiloc. */
static int
lu_plain(
    const struct LocUndo* lu,
    int loc_id)
{
    struct ToriRS_Location* cfg = CacheProvider_LocationGet(lu->h->provider, loc_id);
    return cfg && cfg->transform_count == 0;
}

/* After every sequence the EDIT world must be what a fresh STATIC bake of the
 * same documents is: nothing left behind, nothing doubled, and every loc filed
 * in the document's order (the shade replay and a tile's last wall follow it). */
static void
lu_bisect(
    struct LocUndo* lu,
    const char* label)
{
    struct TestWorld fresh;
    world_new(lu->h, &fresh);
    world_load(lu->h, &fresh);
    compare_worlds(label, lu->m, &fresh);
    world_drain(&fresh);
    WorldBuilder_Free(fresh.builder);
    World_Free(fresh.world);
    ToriDraw_SceneFree(fresh.scene);
    VarPManager_Free(&fresh.varp);
}

/** ui->app_services.loc_record for the tests: the harness's provider. */
static struct ToriRS_Location*
lu_loc_record(
    void* user_data,
    int loc_id)
{
    return CacheProvider_LocationGet(user_data, loc_id);
}

/** Submit commands as the panels do (edp_apply): one stroke, one named row. */
static int
lu_apply(
    struct LocUndo* lu,
    const struct Editor_Cmd* commands,
    int count,
    const char* verb)
{
    int applied = 0;
    Editor_StrokeBegin(lu->ed);
    Editor_HistoryName(lu->ed, verb);
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(lu->ed, &commands[i]) ? 1 : 0;
    Editor_StrokeEnd(lu->ed);
    lu_step(lu);
    return applied;
}

/** The ref Editor_PanelDeleteLocByElement / SelectLoc build from a world
 *  element. 0 when the element is not live. */
static int
lu_ref_of_element(
    const struct LocUndo* lu,
    const struct Editor_Square* sq,
    int element_id,
    struct Editor_LocRef* out)
{
    struct WorldEntity_Scenery* sc = World_SceneryGetByElementId(lu->m->world, element_id);
    if( !sc )
        return 0;
    Editor_LocRefAt(out, sq->map_x * 64 + (sc->grid_position.x - World_ToSceneX(lu->m->world, sq->map_x, 0)),
        sq->map_z * 64 + (sc->grid_position.z - World_ToSceneZ(lu->m->world, sq->map_z, 0)),
        sc->grid_position.level, sc->shape, sc->loc_id, sc->angle & 3);
    return 1;
}

static void
loc_undo_phase(
    struct Harness* h,
    struct Editor* ed,
    struct TestWorld* m,
    const struct Pools* pools,
    int* rebakes)
{
    struct LocUndo lu = { h, ed, m, rebakes };
    struct Editor_Square* sq = Editor_DocFindSquare(&ed->doc, BASE_X, BASE_Z);
    const struct Editor_History* hist = Editor_GetHistory(ed);
    struct Editor_Loc P;
    struct Editor_Loc Q;
    struct Editor_LocOpResult result;
    struct Editor_Cmd cmds[32];
    int empty_x = -1;
    int empty_z = -1;
    int empty2_x = -1;
    int empty2_z = -1;
    int twin = -1;
    int multi_x = -1;
    int multi_z = -1;
    int have_p = 0;
    int have_q = 0;
    int n;

    assert(sq);
    /* A removed element tagged with its kind (every scenery and terrain
     * element is) hands its slot back: the next add reuses it. */
    {
        int const raw = ToriDraw_SceneElementAddPool(m->scene, TORIDRAW_SCENE_POOL_DYNAMIC);
        int const tagged = ElementId_Raw(ElementId_Make(TORIDRAW_ELEMENT_KIND_SCENERY, raw));
        int again;
        CHECK(raw >= 0, "locs: a scene element allocates");
        ToriDraw_SceneElementRemove(m->scene, tagged);
        again = ToriDraw_SceneElementAddPool(m->scene, TORIDRAW_SCENE_POOL_DYNAMIC);
        CHECK(again == raw, "locs: removing a kind-tagged element frees its slot (re-add got %d, freed %d)", again,
            raw);
        ToriDraw_SceneElementRemove(m->scene, again);
    }
    /* Two plain centrepieces from the pools, different ids. */
    for( int i = 0; i < pools->loc_count; i++ )
    {
        const struct Editor_Loc* c = &pools->locs[i];
        if( c->shape != 10 || !lu_plain(&lu, c->loc_id) )
            continue;
        if( !have_p )
        {
            P = *c;
            have_p = 1;
        }
        else if( !have_q && c->loc_id != P.loc_id )
        {
            Q = *c;
            have_q = 1;
        }
    }
    CHECK(have_p && have_q, "locs: two plain shape-10 locs in the pools");
    if( !have_p || !have_q )
        return;

    /* Tiles of m50_50, level 0, away from the edges: two with no loc, one
     * holding a lone plain shape-10 loc, one holding several layers. */
    for( int z = 8; z < 56; z++ )
        for( int x = 8; x < 56; x++ )
        {
            int const here = lu_doc_count(sq, 0, x, z);
            if( here == 0 && lu_doc_count(sq, 0, x + 2, z) == 0 && (x % 3) == 0 )
            {
                if( empty_x < 0 )
                {
                    empty_x = x;
                    empty_z = z;
                }
                else if( empty2_x < 0 && (z != empty_z || x > empty_x + 4) )
                {
                    empty2_x = x;
                    empty2_z = z;
                }
            }
            if( here >= 2 && multi_x < 0 )
            {
                multi_x = x;
                multi_z = z;
            }
            if( here == 1 && twin < 0 )
            {
                int const k = Editor_SquareLocFind(sq, 0, x, z, 10);
                if( k >= 0 && sq->locs[k].loc_id != P.loc_id && lu_plain(&lu, sq->locs[k].loc_id) )
                    twin = k;
            }
        }
    CHECK(empty_x >= 0 && empty2_x >= 0 && twin >= 0 && multi_x >= 0,
        "locs: test tiles found (empty %d,%d / %d,%d, lone centrepiece %d, multi %d,%d)", empty_x, empty_z,
        empty2_x, empty2_z, twin, multi_x, multi_z);
    if( empty_x < 0 || empty2_x < 0 || twin < 0 || multi_x < 0 )
        return;
    lu_step(&lu);

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 1");
    /* 1. Place, undo, redo. */
    {
        char* before = lu_jl2(sq);
        int const rows = hist->count;
        struct Editor_Loc want = P;
        char* after_text;
        want.x = empty_x;
        want.z = empty_z;
        want.level = 0;
        want.rotation = 1;
        n = Editor_LocOpsPlace(&ed->doc, P.loc_id, 10, 1, BASE_X * 64 + empty_x, BASE_Z * 64 + empty_z, 0, cmds, 1,
            &result);
        CHECK(n == 1 && lu_apply(&lu, cmds, n, "place") == 1, "locs 1: the place applies");
        CHECK(Editor_SquareLocFindExact(sq, &want) >= 0, "locs 1: the document holds the placed loc");
        CHECK(lu_world_has(&lu, sq, &want, NULL) == 1, "locs 1: the world draws it once (id, shape 10, angle 1)");
        CHECK(hist->count == rows + 1 && hist->applied == rows + 1, "locs 1: one History row, applied");
        after_text = lu_jl2(sq);
        CHECK(Editor_Undo(ed) == 1, "locs 1: undo takes one command");
        lu_step(&lu);
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 1: after undo the .jl2 is the file before the place");
            free(text);
        }
        CHECK(lu_world_count(&lu, World_ToSceneX(m->world, BASE_X, empty_x), World_ToSceneZ(m->world, BASE_Z, empty_z),
                  0, -1, 0, 0, NULL) == 0,
            "locs 1: after undo the tile holds no scenery");
        CHECK(hist->count == rows + 1 && hist->applied == rows, "locs 1: the row is the undone prefix");
        CHECK(Editor_Redo(ed) == 1, "locs 1: redo takes one command");
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &want, NULL) == 1, "locs 1: after redo the world draws it again, same shape and angle");
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, after_text) == 0, "locs 1: after redo the .jl2 is the file after the place");
            free(text);
        }
        CHECK(hist->applied == rows + 1, "locs 1: redo re-applies the row");
        /* Leave the tile empty again. */
        Editor_Undo(ed);
        lu_step(&lu);
        free(after_text);
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 2");
    /* 2. Place onto a tile already holding a loc of that shape, undo. */
    {
        struct Editor_Loc const occupant = sq->locs[twin];
        struct Editor_Loc want = Q;
        char* before = lu_jl2(sq);
        int k;
        want.x = occupant.x;
        want.z = occupant.z;
        want.level = 0;
        want.rotation = 0;
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1, "locs 2: the occupant is drawn before");
        n = Editor_LocOpsPlace(&ed->doc, Q.loc_id, 10, 0, BASE_X * 64 + occupant.x, BASE_Z * 64 + occupant.z, 0, cmds,
            1, &result);
        CHECK(n == 1 && lu_apply(&lu, cmds, n, "place") == 1, "locs 2: the place onto the taken shape applies");
        k = Editor_SquareLocFind(sq, 0, occupant.x, occupant.z, 10);
        CHECK(k >= 0 && sq->locs[k].loc_id == Q.loc_id, "locs 2: the tile's shape-10 loc is the placed one");
        CHECK(lu_doc_count(sq, 0, occupant.x, occupant.z) == 1, "locs 2: one loc on the tile, not two (%d)",
            lu_doc_count(sq, 0, occupant.x, occupant.z));
        CHECK(lu_world_has(&lu, sq, &want, NULL) == 1 && lu_world_has(&lu, sq, &occupant, NULL) == 0,
            "locs 2: the world draws the new loc in its place");
        {
            char row[EDITOR_HISTORY_LABEL_MAX * 2];
            Editor_HistoryGroupName(&hist->groups[hist->applied - 1], row, sizeof(row));
            CHECK(strstr(row, "place") != NULL, "locs 2: the History row is the place (\"%s\")", row);
        }
        Editor_Undo(ed);
        lu_step(&lu);
        k = Editor_SquareLocFind(sq, 0, occupant.x, occupant.z, 10);
        CHECK(k >= 0 && sq->locs[k].loc_id == occupant.loc_id, "locs 2: undo puts the occupant back in the document");
        CHECK(lu_doc_count(sq, 0, occupant.x, occupant.z) == 1, "locs 2: and only it");
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1 && lu_world_has(&lu, sq, &want, NULL) == 0,
            "locs 2: the world draws the occupant, not the placed loc");
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 2: after undo the .jl2 is the file before the place");
            free(text);
        }
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 3");
    /* 3. Delete by element from the middle of the list, undo: the file comes
     *    back byte for byte, the world draws it once, its new element selects
     *    and deletes, and the rest of the square keeps its elements. */
    {
        int const mid = sq->loc_count / 2;
        struct Editor_Loc gone;
        struct Editor_Loc tail;
        struct Editor_LocRef ref;
        char* before = lu_jl2(sq);
        int element = -1;
        int tail_element = -1;
        int tail_after = -1;
        int pick = -1;
        for( int i = mid; i < sq->loc_count && pick < 0; i++ )
            if( lu_plain(&lu, sq->locs[i].loc_id) && lu_world_has(&lu, sq, &sq->locs[i], NULL) == 1 )
                pick = i;
        CHECK(pick >= 0, "locs 3: a drawn loc in the middle of m50_50's list");
        if( pick >= 0 )
        {
            gone = sq->locs[pick];
            tail = sq->locs[sq->loc_count - 1];
            (void)lu_world_has(&lu, sq, &tail, &tail_element);
            (void)lu_world_has(&lu, sq, &gone, &element);
            CHECK(lu_ref_of_element(&lu, sq, element, &ref), "locs 3: the element is live");
            n = Editor_LocOpsDelete(&ed->doc, &ref, 1, cmds, 1, &result);
            CHECK(n == 1 && lu_apply(&lu, cmds, n, "delete loc") == 1, "locs 3: delete by element applies");
            CHECK(lu_world_has(&lu, sq, &gone, NULL) == 0, "locs 3: the world no longer draws it");
            CHECK(World_SceneryGetByElementId(m->world, element) == NULL, "locs 3: its element is gone");
            Editor_Undo(ed);
            lu_step(&lu);
            {
                char* text = lu_jl2(sq);
                CHECK(strcmp(text, before) == 0, "locs 3: after undo the .jl2 is the file before the delete");
                free(text);
            }
            element = -1;
            CHECK(lu_world_has(&lu, sq, &gone, &element) == 1, "locs 3: after undo the world draws it once");
            (void)lu_world_has(&lu, sq, &tail, &tail_after);
            CHECK(tail_after == tail_element,
                "locs 3: a loc later in the list keeps its element (%d -> %d): the undo re-placed one loc, not the "
                "rest of the square",
                tail_element, tail_after);
            {
                /* EDIT files it where the list has it: the placement after it
                 * in map order is the next loc in the document (the shade
                 * replay and a tile's last wall follow map order). */
                int const k = Editor_SquareLocFindExact(sq, &gone);
                if( k >= 0 && k + 1 < sq->loc_count )
                {
                    const struct Editor_Loc* nx = &sq->locs[k + 1];
                    int sx;
                    int sz;
                    int nsx;
                    int nsz;
                    int h;
                    int hn;
                    lu_scene_of(&lu, sq, gone.x, gone.z, &sx, &sz);
                    lu_scene_of(&lu, sq, nx->x, nx->z, &nsx, &nsz);
                    h = WorldEdit_FindPlacement(m->builder, sx, sz, gone.level, gone.shape);
                    hn = WorldEdit_FindPlacement(m->builder, nsx, nsz, nx->level, nx->shape);
                    CHECK(h != WORLD_BUILDER_HANDLE_NONE && hn != WORLD_BUILDER_HANDLE_NONE
                              && WorldEdit_PlacementNext(m->builder, h) == hn,
                        "locs 3: the restored loc is filed in map order before the document's next loc");
                }
            }
            CHECK(lu_ref_of_element(&lu, sq, element, &ref) && Editor_LocRefResolve(&ed->doc, &ref) >= 0,
                "locs 3: the restored element selects (its ref resolves in the document)");
            n = Editor_LocOpsDelete(&ed->doc, &ref, 1, cmds, 1, &result);
            CHECK(n == 1 && lu_apply(&lu, cmds, n, "delete loc") == 1, "locs 3: and deletes again");
            CHECK(lu_world_has(&lu, sq, &gone, NULL) == 0, "locs 3: gone from the world again");
            Editor_Undo(ed);
            lu_step(&lu);
            CHECK(lu_world_has(&lu, sq, &gone, NULL) == 1, "locs 3: and back");
        }
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 4");
    /* 4. Clear tile (the C-click: every layer, list order, one step), undo. */
    {
        char* before = lu_jl2(sq);
        int sx;
        int sz;
        int drawn;
        n = 0;
        for( int i = 0; i < sq->loc_count && n < 16; i++ )
            if( sq->locs[i].level == 0 && sq->locs[i].x == multi_x && sq->locs[i].z == multi_z )
            {
                Editor_CmdInitSquare(&cmds[n], EDITOR_CMD_LOC, sq->map_x, sq->map_z);
                cmds[n].has_before = 1;
                cmds[n].u.loc.before = sq->locs[i];
                n++;
            }
        lu_scene_of(&lu, sq, multi_x, multi_z, &sx, &sz);
        drawn = lu_world_count(&lu, sx, sz, 0, -1, 0, 0, NULL);
        CHECK(lu_apply(&lu, cmds, n, "clear tile locs") == n, "locs 4: clear tile takes all %d locs", n);
        CHECK(lu_doc_count(sq, 0, multi_x, multi_z) == 0, "locs 4: the document's tile is empty");
        CHECK(lu_world_count(&lu, sx, sz, 0, -1, 0, 0, NULL) == 0, "locs 4: the world's tile is empty");
        CHECK(Editor_Undo(ed) == n, "locs 4: one undo step brings back all %d", n);
        lu_step(&lu);
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 4: after undo the .jl2 is the file before the clear");
            free(text);
        }
        CHECK(lu_world_count(&lu, sx, sz, 0, -1, 0, 0, NULL) == drawn, "locs 4: the world draws the %d it drew before",
            drawn);
        Editor_Redo(ed);
        lu_step(&lu);
        CHECK(lu_world_count(&lu, sx, sz, 0, -1, 0, 0, NULL) == 0, "locs 4: redo clears the tile again");
        Editor_Undo(ed);
        lu_step(&lu);
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 5");
    /* 5. Two locs on two tiles selected, Delete, undo (both back), redo. */
    {
        struct Editor_LocRef refs[2];
        struct Editor_Loc a = sq->locs[twin];
        struct Editor_Loc b;
        int bi = -1;
        for( int i = 0; i < sq->loc_count && bi < 0; i++ )
            if( (sq->locs[i].x != a.x || sq->locs[i].z != a.z) && lu_plain(&lu, sq->locs[i].loc_id)
                && lu_world_has(&lu, sq, &sq->locs[i], NULL) == 1 )
                bi = i;
        b = sq->locs[bi];
        Editor_LocRefAt(&refs[0], BASE_X * 64 + a.x, BASE_Z * 64 + a.z, a.level, a.shape, a.loc_id, a.rotation);
        Editor_LocRefAt(&refs[1], BASE_X * 64 + b.x, BASE_Z * 64 + b.z, b.level, b.shape, b.loc_id, b.rotation);
        n = Editor_LocOpsDelete(&ed->doc, refs, 2, cmds, 2, &result);
        CHECK(n == 2 && lu_apply(&lu, cmds, n, "delete") == 2, "locs 5: the two-loc delete applies");
        CHECK(lu_world_has(&lu, sq, &a, NULL) == 0 && lu_world_has(&lu, sq, &b, NULL) == 0, "locs 5: both gone");
        CHECK(Editor_Undo(ed) == 2, "locs 5: one undo brings both");
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &a, NULL) == 1 && lu_world_has(&lu, sq, &b, NULL) == 1, "locs 5: both drawn again");
        CHECK(Editor_Redo(ed) == 2, "locs 5: one redo takes both");
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &a, NULL) == 0 && lu_world_has(&lu, sq, &b, NULL) == 0, "locs 5: both gone again");
        Editor_Undo(ed);
        lu_step(&lu);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 6");
    /* 6. Place, move (the Move tool), undo twice. */
    {
        struct Editor_Loc at = P;
        struct Editor_Loc moved;
        struct Editor_LocRef placed;
        char* before = lu_jl2(sq);
        at.x = empty_x;
        at.z = empty_z;
        at.level = 0;
        at.rotation = 0;
        moved = at;
        moved.x = empty_x + 2;
        n = Editor_LocOpsPlace(&ed->doc, P.loc_id, 10, 0, BASE_X * 64 + empty_x, BASE_Z * 64 + empty_z, 0, cmds, 1,
            &result);
        lu_apply(&lu, cmds, n, "place");
        placed = result.refs[0];

        n = Editor_LocOpsMoveTo(&ed->doc, &placed, 1, BASE_X * 64 + empty_x + 2, BASE_Z * 64 + empty_z, cmds, 2,
            &result);
        CHECK(n == 1 && lu_apply(&lu, cmds, n, "move") == 1, "locs 6: the move applies");
        CHECK(lu_world_has(&lu, sq, &moved, NULL) == 1 && lu_world_has(&lu, sq, &at, NULL) == 0,
            "locs 6: the world draws it on the new tile only");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &at, NULL) == 1 && lu_world_has(&lu, sq, &moved, NULL) == 0,
            "locs 6: undo puts it back on the first tile only");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &at, NULL) == 0 && lu_world_has(&lu, sq, &moved, NULL) == 0,
            "locs 6: the second undo takes the place");
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 6: the .jl2 is the file before the place");
            free(text);
        }
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 7");
    /* 7. Delete, place a different loc on the same tile, undo, undo. */
    {
        struct Editor_Loc const occupant = sq->locs[twin];
        struct Editor_Loc want = Q;
        struct Editor_LocRef ref;
        char* before = lu_jl2(sq);
        want.x = occupant.x;
        want.z = occupant.z;
        want.level = 0;
        want.rotation = 2;
        Editor_LocRefAt(&ref, BASE_X * 64 + occupant.x, BASE_Z * 64 + occupant.z, 0, 10, occupant.loc_id,
            occupant.rotation);
        n = Editor_LocOpsDelete(&ed->doc, &ref, 1, cmds, 1, &result);
        lu_apply(&lu, cmds, n, "delete");
        n = Editor_LocOpsPlace(&ed->doc, Q.loc_id, 10, 2, BASE_X * 64 + occupant.x, BASE_Z * 64 + occupant.z, 0, cmds,
            1, &result);
        CHECK(n == 1 && !cmds[0].has_before, "locs 7: on the emptied tile the place is a plain add");
        lu_apply(&lu, cmds, n, "place");
        CHECK(lu_world_has(&lu, sq, &want, NULL) == 1 && lu_world_has(&lu, sq, &occupant, NULL) == 0,
            "locs 7: the world draws the new loc");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(lu_world_count(&lu, World_ToSceneX(m->world, BASE_X, occupant.x),
                  World_ToSceneZ(m->world, BASE_Z, occupant.z), 0, -1, 0, 0, NULL) == 0,
            "locs 7: the first undo empties the tile");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1 && lu_world_has(&lu, sq, &want, NULL) == 0,
            "locs 7: the second brings the deleted loc back");
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 7: the .jl2 is the file before the delete");
            free(text);
        }
        free(before);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 8");
    /* 8. The same square in two windows: both delete the same loc; the second
     *    is refused, one undo brings back one loc, in both windows. */
    {
        static struct Editor peer;
        struct EditorHost peer_host;
        struct Editor_Loc const occupant = sq->locs[twin];
        struct Editor_Square* psq;
        struct Editor_Cmd mine;
        struct Editor_Cmd theirs;
        int k;

        CHECK(Editor_HostOpenMapEdEmbedPeer(&peer_host, &ed->host, TORIRSMAPED_ROLE_GENERIC, 1),
            "locs 8: a second window joins");
        Editor_OpenHost(&peer, &peer_host, "second window", &h->profile);
        CHECK(Editor_OpenDoc(&peer, EDITOR_DOC_SQUARE, "m50_50"), "locs 8: the second window opens m50_50");
        psq = Editor_DocFindSquare(&peer.doc, BASE_X, BASE_Z);
        CHECK(psq && psq->loc_count == sq->loc_count, "locs 8: both windows hold the same %d locs", sq->loc_count);
        Editor_CmdInitSquare(&mine, EDITOR_CMD_LOC, BASE_X, BASE_Z);
        mine.has_before = 1;
        mine.u.loc.before = occupant;
        theirs = mine;
        CHECK(lu_apply(&lu, &mine, 1, "delete loc") == 1, "locs 8: this window's delete applies");
        Editor_PumpFacts(&peer);
        CHECK(Editor_Apply(&peer, &theirs) == 0, "locs 8: the other window's delete of the same loc is refused");
        lu_step(&lu);
        Editor_Undo(ed);
        lu_step(&lu);
        Editor_PumpFacts(&peer);
        k = 0;
        for( int i = 0; i < sq->loc_count; i++ )
            k += memcmp(&sq->locs[i], &occupant, sizeof(occupant)) == 0;
        CHECK(k == 1, "locs 8: one undo brings back exactly one loc (%d)", k);
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1, "locs 8: drawn once");
        CHECK(psq && Editor_SquareLocFindExact(psq, &occupant) >= 0 && psq->loc_count == sq->loc_count,
            "locs 8: the other window's document shows the undo too");
        /* An undo from the other window shows in this one. */
        Editor_Apply(&peer, &theirs);
        Editor_PumpFacts(&peer);
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 0, "locs 8: the other window's delete shows here");
        {
            /* This window sees the other's delete as the delete row it had
             * undone, redone (the same command; the authority's new push is
             * the same group either way). */
            char row[EDITOR_HISTORY_LABEL_MAX * 2];
            Editor_HistoryGroupName(&hist->groups[hist->applied - 1], row, sizeof(row));
            CHECK(strstr(row, "elete loc") != NULL && hist->applied == hist->count,
                "locs 8: the other window's delete is the applied delete row here (\"%s\")", row);
        }
        CHECK(Editor_Undo(&peer) == 1, "locs 8: the other window undoes it");
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1 && Editor_SquareLocFindExact(sq, &occupant) >= 0,
            "locs 8: and the undo shows here, document and world");
        /* A replace from the other window (its Place onto a taken shape)
         * reads as one here. */
        {
            struct Editor_Cmd swap;
            char row[EDITOR_HISTORY_LABEL_MAX * 2];
            struct Editor_LocOpResult r2;
            Editor_PumpFacts(&peer);
            CHECK(Editor_LocOpsPlace(&peer.doc, Q.loc_id, 10, 0, BASE_X * 64 + occupant.x, BASE_Z * 64 + occupant.z, 0,
                      &swap, 1, &r2) == 1
                      && swap.has_before,
                "locs 8: the other window's place onto the occupant is a replace");
            Editor_Apply(&peer, &swap);
            lu_step(&lu);
            Editor_HistoryGroupName(&hist->groups[hist->applied - 1], row, sizeof(row));
            CHECK(strncmp(row, "Replace loc", 11) == 0, "locs 8: and reads \"Replace loc ...\" here (\"%s\")", row);
            Editor_Undo(ed);
            lu_step(&lu);
            CHECK(lu_world_has(&lu, sq, &occupant, NULL) == 1, "locs 8: undone from this window, the occupant is back");
        }
        Editor_Close(&peer);
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 9");
    /* 9. History: a new edit after undo discards the redo tail -- also when
     *    the new edit happens to be the very command undone. */
    {
        struct Editor_Loc at = P;
        struct Editor_Cmd first;
        int base;
        at.x = empty_x;
        at.z = empty_z;
        at.level = 0;
        at.rotation = 0;
        lu_step(&lu);
        base = hist->applied;
        n = Editor_LocOpsPlace(&ed->doc, P.loc_id, 10, 0, BASE_X * 64 + empty_x, BASE_Z * 64 + empty_z, 0, &first, 1,
            &result);
        lu_apply(&lu, &first, 1, "place");
        n = Editor_LocOpsPlace(&ed->doc, Q.loc_id, 10, 0, BASE_X * 64 + empty2_x, BASE_Z * 64 + empty2_z, 0, cmds, 1,
            &result);
        lu_apply(&lu, cmds, 1, "place");
        CHECK(hist->applied == base + 2 && hist->count == base + 2, "locs 9: two rows");
        Editor_Undo(ed);
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(hist->applied == base && hist->count == base + 2, "locs 9: both undone, both in the redo tail");
        /* The same place again: a NEW edit, which drops the tail. */
        lu_apply(&lu, &first, 1, "place");
        CHECK(hist->applied == base + 1 && hist->count == base + 1,
            "locs 9: placing the undone loc again is a new row and the redo tail is gone (applied %d count %d, want %d)",
            hist->applied - base, hist->count - base, 1);
        CHECK(Editor_Redo(ed) == 0, "locs 9: the authority has nothing to redo either");
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &at, NULL) == 1, "locs 9: the loc is drawn once");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(lu_world_has(&lu, sq, &at, NULL) == 0, "locs 9: and undone");
    }

    lu_bisect(&lu, "locs: world == STATIC bake before sequence 10");
    /* 10. Undo after Save: the document is dirty again and is the file before. */
    {
        struct Editor_Loc at = P;
        char* before = lu_jl2(sq);
        at.x = empty_x;
        at.z = empty_z;
        at.level = 0;
        at.rotation = 3;
        n = Editor_LocOpsPlace(&ed->doc, P.loc_id, 10, 3, BASE_X * 64 + empty_x, BASE_Z * 64 + empty_z, 0, cmds, 1,
            &result);
        lu_apply(&lu, cmds, n, "place");
        CHECK(Editor_SaveAll(ed) >= 1, "locs 10: save writes the square");
        Editor_PumpFacts(ed);
        CHECK(!sq->dirty_loc, "locs 10: saved, clean");
        Editor_Undo(ed);
        lu_step(&lu);
        CHECK(sq->dirty_loc, "locs 10: the undo after the save dirties it again");
        CHECK(lu_world_has(&lu, sq, &at, NULL) == 0, "locs 10: and takes the loc from the world");
        {
            char* text = lu_jl2(sq);
            CHECK(strcmp(text, before) == 0, "locs 10: the .jl2 is the file before the place");
            free(text);
        }
        free(before);
    }

    /* 11. Delete and undo every 4th loc of m50_50, one step each: each comes
     *     back mid-list, and the world files it there -- a loc filed at the end
     *     instead lights the tiles its shadow shares with a later loc
     *     differently from a fresh bake. */
    {
        char* before = lu_jl2(sq);
        int cycles = 0;
        int const elements_before = m->scene->elements.count;
        lu_bisect(&lu, "locs: world == STATIC bake before sequence 11");
        for( int i = 1; i < sq->loc_count; i += 4 )
        {
            struct Editor_Cmd one;
            Editor_CmdInitSquare(&one, EDITOR_CMD_LOC, BASE_X, BASE_Z);
            one.has_before = 1;
            one.u.loc.before = sq->locs[i];
            if( lu_apply(&lu, &one, 1, "delete loc") != 1 )
                continue;
            Editor_Undo(ed);
            lu_step(&lu);
            cycles++;
        }
        {
            char* text = lu_jl2(sq);
            CHECK(cycles > 100 && strcmp(text, before) == 0,
                "locs 11: %d deletes undone, the .jl2 is the file it was", cycles);
            free(text);
        }
        /* Every removal hands its scene element back: a session of deletes
         * and undos must not climb toward the scene's 65536 (it ran out at
         * ~570 of these cycles, the next add asserting). */
        CHECK(m->scene->elements.count <= elements_before + 64,
            "locs 11: the scene's element high-water stays put (%d -> %d over %d cycles)", elements_before,
            m->scene->elements.count, cycles);
        lu_bisect(&lu, "locs 11: after the deletes undone, world == STATIC bake");
        free(before);
    }
    /* 12. Nudging walls along Lumbridge castle's wall line (the owner's
     *     2026-11-02 report: "moving a loc with the arrow keys sometimes
     *     causes the loc to get stuck"). m50_50 holds castlewall 1911 shape
     *     0 rot 0 on (17,23), (17,24), (17,25), (17,26): a nudge along the
     *     line lands on a wall every time. Through the panel's own action
     *     (edp_locs_nudge, what the arrow keys run), a window's EditorUi. */
    {
        struct EditorUi* ui = calloc(1, sizeof(*ui));
        struct Editor_Loc piece;
        struct Editor_Loc line;
        int pick_shape0 = -1;
        int k;

        assert(ui);
        ui->editor = ed;
        lu_step(&lu);
        line.loc_id = 1911;
        line.shape = 0;
        line.rotation = 0;
        line.level = 0;
        line.x = 17;
        line.z = 24;
        CHECK(Editor_SquareLocFindExact(sq, &line) >= 0, "locs 12: m50_50 has castlewall 1911 on (17,24)");
        /* A different shape-0 loc as the piece that slides (a door in the
         * wall), so a swap shows in the document. */
        for( int i = 0; i < sq->loc_count && pick_shape0 < 0; i++ )
            if( sq->locs[i].shape == 0 && sq->locs[i].loc_id != 1911 && lu_plain(&lu, sq->locs[i].loc_id)
                && lu_world_has(&lu, sq, &sq->locs[i], NULL) == 1 )
                pick_shape0 = sq->locs[i].loc_id;
        CHECK(pick_shape0 >= 0, "locs 12: another shape-0 loc to slide");
        piece.loc_id = pick_shape0;
        piece.shape = 0;
        piece.rotation = 0;
        piece.level = 0;
        piece.x = 18;
        piece.z = 24;
        CHECK(lu_doc_count(sq, 0, 18, 24) == 0 && lu_doc_count(sq, 0, 18, 25) == 0 && lu_doc_count(sq, 0, 19, 25) == 0,
            "locs 12: (18,24), (18,25), (19,25) are free");
        n = Editor_LocOpsPlace(&ed->doc, piece.loc_id, 0, 0, BASE_X * 64 + 18, BASE_Z * 64 + 24, 0, cmds, 1, &result);
        lu_apply(&lu, cmds, n, "place");
        ui->tools.locs[0] = result.refs[0];
        ui->tools.loc_count = 1;

        /* Off the line and back: free tiles, four nudges in a row, the
         * selection naming the loc the document holds after each. */
        {
            static const int steps[4][2] = { { 1, 0 }, { 0, 1 }, { 0, -1 }, { -1, 0 } };
            int x = 18;
            int z = 24;
            for( k = 0; k < 4; k++ )
            {
                struct Editor_Loc at = piece;
                x += steps[k][0];
                z += steps[k][1];
                at.x = x;
                at.z = z;
                CHECK(edp_locs_nudge(ui, steps[k][0], steps[k][1]) == 1, "locs 12: free nudge %d applies", k + 1);
                lu_step(&lu);
                CHECK(Editor_SquareLocFindExact(sq, &at) >= 0, "locs 12: after free nudge %d the document holds the piece at (%d,%d)", k + 1, x, z);
                CHECK(lu_world_has(&lu, sq, &at, NULL) == 1,
                    "locs 12: after free nudge %d the world draws the piece at (%d,%d) once (%d; loc %d)", k + 1, x, z,
                    lu_world_has(&lu, sq, &at, NULL), at.loc_id);
                CHECK(ui->tools.loc_count == 1 && Editor_LocRefResolve(&ed->doc, &ui->tools.locs[0]) >= 0
                          && ui->tools.locs[0].x == x && ui->tools.locs[0].z == z,
                    "locs 12: after free nudge %d the selection names it", k + 1);
            }
        }

        /* Into the line: (17,24) holds a wall. The piece swaps with it, the
         * line stays whole, and the next nudges slide it on along the line. */
        {
            static const int path[4][2] = { { 17, 24 }, { 17, 25 }, { 17, 26 }, { 17, 25 } };
            static const int step[4][2] = { { -1, 0 }, { 0, 1 }, { 0, 1 }, { 0, -1 } };
            for( k = 0; k < 4; k++ )
            {
                struct Editor_Loc at = piece;
                const char* newest;
                int walls = 0;
                at.x = path[k][0];
                at.z = path[k][1];
                CHECK(edp_locs_nudge(ui, step[k][0], step[k][1]) >= 1, "locs 12: line nudge %d applies", k + 1);
                lu_step(&lu);
                newest = Editor_UiConsoleLine(ui, 0);
                CHECK(Editor_SquareLocFindExact(sq, &at) >= 0 && lu_world_has(&lu, sq, &at, NULL) == 1,
                    "locs 12: line nudge %d: the piece is on (%d,%d), drawn once", k + 1, at.x, at.z);
                CHECK(ui->tools.loc_count == 1 && Editor_LocRefResolve(&ed->doc, &ui->tools.locs[0]) >= 0
                          && ui->tools.locs[0].x == at.x && ui->tools.locs[0].z == at.z,
                    "locs 12: line nudge %d: the selection follows it", k + 1);
                CHECK(newest && strstr(newest, "swapped places with loc 1911"),
                    "locs 12: line nudge %d says it swapped (\"%s\")", k + 1, newest ? newest : "");
                for( int z = 23; z <= 26; z++ )
                {
                    int const there = Editor_SquareLocFind(sq, 0, 17, z, 0);
                    CHECK(there >= 0, "locs 12: line nudge %d: (17,%d) still holds a wall (no gap)", k + 1, z);
                    walls += there >= 0 && sq->locs[there].loc_id == 1911;
                }
                /* Entering the line from the side swapped the wall there out
                 * to (18,24), where the piece came from; sliding along the
                 * line swaps neighbours and leaves it be. */
                {
                    int const popped = Editor_SquareLocFind(sq, 0, 18, 24, 0);
                    CHECK(walls == 3 && popped >= 0 && sq->locs[popped].loc_id == 1911,
                        "locs 12: line nudge %d: three castlewalls and the piece on the line, the wall it entered "
                        "on swapped out to (18,24)",
                        k + 1);
                }
            }
        }

        /* Rotate and reshape after the nudges still find the loc. */
        CHECK(edp_locs_rotate(ui) == 1 && Editor_LocRefResolve(&ed->doc, &ui->tools.locs[0]) >= 0,
            "locs 12: a rotate after the nudges applies and the selection still resolves");
        CHECK(Editor_Undo(ed) == 1, "locs 12: undo the rotate");
        lu_step(&lu);
        ui->tools.locs[0].rotation = 0;
        CHECK(Editor_LocRefResolve(&ed->doc, &ui->tools.locs[0]) >= 0, "locs 12: the selection resolves after undo");

        /* A copy onto a held tile+shape has nowhere to put the holder: it
         * stays, the selection stays, and the console says why. */
        ui->tools.locs[0].x = 17;
        ui->tools.locs[0].z = 25;
        {
            int const before_count = sq->loc_count;
            struct Editor_LocRef keep = ui->tools.locs[0];
            const char* newest;
            /* Duplicate goes one tile east: (18,25) is free, so first make it
             * held by nudging a wall... use the line itself: duplicate NORTH
             * is not offered, so duplicate the wall at (17,24) by hand. */
            n = Editor_LocOpsDuplicate(&ed->doc, &keep, 1, 0, 1, cmds, 4, &result);
            CHECK(n == 0 && result.blocked == 1, "locs 12: a copy onto the next wall of the line is blocked");
            CHECK(sq->loc_count == before_count, "locs 12: and changes nothing");
            (void)edp_locs_duplicate(ui);
            newest = Editor_UiConsoleLine(ui, 0);
            CHECK(ui->tools.loc_count == 1, "locs 12: the selection is intact after a duplicate");
            (void)newest;
        }

        /* Two selected walls of the line nudged one tile north: the wall in
         * front goes to the run's tail; every tile keeps one wall. */
        {
            struct Editor_LocRef two[2];
            int const before_count = sq->loc_count;
            Editor_LocRefAt(&two[0], BASE_X * 64 + 17, BASE_Z * 64 + 23, 0, 0, 1911, 0);
            Editor_LocRefAt(&two[1], BASE_X * 64 + 17, BASE_Z * 64 + 24, 0, 0, 1911, 0);
            if( Editor_LocRefResolve(&ed->doc, &two[0]) >= 0 && Editor_LocRefResolve(&ed->doc, &two[1]) >= 0 )
            {
                ui->tools.locs[0] = two[0];
                ui->tools.locs[1] = two[1];
                ui->tools.loc_count = 2;
                CHECK(edp_locs_nudge(ui, 0, 1) >= 2, "locs 12: a two-wall run nudges");
                lu_step(&lu);
                CHECK(sq->loc_count == before_count, "locs 12: no loc gained or lost");
                for( int z = 23; z <= 26; z++ )
                    CHECK(lu_doc_count(sq, 0, 17, z) >= 1 && Editor_SquareLocFind(sq, 0, 17, z, 0) >= 0,
                        "locs 12: after the run nudge (17,%d) holds a wall", z);
                CHECK(ui->tools.loc_count == 2 && ui->tools.locs[0].z == 24 && ui->tools.locs[1].z == 25
                          && Editor_LocRefResolve(&ed->doc, &ui->tools.locs[0]) >= 0
                          && Editor_LocRefResolve(&ed->doc, &ui->tools.locs[1]) >= 0,
                    "locs 12: both stay selected, one tile north");
                CHECK(Editor_Undo(ed) >= 3, "locs 12: one undo takes the run nudge back");
                lu_step(&lu);
            }
        }

        /* The selection relay: this window's own echo never replaces its
         * selection (a multi-selection collapsed to its first loc, and a late
         * echo pointed it at the tile a nudged loc had left). */
        {
            struct Editor_Panel* panel = calloc(1, sizeof(*panel));
            int32_t const a[7] = { 2, 3217, 3224, 0, 1911, 0, 0 };
            int32_t const b[7] = { 2, 3217, 3225, 0, 1911, 0, 0 };
            int32_t const other[7] = { 2, 3230, 3230, 0, 99, 10, 0 };
            assert(panel);
            Editor_PanelSelectionPublished(panel, a);
            Editor_PanelSelectionPublished(panel, b);
            CHECK(!Editor_PanelSelectionHeard(panel, a, 7), "locs 12: the first publish's echo is consumed, not applied");
            CHECK(!Editor_PanelSelectionHeard(panel, other, 7),
                "locs 12: another window's fact heard before this window's newer echo is superseded");
            CHECK(!Editor_PanelSelectionHeard(panel, b, 7), "locs 12: the second echo is consumed");
            CHECK(Editor_PanelSelectionHeard(panel, other, 7),
                "locs 12: with no echo pending, another window's selection applies");
            free(panel);
        }

        lu_bisect(&lu, "locs 12: after the nudges, world == STATIC bake");
        free(ui);
    }
    /* 13. A loc's shapes (the owner, 2026-11-02: "improve the experience for
     *     locs with multiple shapes, like walls"; "it shouldn't let me select
     *     a shape that a loc doesn't have"). castlewall 1911 has models for
     *     shapes 0, 1, 2, 3 and 9 only. */
    {
        struct EditorUi* ui = calloc(1, sizeof(*ui));
        struct ToriRS_Location* wall = CacheProvider_LocationGet(h->provider, 1911);
        int list[EDITOR_LOC_SHAPE_COUNT];
        uint32_t mask = 0;
        int count;
        int bx = -1;
        int bz = -1;

        assert(ui);
        ui->editor = ed;
        ui->app_services.user_data = h->provider;
        ui->app_services.loc_record = lu_loc_record;
        CHECK(wall != NULL, "shapes: castlewall 1911 is resident");
        if( !wall )
        {
            free(ui);
            return;
        }
        mask = Editor_LocShapeMask(wall);
        count = Editor_LocShapeList(mask, list);
        CHECK(mask == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 9)),
            "shapes: castlewall's shapes are 0, 1, 2, 3, 9 (mask %x)", mask);
        CHECK(count == 5 && list[0] == 0 && list[4] == 9, "shapes: listed wall first, diagonal last");
        /* A synthetic loc with three shapes, and the derived ones. */
        {
            struct ToriRS_Location fake;
            int shapes3[3] = { 0, 4, 10 };
            int lengths3[3] = { 1, 1, 1 };
            int m0[1] = { 1 };
            int* models3[3] = { m0, m0, m0 };
            uint32_t m3;
            memset(&fake, 0, sizeof(fake));
            fake.shapes = shapes3;
            fake.models = models3;
            fake.lengths = lengths3;
            fake.shapes_and_model_count = 3;
            m3 = Editor_LocShapeMask(&fake);
            CHECK(m3 == ((1u << 0) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 10) | (1u << 11)),
                "shapes: a loc with models for 0, 4 and 10 offers 0, the decor 4..8 and 10, 11 (%x)", m3);
            CHECK(Editor_LocShapeNext(m3, 10) == 11 && Editor_LocShapeNext(m3, 11) == 0 && Editor_LocShapeNext(m3, 8) == 10,
                "shapes: cycling stays within them and wraps");
            CHECK(Editor_LocShapeNext(m3, 22) == 10, "shapes: from a shape it lacks, cycling goes to its first");
            fake.shapes = NULL;
            CHECK(Editor_LocShapeMask(&fake) == ((1u << 10) | (1u << 11)),
                "shapes: no shape list is a centrepiece only (10, 11)");
        }
        CHECK(Editor_LocShapeNext(mask, 0) == 1 && Editor_LocShapeNext(mask, 3) == 9 && Editor_LocShapeNext(mask, 9) == 0,
            "shapes: castlewall cycles 0 -> 1 ... 3 -> 9 -> 0");

        /* The Place pose snaps to a shape the pick has; a place naming one
         * it lacks is refused and says so. */
        ui->tools.pick_kind = 0;
        ui->tools.pick_id = 1911;
        ui->tools.place_shape = 10;
        CHECK(edp_place_pose_follow(ui) == 1 && ui->tools.place_shape == 0,
            "shapes: picking castlewall with the pose on 10 snaps the pose to its first shape, 0");
        edp_place_reshape(ui);
        CHECK(ui->tools.place_shape == 1, "shapes: Shift+Q on the pose goes to castlewall's next shape");
        for( int k = 0; k < 4; k++ )
            edp_place_reshape(ui);
        CHECK(ui->tools.place_shape == 0, "shapes: five presses come back round (never a shape it lacks)");

        /* A free 5x5 (no loc on level 0) in one of the open squares. */
        for( int q = 0; q < SQUARE_COUNT && bx < 0; q++ )
        {
            struct Editor_Square* s2 = Editor_DocFindSquare(&ed->doc, k_squares[q][0], k_squares[q][1]);
            for( int z = 4; z < 56 && bx < 0; z++ )
                for( int x = 4; x < 56 && bx < 0; x++ )
                {
                    int empty = 1;
                    for( int dz = 0; dz < 5 && empty; dz++ )
                        for( int dx = 0; dx < 5 && empty; dx++ )
                            empty = lu_doc_count(s2, 0, x + dx, z + dz) == 0;
                    if( empty )
                    {
                        sq = s2;
                        bx = x;
                        bz = z;
                    }
                }
        }
        CHECK(bx >= 0, "shapes: a free 5x5 in the open squares");
        if( bx >= 0 )
        {
            int const wx = sq->map_x * 64 + bx;
            int const wz = sq->map_z * 64 + bz;
            int shape;
            int rotation;
            int before_count = sq->loc_count;
            const char* newest;

            CHECK(edp_locs_place(ui, 1911, 10, 0, wx, wz, 0) == 0 && sq->loc_count == before_count,
                "shapes: placing castlewall as shape 10 is refused");
            newest = Editor_UiConsoleLine(ui, 0);
            CHECK(newest && strstr(newest, "has no model for shape 10"), "shapes: and the console says why (\"%s\")",
                newest ? newest : "");

            /* Fit to neighbours: a straight run on the west edges of (bx, bz)
             * and (bx, bz+1); the next tile north continues it. */
            CHECK(edp_locs_place(ui, 1911, 0, 0, wx, wz, 0) == 1 && edp_locs_place(ui, 1911, 0, 0, wx, wz + 1, 0) == 1,
                "fit: two castlewalls on a west-edge run");
            ui->tools.place_shape = 3;
            ui->tools.place_rotation = 2;
            edp_place_fit_set(1);
            edp_place_pose_at(ui, wx, wz + 2, 0, &shape, &rotation);
            CHECK(shape == 0 && rotation == 0, "fit: the tile north of the run fits a straight wall on its west edge (%d rot %d)",
                shape, rotation);
            /* A run along the north edges going east from (bx+1, bz+3): the
             * tile (bx, bz+2) between the two runs is their corner. */
            CHECK(edp_locs_place(ui, 1911, 0, 1, wx + 1, wz + 2, 0) == 1, "fit: a castlewall on a north-edge run");
            edp_place_pose_at(ui, wx, wz + 2, 0, &shape, &rotation);
            CHECK(shape == 2 && rotation == 0, "fit: where the west run meets the north run, an L (2 rot 0) (%d rot %d)",
                shape, rotation);
            {
                struct Editor_Loc want = { 1911, 2, 0, 0, bx, bz + 2 };
                CHECK(edp_locs_place(ui, 1911, shape, rotation, wx, wz + 2, 0) == 1
                          && Editor_SquareLocFindExact(sq, &want) >= 0,
                    "fit: placing with the fitted pose puts the L in the document");
                lu_step(&lu);
                {
                    int sx;
                    int sz;
                    lu_scene_of(&lu, sq, want.x, want.z, &sx, &sz);
                    /* An L is two wall models on the tile, one entity each. */
                    CHECK(lu_world_count(&lu, sx, sz, 0, 1911, 2, 0, NULL) >= 1,
                        "fit: and the world draws it (%d entities of 1911 shape 2 there; %d on the tile)",
                        lu_world_count(&lu, sx, sz, 0, 1911, 2, 0, NULL), lu_world_count(&lu, sx, sz, 0, -1, 0, 0, NULL));
                }
            }
            /* A diagonal continues a diagonal neighbour. */
            CHECK(edp_locs_place(ui, 1911, 9, 3, wx + 3, wz + 1, 0) == 1, "fit: a diagonal castlewall");
            ui->tools.place_shape = 0;
            edp_place_pose_at(ui, wx + 4, wz, 0, &shape, &rotation);
            CHECK(shape == 9 && rotation == 3, "fit: the tile south-east of it continues the diagonal (%d rot %d)", shape,
                rotation);
            /* With fit off, the pose as set. */
            edp_place_fit_set(0);
            edp_place_pose_at(ui, wx + 4, wz, 0, &shape, &rotation);
            CHECK(shape == 0, "fit: off, the pose is the pose");
            edp_place_fit_set(1);

            /* Reshape of a selection cycles the loc's own shapes; Fit fixes a
             * wrong one. */
            {
                struct Editor_LocRef ref;
                Editor_LocRefAt(&ref, wx, wz + 1, 0, 0, 1911, 0);
                ui->tools.locs[0] = ref;
                ui->tools.loc_count = 1;
                CHECK(edp_locs_reshape(ui) == 1 && ui->tools.locs[0].shape == 1,
                    "shapes: Reshape takes the selected castlewall from 0 to 1, its next shape");
                CHECK(edp_locs_set_shape(ui, 10) == 0 && ui->tools.locs[0].shape == 1,
                    "shapes: the strip cannot give it shape 10");
                CHECK(edp_locs_set_shape(ui, 9) == 1 && ui->tools.locs[0].shape == 9, "shapes: the strip gives it 9");
                CHECK(edp_locs_fit(ui) == 1 && ui->tools.locs[0].shape == 0 && ui->tools.locs[0].rotation == 0,
                    "fit: Fit selection puts it back as the straight wall of its run (%d rot %d)", ui->tools.locs[0].shape,
                    ui->tools.locs[0].rotation);
                lu_step(&lu);
            }
            lu_bisect(&lu, "shapes: after the walls, world == STATIC bake");
        }
        free(ui);
    }
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
    /* An empty upper floor (the Level row pinned on 2 or 3 over open ground):
     * a 3x3 of level-2 tiles the file states nothing for takes an underlay
     * and a height, a level-3 tile a height, and a loc lands on level 2 --
     * all through the local flush, no rebake; the STATIC rebuild below must
     * agree with what the flush built. */
    {
        int cx = -1;
        int cz = -1;
        for( int z = 20; z < 108 && cx < 0; z++ )
            for( int x = 20; x < 108 && cx < 0; x++ )
            {
                int empty = 1;
                for( int dz = -1; dz <= 1; dz++ )
                    for( int dx = -1; dx <= 1; dx++ )
                    {
                        struct Editor_Square* sq = mirror_square(ed, x + dx, z + dz);
                        const struct Editor_Tile* t;
                        if( !sq )
                        {
                            empty = 0;
                            continue;
                        }
                        t = &sq->tiles[Editor_TileIndex((x + dx) % 64, (z + dz) % 64, 2)];
                        if( t->has_height || t->has_overlay || t->underlay_id || t->settings )
                            empty = 0;
                        t = &sq->tiles[Editor_TileIndex((x + dx) % 64, (z + dz) % 64, 3)];
                        if( t->has_height || t->has_overlay || t->underlay_id || t->settings )
                            empty = 0;
                    }
                if( empty && (x % 64) > 0 && (x % 64) < 63 && (z % 64) > 0 && (z % 64) < 63 )
                {
                    cx = x;
                    cz = z;
                }
            }
        CHECK(cx >= 0, "mirror: an empty 3x3 of levels 2 and 3 to edit");
        if( cx >= 0 )
        {
            struct Editor_Square* sq = mirror_square(ed, cx, cz);
            int loc_found = 0;
            Editor_StrokeBegin(ed);
            for( int dz = -1; dz <= 1; dz++ )
                for( int dx = -1; dx <= 1; dx++ )
                {
                    mirror_tile(ed, cx + dx, cz + dz, 2, 1, pools.underlays[0], 0, 0);
                    mirror_tile(ed, cx + dx, cz + dz, 2, 0, 20, 0, 0);
                }
            mirror_tile(ed, cx, cz, 3, 0, 30, 0, 0);
            Editor_StrokeEnd(ed);
            {
                const struct Editor_Tile* t = &sq->tiles[Editor_TileIndex(cx % 64, cz % 64, 2)];
                CHECK(t->underlay_id == pools.underlays[0] && t->has_height,
                    "mirror: the empty level-2 tile took the underlay and the height");
            }
            Editor_PumpFacts(ed);
            CHECK(!app_map_editor_edit_step(ed, h->provider, m.builder, m.world, m.scene),
                "mirror: tiles painted on an empty upper floor flush locally (no rebake)");
            world_drain(&m);
            Editor_StrokeBegin(ed);
            {
                struct Editor_Cmd cmd;
                struct Editor_Loc loc = pools.locs[0];
                loc.x = cx % 64;
                loc.z = cz % 64;
                loc.level = 2;
                loc.rotation = 0;
                Editor_CmdInitSquare(&cmd, EDITOR_CMD_LOC, sq->map_x, sq->map_z);
                cmd.has_after = 1;
                cmd.u.loc.after = loc;
                applied += Editor_Apply(ed, &cmd);
            }
            Editor_StrokeEnd(ed);
            for( int i = 0; i < sq->loc_count; i++ )
                if( sq->locs[i].level == 2 && sq->locs[i].x == cx % 64 && sq->locs[i].z == cz % 64 )
                    loc_found = 1;
            CHECK(loc_found, "mirror: the loc document holds the loc at level 2");
            /* A loc the world lacks assets for rebakes, as anywhere. */
            Editor_PumpFacts(ed);
            if( app_map_editor_edit_step(ed, h->provider, m.builder, m.world, m.scene) )
            {
                world_load(h, &m);
                rebakes++;
            }
            world_drain(&m);
        }
    }
    loc_undo_phase(h, ed, &m, &pools, &rebakes);
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
