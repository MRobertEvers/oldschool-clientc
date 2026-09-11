/*
 * wev_rebuild_test — SAILING_PLAN C2 gate: REBUILD_WORLDENTITY_V4 (op 109).
 *
 * Three seams, hand-fed:
 *   1. The parse arm's raw-carry: header (baseX, baseZ, source-square count)
 *      validated, the bit-packed zone grid carried opaque because its
 *      dimensions are spawn-time view state the wire omits.
 *   2. PktRebuildWev_DecodeZones against hand-laid bitstreams: the 1+26-bit
 *      cell layout, the 13x13 REBUILD_REGION stride, and whole-stream
 *      rejection when the bit count disagrees with the view size.
 *   3. The zone-template rebuild itself: a 1-zone raft deck sourced from
 *      Lumbridge castle (zone 402,402 in map square 50,50) built through
 *      CreateTask_WorldLoad into a BOAT world that shares the root's
 *      ToriDraw scene — asserting the tiles and locs land in the boat world,
 *      the root world stays untouched, and the boat's heights match a real
 *      root-world Lumbridge rebuild tile for tile. Cache-backed, loud SKIP
 *      when ../cache.osrs239 is absent.
 *   4. The per-view element pools that make (3) survivable: with two worlds in
 *      ONE scene, a rebuild is a pool clear plus a dynamic-orphan sweep, and
 *      both used to be scene-global. So the same fixture rebuilds each view
 *      with the other already built and asserts neither one's elements move —
 *      then clears the boat's static pool the way App_WevDespawn does and
 *      asserts only the boat's elements go.
 *
 *   5. The ROOT half of the same drain rule: an EntityRemoved queued on the
 *      root world (a despawn that arrived in the same packet pump as the
 *      rebuild) is drained — elements freed, not dropped — before
 *      WorldBuilder_RebuildCenterzoneBegin resets the scene allocation. Needs
 *      no cache; never skips.
 *
 *   make -C src test-wev-rebuild    (runs from src/, cache at ../cache.osrs239)
 */
#include "app.h"
#include "asyncio.h"
#include "task_runner.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "platform/platform_x_io.h"
#include "varp/varp_manager.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <rscache.h>
#include <toridraw.h>
#include <toridraw_scene.h>

/* assert() with the sentence attached (same shape as wev_test): aborts on the
 * first failure, because a half-run gate proves nothing. */
#define TEST_WEVR_ASSERT(cond, msg)                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

/* The raft parks over unused map at zone 800 (tile 6400 — nothing real lives
 * there), decked from Lumbridge castle's east zone: absolute zone (402,402),
 * tiles 3216..3223, inside map square (50,50). Castle walls guarantee locs. */
#define RAFT_BASE_TILE 6400
#define RAFT_SRC_ZONE 402

/* The worldview id the raft is registered under. Anything but WORLDVIEW_ROOT:
 * the point of the pool checks is that view 0 and view N do not collide. */
#define RAFT_VIEW_ID 1

/* ------------------------------------------------------------------ */
/* MSB-first bit writer (mirror of the wire's gbits reader)            */
/* ------------------------------------------------------------------ */

static void
put_bits(
    uint8_t* buf,
    int* bit_pos,
    uint32_t value,
    int count)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        if( (value >> i) & 1 )
            buf[*bit_pos >> 3] |= (uint8_t)(0x80 >> (*bit_pos & 7));
        (*bit_pos)++;
    }
}

/* Zone descriptor as the deob packs it: bit 0 unused, rotation<<1,
 * src_zone_z<<3 (11 bits), src_zone_x<<14 (10 bits), src_level<<24. */
static uint32_t
zone_descriptor(
    int src_zone_x,
    int src_zone_z,
    int src_level,
    int rotation)
{
    return ((uint32_t)src_level << 24) | ((uint32_t)src_zone_x << 14) |
           ((uint32_t)src_zone_z << 3) | ((uint32_t)rotation << 1);
}

/* The full op-109 body for a 1x1-zone (8x8-tile) view: level 0's single cell
 * present with `v`, levels 1..3 void. 6-byte header + 30 grid bits = 10 bytes.
 * `buf` must hold at least 10 zeroed bytes; returns the byte length. */
static int
build_raft_packet(
    uint8_t* buf,
    uint32_t v)
{
    int bit_pos = 8 * 6;

    buf[0] = (RAFT_BASE_TILE >> 8) & 0xFF; /* baseX u16 be */
    buf[1] = RAFT_BASE_TILE & 0xFF;
    buf[2] = (RAFT_BASE_TILE >> 8) & 0xFF; /* baseZ */
    buf[3] = RAFT_BASE_TILE & 0xFF;
    buf[4] = 0x00; /* distinct source squares: 1 */
    buf[5] = 0x01;

    put_bits(buf, &bit_pos, 1, 1); /* level 0, zone (0,0): present */
    put_bits(buf, &bit_pos, v, 26);
    put_bits(buf, &bit_pos, 0, 1); /* levels 1..3: void */
    put_bits(buf, &bit_pos, 0, 1);
    put_bits(buf, &bit_pos, 0, 1);
    return (bit_pos + 7) / 8;
}

/* ------------------------------------------------------------------ */
/* Parse arm (raw carry) + grid decode                                 */
/* ------------------------------------------------------------------ */

static void
test_parse_and_decode(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct RevPacket p;
    uint8_t buf[16];
    int len;
    uint32_t v = zone_descriptor(RAFT_SRC_ZONE, RAFT_SRC_ZONE, 0, 0);
    int32_t zones[PKT_MAP_REBUILD_ZONES];

    memset(buf, 0, sizeof(buf));
    len = build_raft_packet(buf, v);
    TEST_WEVR_ASSERT(len == 10, "1x1 grid packs to 4 grid bytes");

    memset(&p, 0, sizeof(p));
    TEST_WEVR_ASSERT(
        rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, buf, len, &p), "raft packet parses");
    /* The parse arms leave packet_type to the reader (net.c stamps it after a
     * successful parse); gameproto_free dispatches on it, so stamp it here the
     * same way. */
    p.packet_type = PKT_NAME_REBUILD_WORLDENTITY;
    TEST_WEVR_ASSERT(p._rebuild_wev.base_x == RAFT_BASE_TILE, "baseX carried");
    TEST_WEVR_ASSERT(p._rebuild_wev.base_z == RAFT_BASE_TILE, "baseZ carried");
    TEST_WEVR_ASSERT(p._rebuild_wev.length == 4, "grid carried raw, header stripped");
    TEST_WEVR_ASSERT(p._rebuild_wev.data != NULL, "grid bytes copied out of the frame");

    /* Decode against the true view size: the one cell lands at index 0 of the
     * 13-stride grid, every other cell (incl. all of levels 1..3) is void. */
    TEST_WEVR_ASSERT(
        PktRebuildWev_DecodeZones(&p._rebuild_wev, 1, 1, zones), "grid decodes at 1x1 zones");
    TEST_WEVR_ASSERT(zones[0] == (int32_t)v, "descriptor lands at [0]");
    for( int i = 1; i < PKT_MAP_REBUILD_ZONES; i++ )
        TEST_WEVR_ASSERT(zones[i] == 0, "every other cell stays void");

    /* The wrong view size must reject the whole stream, both directions:
     * 2x1 zones wants 34 bits from a 30-bit grid (short), and a 4-byte grid
     * read as 1x1 with a 5th byte appended has a leftover byte (long). */
    TEST_WEVR_ASSERT(
        !PktRebuildWev_DecodeZones(&p._rebuild_wev, 2, 1, zones),
        "bigger view than the stream rejected");
    gameproto_free(&p);
    TEST_WEVR_ASSERT(p._rebuild_wev.data == NULL, "gameproto_free clears the carry");

    memset(&p, 0, sizeof(p));
    TEST_WEVR_ASSERT(
        rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, buf, len - 1, &p),
        "parse cannot size-check the grid (dims are spawn-time state)");
    p.packet_type = PKT_NAME_REBUILD_WORLDENTITY;
    TEST_WEVR_ASSERT(p._rebuild_wev.length == 3, "one grid byte lost");
    TEST_WEVR_ASSERT(
        !PktRebuildWev_DecodeZones(&p._rebuild_wev, 1, 1, zones), "short grid rejected at decode");
    gameproto_free(&p);

    {
        uint8_t padded[16];

        memcpy(padded, buf, sizeof(buf));
        memset(&p, 0, sizeof(p));
        TEST_WEVR_ASSERT(
            rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, padded, len + 1, &p),
            "padded frame still parses");
        p.packet_type = PKT_NAME_REBUILD_WORLDENTITY;
        TEST_WEVR_ASSERT(
            !PktRebuildWev_DecodeZones(&p._rebuild_wev, 1, 1, zones),
            "trailing grid byte rejected at decode");
        gameproto_free(&p);
    }

    /* Header truncation and an impossible source-square count fail at parse. */
    memset(&p, 0, sizeof(p));
    TEST_WEVR_ASSERT(
        !rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, buf, 6, &p), "empty grid rejected");
    memset(&p, 0, sizeof(p));
    TEST_WEVR_ASSERT(
        !rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, buf, 5, &p), "short header rejected");
    {
        uint8_t bad[16];

        memcpy(bad, buf, sizeof(buf));
        bad[4] = (uint8_t)((PKT_MAP_REBUILD_ZONES + 1) >> 8);
        bad[5] = (uint8_t)((PKT_MAP_REBUILD_ZONES + 1) & 0xFF);
        memset(&p, 0, sizeof(p));
        TEST_WEVR_ASSERT(
            !rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, bad, len, &p),
            "source-square count past the grid rejected");
    }

    /* Stride check on a wider view: 2x1 zones, only cell (zx=1, zz=0) present
     * — it must land at [1*13 + 0], not [1]. Grid built by hand, no header. */
    {
        struct PktRebuildWev wide;
        uint8_t grid[8];
        int bit_pos = 0;

        memset(grid, 0, sizeof(grid));
        put_bits(grid, &bit_pos, 0, 1); /* level 0, (0,0): void */
        put_bits(grid, &bit_pos, 1, 1); /* level 0, (1,0): present */
        put_bits(grid, &bit_pos, v, 26);
        for( int i = 0; i < 6; i++ ) /* levels 1..3, both zones: void */
            put_bits(grid, &bit_pos, 0, 1);

        memset(&wide, 0, sizeof(wide));
        wide.data = grid;
        wide.length = (bit_pos + 7) / 8;
        TEST_WEVR_ASSERT(PktRebuildWev_DecodeZones(&wide, 2, 1, zones), "2x1 grid decodes");
        TEST_WEVR_ASSERT(zones[13] == (int32_t)v, "zx=1 lands at the 13-stride cell");
        TEST_WEVR_ASSERT(zones[0] == 0, "zx=0 stays void");
        TEST_WEVR_ASSERT(zones[1] == 0, "the naive 2-stride cell stays void");
    }

    printf("ok - REBUILD_WORLDENTITY parse carry + view-sized grid decode\n");
}

/* ------------------------------------------------------------------ */
/* The pool tag space every view has to fit into                       */
/* ------------------------------------------------------------------ */

static void
test_pool_view_tags(void)
{
    /* A scene element carries its pool in one uint8_t, so the view stride is
     * what caps the number of views that can share a scene at all. Every id
     * worldview.h can hand out must have a pair inside that byte. */
    TEST_WEVR_ASSERT(
        WORLDVIEW_MAX <= TORIDRAW_SCENE_POOL_VIEW_MAX, "every worldview id has a pool pair");
    TEST_WEVR_ASSERT(
        TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(WORLDVIEW_MAX - 1) < 256, "the last pair fits in the tag");

    /* The root's pair IS the historic {STATIC, DYNAMIC}, which is the whole
     * reason a single-world client is untouched by the split. */
    TEST_WEVR_ASSERT(
        TORIDRAW_SCENE_POOL_STATIC_VIEW(WORLDVIEW_ROOT) == TORIDRAW_SCENE_POOL_STATIC,
        "view 0's static pool is the historic STATIC");
    TEST_WEVR_ASSERT(
        TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(WORLDVIEW_ROOT) == TORIDRAW_SCENE_POOL_DYNAMIC,
        "view 0's dynamic pool is the historic DYNAMIC");

    /* Distinctness, both within a view and across views — an overlap anywhere
     * here is one view's rebuild freeing another's elements. */
    for( int a = 0; a < WORLDVIEW_MAX; a++ )
    {
        TEST_WEVR_ASSERT(
            TORIDRAW_SCENE_POOL_STATIC_VIEW(a) != TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(a),
            "a view's own two pools differ");
        for( int b = a + 1; b < WORLDVIEW_MAX; b++ )
        {
            TEST_WEVR_ASSERT(
                TORIDRAW_SCENE_POOL_STATIC_VIEW(a) != TORIDRAW_SCENE_POOL_STATIC_VIEW(b),
                "static pools are unique per view");
            TEST_WEVR_ASSERT(
                TORIDRAW_SCENE_POOL_STATIC_VIEW(a) != TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(b),
                "no view's static pool is another's dynamic pool");
            TEST_WEVR_ASSERT(
                TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(a) != TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(b),
                "dynamic pools are unique per view");
        }
    }

    printf("ok - per-view scene pools are distinct and fit the element pool tag\n");
}

/* ------------------------------------------------------------------ */
/* Handing an actor's element between view pools (SAILING_PLAN C5.1)   */
/* ------------------------------------------------------------------ */

/*
 * An actor boarding a boat does not get a new scene element — its id is what
 * the entity record, the painter's scenery chains and the plugin-facing
 * EntityRemoved queue all hold. What changes is which view's sweep owns it,
 * and that is one byte: ToriDraw_SceneElementSetPool.
 *
 * Three things have to hold or a boarding leaks or strands:
 *   - after the move the OLD view's clear no longer reaches the element,
 *   - the NEW view's clear does,
 *   - and moving it back before that clear (which is what App_WevDespawn's
 *     eviction does) saves it, with the element count proving nothing was
 *     duplicated along the way.
 * No cache: this is pure pool bookkeeping.
 */
static void
test_actor_pool_handoff(void)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    int const boat_view = 3;
    int const boat_pool = TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(boat_view);
    int actor;
    int bystander;

    TEST_WEVR_ASSERT(scene != NULL, "scene allocates");

    /* Ashore: every actor's element starts in the root's dynamic half. */
    actor = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    bystander = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_WEVR_ASSERT(actor >= 0 && bystander >= 0, "two root dynamic elements allocate");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementPool(scene, actor) == TORIDRAW_SCENE_POOL_DYNAMIC,
        "an actor ashore is tagged with the root's dynamic pool");

    /* Boarding. */
    ToriDraw_SceneElementSetPool(scene, actor, boat_pool);
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementPool(scene, actor) == boat_pool,
        "boarding retags the element into the boat's dynamic pool");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, actor), "the element survives the retag");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementPool(scene, bystander) == TORIDRAW_SCENE_POOL_DYNAMIC,
        "the actor beside them did not move");

    /* The old pool's sweep must no longer reach it -- this is the half that
     * would otherwise delete a deck player on the mainland's next rebuild. */
    ToriDraw_SceneClearPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, actor),
        "the root's dynamic clear no longer reaches an aboard actor");
    TEST_WEVR_ASSERT(
        !ToriDraw_SceneElementIsLive(scene, bystander),
        "the root's dynamic clear still reaches an actor ashore");

    /* Disembarking (or App_WevDespawn evicting them) puts it back, and the
     * boat's clear then leaves it alone -- no element stranded in a pool that
     * is about to be freed. */
    ToriDraw_SceneElementSetPool(scene, actor, TORIDRAW_SCENE_POOL_DYNAMIC);
    ToriDraw_SceneClearPool(scene, boat_pool);
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, actor),
        "an evicted actor survives the sinking boat's pool clear");

    /* And the counterfactual: had they still been aboard, that same clear is
     * what would have taken them. */
    {
        int drowned = ToriDraw_SceneElementAddPool(scene, boat_pool);
        TEST_WEVR_ASSERT(drowned >= 0, "a boat-pool element allocates");
        ToriDraw_SceneClearPool(scene, boat_pool);
        TEST_WEVR_ASSERT(
            !ToriDraw_SceneElementIsLive(scene, drowned),
            "the boat's clear does reach what is still tagged to it");
    }

    /* No leak: everything allocated here is either freed by a clear above or
     * still reachable, so one final clear empties the scene. */
    ToriDraw_SceneElementSetPool(scene, actor, TORIDRAW_SCENE_POOL_DYNAMIC);
    ToriDraw_SceneClearPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_WEVR_ASSERT(
        !ToriDraw_SceneElementIsLive(scene, actor), "the last clear empties the scene");

    ToriDraw_SceneFree(scene);
    printf("ok - an actor's element moves between view pools without leaking or stranding\n");
}

/* ------------------------------------------------------------------ */
/* The raft-deck rebuild against the real cache                        */
/* ------------------------------------------------------------------ */

/* Which element ids a pool holds, taken before an operation that must not
 * touch it. Heap-backed because the root's static pool is a 104-tile scene:
 * tens of thousands of ids, far past anything worth putting on the stack. */
struct PoolSnapshot
{
    int* ids;
    int count;
    int pool;
};

static void
pool_snapshot_take(
    struct PoolSnapshot* snap,
    struct ToriDraw_Scene* scene,
    int pool)
{
    assert(snap);
    assert(scene);
    assert(pool >= 0);

    snap->ids = (int*)malloc((size_t)TORIDRAW_SCENE_MAX_ELEMENTS * sizeof(int));
    assert(snap->ids);
    snap->count = 0;
    snap->pool = pool;
    for( int id = 0; id < ToriDraw_SceneElementSlotCount(scene); id++ )
    {
        struct ToriDraw_SceneElement* el;

        if( !ToriDraw_SceneElementIsLive(scene, id) )
            continue;
        el = ToriDraw_SceneElementGet(scene, id);
        if( !el || el->pool != (uint8_t)pool )
            continue;
        snap->ids[snap->count++] = id;
    }
}

static void
pool_snapshot_free(struct PoolSnapshot* snap)
{
    if( !snap )
        return;
    free(snap->ids);
    snap->ids = NULL;
    snap->count = 0;
}

/* Every id still live and still tagged with the pool it was taken from. A
 * recycled id would read as live while belonging to somebody else, so the tag
 * is checked too — that is exactly the corruption the split exists to stop. */
static void
pool_snapshot_assert_intact(
    const struct PoolSnapshot* snap,
    struct ToriDraw_Scene* scene,
    char const* msg)
{
    assert(snap);
    assert(snap->ids);
    assert(scene);

    for( int i = 0; i < snap->count; i++ )
    {
        struct ToriDraw_SceneElement* el;

        TEST_WEVR_ASSERT(ToriDraw_SceneElementIsLive(scene, snap->ids[i]), msg);
        el = ToriDraw_SceneElementGet(scene, snap->ids[i]);
        TEST_WEVR_ASSERT(el && el->pool == (uint8_t)snap->pool, msg);
    }
}

static void
pool_snapshot_assert_gone(
    const struct PoolSnapshot* snap,
    struct ToriDraw_Scene* scene,
    char const* msg)
{
    assert(snap);
    assert(snap->ids);
    assert(scene);

    for( int i = 0; i < snap->count; i++ )
    {
        struct ToriDraw_SceneElement* el;

        if( !ToriDraw_SceneElementIsLive(scene, snap->ids[i]) )
            continue;
        /* Live is only acceptable if the id was recycled INTO another pool;
         * still carrying this pool's tag means the clear missed it. */
        el = ToriDraw_SceneElementGet(scene, snap->ids[i]);
        TEST_WEVR_ASSERT(el && el->pool != (uint8_t)snap->pool, msg);
    }
}

/*
 * Drain one task to completion.
 *
 * Through a TaskRunner rather than by stepping the queue directly: the runner
 * is what hands a task its IO slot, and a world load now fans its assets out
 * as sibling tasks that need slots of their own.
 */
static void
run_task(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct PlatformX_IO* px,
    struct ToriRS_Task* task)
{
    struct TaskRunner runner;

    assert(task);
    memset(&runner, 0, sizeof(runner));
    runner.queue = queue;
    runner.io = io;
    runner.px = px;
    /* Asset loads, like the client's own asset runner: a rebuild fans its
     * models and textures out as siblings and they have to be allowed to run. */
    runner.parallel = 1;
    ToriRS_TaskQueue_Add(queue, task);
    TaskRunner_Drain(&runner);
}

/*
 * The ROOT world's EntityRemoved queue is drained before its rebuild resets
 * the scene allocation — the mainland half of the C2 drain rule the boat
 * branch above already obeys.
 *
 * The bug this pins: a despawn that lands in the same packet pump as a
 * REBUILD_NORMAL (a boat sinking one packet before the teleport that follows
 * it) queues an EntityRemoved on the root world that no tick has drained yet,
 * because the per-tick drain sits behind the world gate a rebuild has already
 * closed. The rebuild then reached World_ResetSceneAlloc with a full queue and
 * a debug client aborted on the spot; a release client silently dropped the
 * queue instead, orphaning the DYNAMIC scene elements it named.
 *
 * WorldBuilder_RebuildCenterzoneBegin is the exact frame the abort came from
 * (world_builder.c -> World_ResetScene -> World_ResetSceneAlloc), and it reads
 * nothing out of the provider, so this needs no cache and never skips. The
 * drain is the production one, App_WorldDrainEntityRemovedFor: what makes it a
 * drain rather than a queue wipe is that it hands the element to the plugins
 * and frees it (ToriDraw_SceneElementRemove) before the reset makes its id
 * unreachable, and that release is asserted here.
 */
static void
test_root_rebuild_drains_entity_removed(void)
{
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct ToriDraw_Scene* scene;
    struct VarPManager varp;
    struct World* world;
    struct WorldBuilder* builder;
    struct App* app;
    struct WorldEntityFacet_IdleAnimations idle;
    struct World_Event const* removed;
    int element;
    int npc_index;

    memset(&idle, 0, sizeof(idle));
    ToriDraw_Init();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    TEST_WEVR_ASSERT(scene != NULL, "scene allocates");
    VarPManager_Init(&varp);
    world = World_New();
    World_SetScene(world, scene);
    builder = WorldBuilder_New(world, provider, scene, &varp);
    TEST_WEVR_ASSERT(builder != NULL, "root builder allocates");

    /* The scene the player was standing in when the despawn arrived. */
    WorldBuilder_RebuildCenterzoneBegin(builder, 50, 50, 64);
    TEST_WEVR_ASSERT(world->heightmap != NULL, "the first root rebuild allocated the scene");

    /* An npc with a live DYNAMIC element, despawned the way a wire despawn
     * despawns it: the pool slot goes immediately, the render-side removal is
     * queued with an immutable copy of the npc for the drain to hand on. */
    element = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_WEVR_ASSERT(element >= 0, "the root's dynamic pool allocates");
    npc_index = World_NpcSpawn(world, element, 123, 0, 10, 10, 1, idle);
    TEST_WEVR_ASSERT(npc_index >= 0, "npc spawns into the root world");
    World_NpcDespawn(world, npc_index);

    /* The precondition that used to reach the reset. */
    TEST_WEVR_ASSERT(
        World_EventsCount(world) == 1, "the despawn queued one EntityRemoved on the root world");
    removed = World_EventsPeek(world, 0);
    TEST_WEVR_ASSERT(removed != NULL, "the queued event is readable");
    TEST_WEVR_ASSERT(
        removed->kind == WorldEventKind_EntityRemoved, "the queued event is an EntityRemoved");
    TEST_WEVR_ASSERT(removed->element_id == element, "the event names the npc's scene element");
    TEST_WEVR_ASSERT(
        removed->removed_npc != NULL, "the event carries the despawned npc for delivery");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, element),
        "the element is still live before the drain");

    /* Heap, not stack: struct App is large (same reason rs_gameproto_exec_test
     * calloc's it). Only the scene is wired — a NULL plugin host is the
     * no-plugins client, and the drain's own guard covers it. */
    app = calloc(1, sizeof(*app));
    TEST_WEVR_ASSERT(app != NULL, "App stub allocates");
    app->scene = scene;
    App_WorldDrainEntityRemovedFor(app, world);

    TEST_WEVR_ASSERT(World_EventsCount(world) == 0, "the drain emptied the root queue");
    TEST_WEVR_ASSERT(
        !ToriDraw_SceneElementIsLive(scene, element),
        "the drain freed the removed entity's scene element rather than orphaning it");

    /* The rebuild the teleport triggers. Before the fix this line aborted on
     * `world->event_count == 0` in World_ResetSceneAlloc. */
    WorldBuilder_RebuildCenterzoneBegin(builder, 51, 51, 64);
    TEST_WEVR_ASSERT(
        world->heightmap != NULL, "the root rebuild reallocated the scene after the drain");
    TEST_WEVR_ASSERT(World_EventsCount(world) == 0, "the rebuild left the queue empty");
    TEST_WEVR_ASSERT(
        !ToriDraw_SceneElementIsLive(scene, element),
        "the removed element did not come back through the rebuild");

    free(app);
    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    dat2_buildcache_free(bc);
    printf("ok - the root rebuild drains EntityRemoved before World_ResetSceneAlloc\n");
}

static void
test_raft_deck_rebuild(char const* cache_dir)
{
    struct stat st;
    struct ToriRS_IO* io;
    struct ToriRS_TaskQueue* queue;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct RSCache_Dat2Disk* disk;
    struct RSCache profile;
    struct PlatformX_IO* px;
    struct ToriDraw_Scene* scene;
    struct VarPManager varp;
    struct World* root_world;
    struct WorldBuilder* root_builder;
    struct World* boat_world;
    struct WorldBuilder* boat_builder;
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct RevPacket p;
    uint8_t buf[16];
    int len;
    int32_t zones[PKT_MAP_REBUILD_ZONES];
    int chunks[2] = { RAFT_SRC_ZONE >> 3, RAFT_SRC_ZONE >> 3 };
    int boat_zone_center;
    int boat_static_pool = TORIDRAW_SCENE_POOL_STATIC_VIEW(RAFT_VIEW_ID);
    int boat_dynamic_pool = TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(RAFT_VIEW_ID);
    struct PoolSnapshot boat_deck;
    struct PoolSnapshot boat_deck_rebuilt;
    struct PoolSnapshot root_static;
    /* Stand-ins for the entity elements the two views would own at sea: a
     * player on the mainland, a player on the deck. Nothing claims them, which
     * is the point — the OWNING view's rebuild is supposed to reclaim its own
     * orphan, and no other view's rebuild may touch it. */
    int boat_entity_element;
    int root_entity_element;

    if( stat(cache_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        fprintf(
            stderr,
            "\n*** SKIP: no dat2 cache at %s — the raft-deck rebuild was NOT verified "
            "against real map data. Run from src/ with ../cache.osrs239 present. ***\n\n",
            cache_dir);
        return;
    }

    io = ToriRS_IO_New();
    queue = ToriRS_TaskQueue_New();
    bc = dat2_buildcache_new();
    provider = dat2_buildcache_as_provider(bc);
    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    TEST_WEVR_ASSERT(disk != NULL, "dat2 disk opens");
    TEST_WEVR_ASSERT(RSCache_ProfileByName("osrs239", &profile), "osrs239 profile exists");
    RSCache_Dat2DiskSetProfile(disk, &profile);
    CacheProvider_SetProfile(provider, &profile);
    /* cache.osrs239 ships plain map-loc archives (rscache README: pre-237
     * caches carry keys, this one needs none); keep the gate anyway so a
     * pointed-elsewhere CACHE_DIR fails loudly at the scenery asserts instead
     * of silently decoding garbage. */
    if( RSCache_MapLocsEncrypted(&profile) )
    {
        char xtea_path[1024];
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cache_dir);
        if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
            fprintf(stderr, "warning: no xtea keys at %s\n", xtea_path);
    }
    px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(px, disk);

    ToriDraw_Init();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    VarPManager_Init(&varp);

    /* Two worlds, ONE scene — the C2 ownership shape: the root and the boat
     * each get their own World + WorldBuilder, and every element they place
     * goes into the same scene-global element pool. */
    root_world = World_New();
    root_builder = WorldBuilder_New(root_world, provider, scene, &varp);
    boat_world = World_New();
    boat_builder = WorldBuilder_New(boat_world, provider, scene, &varp);
    /* ...and the boat builds into its VIEW's element pools, which is what keeps
     * "same scene" from meaning "same clear". The root builder stays on
     * WORLDVIEW_ROOT's pair, i.e. exactly the pools it has always used. */
    WorldBuilder_SetSceneView(boat_builder, RAFT_VIEW_ID);
    TEST_WEVR_ASSERT(
        boat_builder->static_pool == boat_static_pool, "boat builder took the view's static pool");
    TEST_WEVR_ASSERT(
        boat_builder->dynamic_pool == boat_dynamic_pool,
        "boat builder took the view's dynamic pool");
    TEST_WEVR_ASSERT(
        root_builder->static_pool == TORIDRAW_SCENE_POOL_STATIC,
        "root builder stays on the historic static pool");
    TEST_WEVR_ASSERT(
        root_builder->dynamic_pool == TORIDRAW_SCENE_POOL_DYNAMIC,
        "root builder stays on the historic dynamic pool");

    /* Feed the wire bytes end to end: parse -> decode at the view's size. */
    memset(buf, 0, sizeof(buf));
    len = build_raft_packet(buf, zone_descriptor(RAFT_SRC_ZONE, RAFT_SRC_ZONE, 0, 0));
    memset(&p, 0, sizeof(p));
    TEST_WEVR_ASSERT(
        rev->parse(rev, PKT_NAME_REBUILD_WORLDENTITY, buf, len, &p), "raft packet parses");
    p.packet_type = PKT_NAME_REBUILD_WORLDENTITY; /* net.c stamps this in the live path */
    TEST_WEVR_ASSERT(PktRebuildWev_DecodeZones(&p._rebuild_wev, 1, 1, zones), "grid decodes");

    /* The exec branch's base-pinning arithmetic for an 8-tile scene:
     * zone_center = base/8 + scene_size/16, so ResetScene's
     * (zone_center - scene_size/16) * 8 gives back exactly the wire base. */
    boat_zone_center = p._rebuild_wev.base_x / 8 + 8 / 16;

    run_task(
        queue, io, px,
        CreateTask_WorldLoad(
            provider, boat_builder, queue, chunks, 1, boat_zone_center, boat_zone_center, 8,
            zones, NULL, NULL));

    TEST_WEVR_ASSERT(boat_world->_base_tile_x == RAFT_BASE_TILE, "boat base X pinned to the wire");
    TEST_WEVR_ASSERT(boat_world->_base_tile_z == RAFT_BASE_TILE, "boat base Z pinned to the wire");
    TEST_WEVR_ASSERT(boat_world->load_complete, "boat load completes");
    TEST_WEVR_ASSERT(boat_world->heightmap != NULL, "boat heightmap allocated");
    TEST_WEVR_ASSERT(boat_world->painter != NULL, "boat world owns its own painter");
    TEST_WEVR_ASSERT(
        boat_world->entities.terrain.active_count > 0, "deck tiles land in the BOAT world");
    TEST_WEVR_ASSERT(
        boat_world->entities.scenery.active_count > 0, "castle locs land in the BOAT world");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementSlotCount(scene) > 0, "boat elements live in the shared scene");

    /* The root world must be exactly as World_New left it: no tiles, no locs,
     * no scene alloc — the rebuild was built against the boat's view. */
    TEST_WEVR_ASSERT(
        root_world->entities.terrain.active_count == 0, "no tiles leak into the root world");
    TEST_WEVR_ASSERT(
        root_world->entities.scenery.active_count == 0, "no locs leak into the root world");
    TEST_WEVR_ASSERT(root_world->heightmap == NULL, "root scene never allocated");
    TEST_WEVR_ASSERT(root_world->painter == NULL, "root painter never created");

    /* The deck's geometry is in the VIEW's static pool, and the root's pool is
     * still empty — nothing the boat built was tagged as the mainland's. */
    pool_snapshot_take(&boat_deck, scene, boat_static_pool);
    TEST_WEVR_ASSERT(boat_deck.count > 0, "deck elements carry the boat view's static pool");
    {
        struct PoolSnapshot root_before;

        pool_snapshot_take(&root_before, scene, TORIDRAW_SCENE_POOL_STATIC);
        TEST_WEVR_ASSERT(
            root_before.count == 0, "the boat's build put nothing in the root's static pool");
        pool_snapshot_free(&root_before);
    }
    boat_entity_element = ToriDraw_SceneElementAddPool(scene, boat_dynamic_pool);
    TEST_WEVR_ASSERT(boat_entity_element >= 0, "the boat view's dynamic pool allocates");

    /* Ground truth: a real root-world Lumbridge rebuild (classic 104 scene,
     * 3x3 squares, zone center 404). The deck's heights must equal the source
     * zone's, tile for tile: source zone 402 starts at world tile 3216, the
     * root scene is based at (404-6)*8 = 3184, so root-local (32+x, 32+z). */
    {
        int root_chunks[18];
        int count = 0;
        int src_local = (RAFT_SRC_ZONE - (404 - 104 / 16)) * 8;

        for( int mx = 49; mx <= 51; mx++ )
            for( int mz = 49; mz <= 51; mz++ )
            {
                root_chunks[count * 2] = mx;
                root_chunks[count * 2 + 1] = mz;
                count++;
            }
        run_task(
            queue, io, px,
            CreateTask_WorldLoad(
                provider, root_builder, queue, root_chunks, count, 404, 404, 104, NULL, NULL,
                NULL));
        TEST_WEVR_ASSERT(root_world->load_complete, "root load completes");
        TEST_WEVR_ASSERT(
            root_world->entities.terrain.active_count > 0, "root rebuild fills the root world");

        for( int x = 0; x < 8; x++ )
            for( int z = 0; z < 8; z++ )
                TEST_WEVR_ASSERT(
                    heightmap_get(boat_world->heightmap, x, z, 0) ==
                        heightmap_get(root_world->heightmap, src_local + x, src_local + z, 0),
                    "deck height == source zone height, tile for tile");
    }

    /* --- The mainland's rebuild ran with the boat already built. ---------
     * Before the pool split this is where the deck died: the root's
     * ClearPool(STATIC) freed every element in the scene, and its dynamic
     * reconcile swept every element no ROOT entity claimed. */
    pool_snapshot_assert_intact(
        &boat_deck, scene, "the root's rebuild left the deck's elements alone");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, boat_entity_element),
        "the root's dynamic sweep left the boat's entity element alone");
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, boat_entity_element);
        TEST_WEVR_ASSERT(
            el && el->pool == (uint8_t)boat_dynamic_pool, "the boat's entity element kept its pool");
    }

    /* --- Now the same thing the other way round: rebuild the boat with the
     * mainland already built. A boat rebuilds every time it moves a zone, so
     * this is the direction that would fire every few seconds at sea. */
    pool_snapshot_take(&root_static, scene, TORIDRAW_SCENE_POOL_STATIC);
    TEST_WEVR_ASSERT(root_static.count > 0, "the root's rebuild filled the root's static pool");
    root_entity_element = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC);
    TEST_WEVR_ASSERT(root_entity_element >= 0, "the root view's dynamic pool allocates");

    /* World_ResetSceneAlloc asserts the world's EntityRemoved queue is drained
     * before it reallocates; the live path drains at the exec branch, and this
     * fixture never spawns an entity, so the boat's queue is already empty. */
    TEST_WEVR_ASSERT(World_EventsCount(boat_world) == 0, "boat queue drained before the rebuild");
    run_task(
        queue, io, px,
        CreateTask_WorldLoad(
            provider, boat_builder, queue, chunks, 1, boat_zone_center, boat_zone_center, 8,
            zones, NULL, NULL));
    TEST_WEVR_ASSERT(boat_world->load_complete, "boat reload completes");

    pool_snapshot_assert_intact(
        &root_static, scene, "the boat's rebuild left the mainland's elements alone");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, root_entity_element),
        "the boat's dynamic sweep left the mainland's entity element alone");
    TEST_WEVR_ASSERT(
        root_world->entities.terrain.active_count > 0, "the mainland's tiles survive too");
    /* The sweep is scoped, not disabled: the boat's own unclaimed element is
     * still reclaimed by the boat's own rebuild. */
    TEST_WEVR_ASSERT(
        !ToriDraw_SceneElementIsLive(scene, boat_entity_element) ||
            ToriDraw_SceneElementGet(scene, boat_entity_element)->pool !=
                (uint8_t)boat_dynamic_pool,
        "the boat's rebuild reclaims its own orphaned entity element");

    /* --- The despawn sweep (App_WevDespawn): clearing the view's static pool
     * takes the deck and nothing else. Without it a sunk boat's elements would
     * sit in the scene forever, since no other view's clear can reach them. */
    pool_snapshot_take(&boat_deck_rebuilt, scene, boat_static_pool);
    TEST_WEVR_ASSERT(boat_deck_rebuilt.count > 0, "the rebuilt deck is in the view's static pool");
    ToriDraw_SceneClearPool(scene, boat_static_pool);
    ToriDraw_SceneClearPool(scene, boat_dynamic_pool);
    pool_snapshot_assert_gone(&boat_deck_rebuilt, scene, "despawn frees the deck's elements");
    pool_snapshot_assert_intact(
        &root_static, scene, "despawning the boat leaves the mainland's elements alone");
    TEST_WEVR_ASSERT(
        ToriDraw_SceneElementIsLive(scene, root_entity_element),
        "despawning the boat leaves the mainland's entity element alone");

    /* OQ4: a loaded104-tile root plus all15 real-cache small decks must fit
     * simultaneously. Castle template zones are denser than the sailing hull
     * templates, and retain both real terrain and real scenery in every view. */
    {
        struct WorldviewRegistry registry;
        struct PoolSnapshot decks[WORLDVIEW_MAX];
        memset(decks,0,sizeof(decks));
        WorldviewRegistry_Init(&registry);
        WorldviewRegistry_RegisterRoot(&registry,root_world,root_builder);
        int total=root_static.count;
        for(int id=1;id<WORLDVIEW_MAX;++id)
        {
            struct World* world=id==1 ? boat_world:World_New();
            struct WorldBuilder* builder=id==1 ? boat_builder:WorldBuilder_New(world,provider,scene,&varp);
            WorldBuilder_SetSceneView(builder,id);
            int base=RAFT_BASE_TILE+id*16;
            WorldviewRegistry_Register(&registry,id,world,builder,base,RAFT_BASE_TILE,8,8,0);
            run_task(queue,io,px,CreateTask_WorldLoad(provider,builder,queue,chunks,1,
                base/8,RAFT_BASE_TILE/8,8,zones,NULL,NULL));
            TEST_WEVR_ASSERT(world->load_complete && world->entities.terrain.active_count>0 &&
                world->entities.scenery.active_count>0,"each of15 actual-cache decks finished terrain and loc loading");
            pool_snapshot_take(&decks[id],scene,TORIDRAW_SCENE_POOL_STATIC_VIEW(id));
            TEST_WEVR_ASSERT(decks[id].count>0,"all15 static deck pools contain real geometry");
            total+=decks[id].count;
            pool_snapshot_assert_intact(&root_static,scene,"maximum deck loading preserves the full root scene");
            for(int prior=1;prior<id;++prior)
                pool_snapshot_assert_intact(&decks[prior],scene,"later deck allocation cannot overwrite an earlier view");
        }
        for(int id=0;id<WORLDVIEW_MAX;++id)
            TEST_WEVR_ASSERT(WorldviewRegistry_IsLive(&registry,id),"all16 registry slots coexist");
        TEST_WEVR_ASSERT(total<TORIDRAW_SCENE_MAX_ELEMENTS,"root plus15 decks leaves element capacity for dynamics");
        int actor=ToriDraw_SceneElementAddPool(scene,TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(1));
        TEST_WEVR_ASSERT(actor>=0,"dynamic actor still allocates at maximum view count");
        int highwater=ToriDraw_SceneElementSlotCount(scene);
        for(int id=2;id<WORLDVIEW_MAX;++id)
        {
            ToriDraw_SceneElementSetPool(scene,actor,TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(id));
            ToriDraw_SceneClearPool(scene,TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(id-1));
            TEST_WEVR_ASSERT(ToriDraw_SceneElementIsLive(scene,actor) &&
                ToriDraw_SceneElementPool(scene,actor)==TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(id),
                "one actor retains its ID while transferring through every deck pool");
            TEST_WEVR_ASSERT(ToriDraw_SceneElementSlotCount(scene)==highwater,
                "actor pool migration allocates no duplicate elements");
        }
        ToriDraw_SceneElementSetPool(scene,actor,TORIDRAW_SCENE_POOL_DYNAMIC);
        for(int id=WORLDVIEW_MAX-1;id>0;--id)
        {
            ToriDraw_SceneClearPool(scene,TORIDRAW_SCENE_POOL_STATIC_VIEW(id));
            ToriDraw_SceneClearPool(scene,TORIDRAW_SCENE_POOL_DYNAMIC_VIEW(id));
            pool_snapshot_assert_gone(&decks[id],scene,"despawn frees only that deck at maximum occupancy");
            for(int other=1;other<id;++other)
                pool_snapshot_assert_intact(&decks[other],scene,"despawn keeps every remaining deck intact");
            pool_snapshot_assert_intact(&root_static,scene,"maximum occupancy despawn keeps root geometry intact");
            TEST_WEVR_ASSERT(ToriDraw_SceneElementIsLive(scene,actor),"actor evicted to root survives all15 deck despawns");
            WorldviewRegistry_Release(&registry,id);
            pool_snapshot_free(&decks[id]);
        }
        ToriDraw_SceneElementRemove(scene,actor);
        WorldviewRegistry_Free(&registry); /* root is borrowed */
        boat_builder=NULL;boat_world=NULL;
        printf("ok - maximum16 views: root +15 actual-cache decks hold %d static elements; one actor migrated all15 pools; isolated despawn PASS\n",total);
    }

    pool_snapshot_free(&boat_deck);
    pool_snapshot_free(&boat_deck_rebuilt);
    pool_snapshot_free(&root_static);

    gameproto_free(&p);
    if(boat_builder)WorldBuilder_Free(boat_builder);
    if(boat_world)World_Free(boat_world);
    WorldBuilder_Free(root_builder);
    World_Free(root_world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    dat2_buildcache_free(bc);
    printf("ok - 1-zone raft deck rebuilt into the boat world against %s\n", cache_dir);
    printf("ok - boat and root rebuilds isolate their scene pools, despawn frees only the deck\n");
}

int
main(int argc, char** argv)
{
    char const* cache_dir = argc > 1 ? argv[1] : "../cache.osrs239";

    test_parse_and_decode();
    test_pool_view_tags();
    test_actor_pool_handoff();
    test_root_rebuild_drains_entity_removed();
    test_raft_deck_rebuild(cache_dir);

    printf("wev_rebuild_test: all passed\n");
    return 0;
}
