#ifndef TORIRS_PERF_H
#define TORIRS_PERF_H

/*
 * Per-stage frame timers and counters for the performance harness.
 *
 * Enable with TORIRS_PERF=1 (or TorirsPerf_Init(1)). When off, every macro is
 * a load-and-branch (or nothing under -DTORIRS_PERF_DISABLE). Stages and
 * counters are a fixed compile-time enum — no strings at runtime, no hashing.
 *
 * Report at exit via TorirsPerf_Report(): per-stage mean/p50/p95/max, main
 * thread CPU time (which excludes waits), counter totals, frames over 20 ms,
 * and effective fps. On modern Windows, exact begin-to-frame-end thread cycles
 * define the non-waiting work interval. The coarse GetThreadTimes interval is
 * an aggregate scale calibration; the cycle API is runtime-resolved so XP
 * remains loadable.
 * Optional TORIRS_PERF_CSV=<path> writes both `cpu` distribution and `cpu_raw`
 * aggregate rows in a machine-readable CSV for tools/perf/compare.py.
 *
 * Windowed drift: TORIRS_PERF_WINDOW=<N> (default 1000) also appends one row
 * per stage, CPU metric, and counter every N frames (kind=window_stage /
 * window_cpu / window_cpu_raw / window_counter / window_gauge). Counters are
 * deltas over the window; gauges (COUNT_SET) are the last sample. compare.py
 * --drift reads these.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum TorirsPerfStage
{
    TORIRS_PERF_STAGE_FRAME = 0,
    TORIRS_PERF_STAGE_ASYNC,
    TORIRS_PERF_STAGE_LOGIC,
    TORIRS_PERF_STAGE_CS2,
    TORIRS_PERF_STAGE_LAYOUT,
    TORIRS_PERF_STAGE_INTERACT,
    TORIRS_PERF_STAGE_EMIT,
    TORIRS_PERF_STAGE_PAINT,
    TORIRS_PERF_STAGE_BUILD,
    TORIRS_PERF_STAGE_RENDER,
    /** Classifying renderer hits into the world hover/pick set. */
    TORIRS_PERF_STAGE_PICK_FINISH,
    TORIRS_PERF_STAGE_PRESENT,
    /** Embedded mock230 pump + world tick (net_transport_embed). */
    TORIRS_PERF_STAGE_SERVER,
    /** Native event/network polling before the command bus is drained. */
    TORIRS_PERF_STAGE_PLATFORM_POLL,
    /** Applying the queued platform/network commands to client state. */
    TORIRS_PERF_STAGE_COMMAND_DRAIN,
    /** Complete App_RunOnce duration; contains the finer app stages above. */
    TORIRS_PERF_STAGE_APP_RUN,
    /** Audio submission and window-title maintenance after presentation. */
    TORIRS_PERF_STAGE_FRAME_POST,
    /** Native test-input synthesis, including platform polling. */
    TORIRS_PERF_STAGE_INPUT_PREP,
    /** Canvas/backbuffer reconciliation after commands are applied. */
    TORIRS_PERF_STAGE_SURFACE_SYNC,
    /** Complete build/render/pick/present call; contains its detailed stages. */
    TORIRS_PERF_STAGE_DISPLAY,
    /** Fixed/resizable mode reconciliation after presentation. */
    TORIRS_PERF_STAGE_WINDOW_SYNC,
    /** The 50 fps pacing wait itself. Sampled after the frame timer closes, so
     * frame N's wait is flushed with frame N+1 — the distribution is what
     * matters, and it is the only way to see the cap overshoot its deadline. */
    TORIRS_PERF_STAGE_PACE,
    /** Wall time between consecutive frame starts — work plus pace plus the
     * loop overhead that sits in neither. This is the only stage that answers
     * "what frame rate does the player actually see". */
    TORIRS_PERF_STAGE_PERIOD,
    /** The app_settle_cs2_frame portion of `cs2`, nested inside it. `cs2` minus
     * this is the logic tick's own script execution. */
    TORIRS_PERF_STAGE_CS2_SETTLE,
    /** Software rasterization of one obj inventory icon: model copy, scale,
     * recolour, lighting and a 36x32 3D render. Nested under whatever asked for
     * the icon, which in practice is a CS2 interface script. */
    TORIRS_PERF_STAGE_UI_ICON,
    /** The serial packet pipeline inside one logic tick: TaskRunner_SettleFrame
     * plus every GameProtoExec task it runs. Nested inside `cs2`, which spans
     * the whole tick body rather than script execution alone. */
    TORIRS_PERF_STAGE_TICK_PACKETS,
    TORIRS_PERF_STAGE_COUNT
};

enum TorirsPerfCounter
{
    /* UITree */
    TORIRS_PERF_CTR_UITREE_FIND_ID = 0,
    TORIRS_PERF_CTR_UITREE_FIND_ID_PROBES,
    TORIRS_PERF_CTR_UITREE_FIND_ID_LINEAR,
    TORIRS_PERF_CTR_UITREE_ID_REBUILD,
    TORIRS_PERF_CTR_UITREE_FIND_CHILD,
    TORIRS_PERF_CTR_UITREE_FIND_CHILD_HIT,
    TORIRS_PERF_CTR_UITREE_FIND_CHILD_CEIL_MISS,
    TORIRS_PERF_CTR_UITREE_WALK_EMIT,
    TORIRS_PERF_CTR_UITREE_WALK_EMIT_DRAG,
    TORIRS_PERF_CTR_UITREE_WALK_HIT,
    TORIRS_PERF_CTR_UITREE_WALK_HOVER,
    TORIRS_PERF_CTR_UITREE_WALK_DROP,
    TORIRS_PERF_CTR_UITREE_EMIT_SKIP,
    TORIRS_PERF_CTR_UITREE_LAYOUT_RESOLVE,
    /** Resolves that returned without walking (nothing invalidated since). */
    TORIRS_PERF_CTR_UITREE_LAYOUT_SKIP,
    TORIRS_PERF_CTR_UITREE_LAYOUT_NODES,
    /** Nodes the walk visited but did not recompute (own box and parent's box
     *  both unchanged). LAYOUT_NODES minus this is the real per-frame work. */
    TORIRS_PERF_CTR_UITREE_LAYOUT_NODE_SKIP,
    TORIRS_PERF_CTR_UITREE_LAYOUT_DEPTH_RECOMPUTE,
    TORIRS_PERF_CTR_UITREE_ENSURE_LAYOUT,
    TORIRS_PERF_CTR_UITREE_CC_CREATE,
    TORIRS_PERF_CTR_UITREE_CC_DELETE,
    TORIRS_PERF_CTR_UITREE_CC_DELETEALL,
    TORIRS_PERF_CTR_UITREE_CC_DELETEALL_ROWS,
    TORIRS_PERF_CTR_UITREE_APPLY_GEO,
    TORIRS_PERF_CTR_UITREE_APPLY_CONTENT,
    TORIRS_PERF_CTR_UITREE_APPLY_HOOK,
    TORIRS_PERF_CTR_UITREE_APPLY_OTHER,
    TORIRS_PERF_CTR_UITREE_KEY_SCAN,
    TORIRS_PERF_CTR_UITREE_KEY_SCAN_NODES,
    TORIRS_PERF_CTR_UITREE_COMPONENTS,
    TORIRS_PERF_CTR_UITREE_CAPACITY,
    TORIRS_PERF_CTR_UITREE_FREE_LIST,
    TORIRS_PERF_CTR_UITREE_NODE_BYTES,

    /* CS2 VM */
    TORIRS_PERF_CTR_CS2_SCRIPTS,
    TORIRS_PERF_CTR_CS2_OPCODES,
    TORIRS_PERF_CTR_CS2_HOST_OPS,
    TORIRS_PERF_CTR_CS2_CYCLES,
    TORIRS_PERF_CTR_CS2_ABORTS,
    TORIRS_PERF_CTR_CS2_VM_ACQUIRE,
    TORIRS_PERF_CTR_CS2_VM_POOL_HIT,
    TORIRS_PERF_CTR_CS2_VM_POOL_MISS,
    TORIRS_PERF_CTR_CS2_VM_INIT_NS,
    TORIRS_PERF_CTR_CS2_FRAME_POOL_HIT,
    TORIRS_PERF_CTR_CS2_FRAME_POOL_MISS,

    /* CacheProvider */
    TORIRS_PERF_CTR_CACHE_MODEL_HIT,
    TORIRS_PERF_CTR_CACHE_MODEL_MISS,
    TORIRS_PERF_CTR_CACHE_MODEL_EVICT,
    TORIRS_PERF_CTR_CACHE_SPRITE_HIT,
    TORIRS_PERF_CTR_CACHE_SPRITE_MISS,
    TORIRS_PERF_CTR_CACHE_SPRITE_EVICT,
    TORIRS_PERF_CTR_CACHE_TEXTURE_HIT,
    TORIRS_PERF_CTR_CACHE_TEXTURE_MISS,
    TORIRS_PERF_CTR_CACHE_OBJTYPE_HIT,
    TORIRS_PERF_CTR_CACHE_OBJTYPE_MISS,
    TORIRS_PERF_CTR_CACHE_NPCTYPE_HIT,
    TORIRS_PERF_CTR_CACHE_NPCTYPE_MISS,
    TORIRS_PERF_CTR_CACHE_LOC_HIT,
    TORIRS_PERF_CTR_CACHE_LOC_MISS,
    TORIRS_PERF_CTR_CACHE_OTHER_HIT,
    TORIRS_PERF_CTR_CACHE_OTHER_MISS,
    TORIRS_PERF_CTR_CACHE_GROUP_HIT,
    TORIRS_PERF_CTR_CACHE_GROUP_MISS,
    TORIRS_PERF_CTR_CACHE_GROUP_EVICT,
    TORIRS_PERF_CTR_CACHE_ARCHIVE_HIT,
    TORIRS_PERF_CTR_CACHE_ARCHIVE_MISS,
    TORIRS_PERF_CTR_CACHE_ARCHIVE_EVICT,

    /* Model instance caches */
    TORIRS_PERF_CTR_MODEL_INST_HIT,
    TORIRS_PERF_CTR_MODEL_INST_MISS,
    TORIRS_PERF_CTR_MODEL_INST_EVICT,

    /* Painter bucket */
    TORIRS_PERF_CTR_PAINTER_POPS,
    TORIRS_PERF_CTR_PAINTER_GATE_REJECTS,
    TORIRS_PERF_CTR_PAINTER_PUSHES,
    TORIRS_PERF_CTR_PAINTER_PUSH_DEDUP,
    TORIRS_PERF_CTR_PAINTER_DRAIN_EVENTS,
    TORIRS_PERF_CTR_PAINTER_COMMANDS,
    TORIRS_PERF_CTR_PAINTER_TILES_REMAINING_SET,

    /* Growth gauges (COUNT_SET once per frame / tick — last value in a window) */
    TORIRS_PERF_CTR_SCENE_ELEMENTS,
    TORIRS_PERF_CTR_SCENE_ANIM_LIST,
    TORIRS_PERF_CTR_ZONE_MAP_COUNT,
    TORIRS_PERF_CTR_ZONE_MAP_CAPACITY,
    TORIRS_PERF_CTR_NPC_SLOT_MAX,
    TORIRS_PERF_CTR_CACHE_MODEL_SIZE,
    TORIRS_PERF_CTR_CACHE_SPRITE_SIZE,

    /* Interface open/close lifecycle (events + hitch ns) */
    TORIRS_PERF_CTR_IFACE_OPEN,
    TORIRS_PERF_CTR_IFACE_CLOSE,
    TORIRS_PERF_CTR_IFACE_BAKE,
    TORIRS_PERF_CTR_IFACE_BAKE_REUSE,
    TORIRS_PERF_CTR_IFACE_MOUNT_NS,
    TORIRS_PERF_CTR_IFACE_CLOSE_NS,

    /* UITree growth under multi-panel churn (COUNT_SET gauges) */
    TORIRS_PERF_CTR_IFACE_GROUPS_RESIDENT,
    TORIRS_PERF_CTR_UITREE_HIDDEN,
    TORIRS_PERF_CTR_UITREE_UNMOUNTED,
    TORIRS_PERF_CTR_UITREE_FREED,
    TORIRS_PERF_CTR_UITREE_HOOK_BLOCKS,
    TORIRS_PERF_CTR_UITREE_HOOK_BYTES,
    TORIRS_PERF_CTR_HOST_INV_HOOKS,
    TORIRS_PERF_CTR_HOST_VAR_HOOKS,
    TORIRS_PERF_CTR_HOST_STAT_HOOKS,
    TORIRS_PERF_CTR_BRIDGE_SPRITE_MAP,
    TORIRS_PERF_CTR_BRIDGE_MODEL_MAP,
    TORIRS_PERF_CTR_BRIDGE_OBJ_ICON_MAP,
    TORIRS_PERF_CTR_CACHE_CLIENTSCRIPT_SIZE,

    /* Full-array scan costs (accumulate nodes locally, count once per call) */
    TORIRS_PERF_CTR_UITREE_ANIM_SCAN,
    TORIRS_PERF_CTR_UITREE_ANIM_SCAN_NODES,
    TORIRS_PERF_CTR_UITREE_ANIM_MODEL_NODES,
    TORIRS_PERF_CTR_UITREE_WHEEL_SCAN_NODES,
    TORIRS_PERF_CTR_UITREE_OPKEY_SCAN_NODES,
    TORIRS_PERF_CTR_UITREE_HOOK_INDEX_REBUILD_NODES,
    TORIRS_PERF_CTR_IFACE_GROUP_SCAN_NODES,

    /* D3D9 retained-resource traffic. Steady-state static values should be 0. */
    TORIRS_PERF_CTR_D3D9_WORLD_ATLAS_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_WORLD_ATLAS_UPLOADS,
    TORIRS_PERF_CTR_D3D9_ANIMATED_TEXTURE_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_ANIMATED_TEXTURE_UPLOADS,
    TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOADS,
    TORIRS_PERF_CTR_D3D9_UI_MODEL_FALLBACKS,
    TORIRS_PERF_CTR_D3D9_UI_BATCH_DRAWS,
    TORIRS_PERF_CTR_D3D9_UI_WIDGET_DRAWS,
    TORIRS_PERF_CTR_D3D9_UI_ROTMASK_DRAWS,
    TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOADS,
    TORIRS_PERF_CTR_D3D9_DYNAMIC_VBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_DYNAMIC_VBO_UPLOADS,
    TORIRS_PERF_CTR_D3D9_STATIC_BATCH_BUILDS,
    TORIRS_PERF_CTR_D3D9_STATIC_BATCH_ACTIVE_PAGES,
    TORIRS_PERF_CTR_D3D9_IBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_D3D9_IBO_UPLOADS,
    /* World-submit diagnostics.  These distinguish index bandwidth from the
     * driver overhead of fragmented painter-order draw ranges. */
    TORIRS_PERF_CTR_D3D9_IBO_INDICES,
    TORIRS_PERF_CTR_D3D9_IBO_CHAIN_NODES,
    TORIRS_PERF_CTR_D3D9_DRAW_RANGES,
    TORIRS_PERF_CTR_D3D9_DRAW_CALLS,
    TORIRS_PERF_CTR_D3D9_PAGE_SWITCHES,
    TORIRS_PERF_CTR_D3D9_TEXTURE_SWITCHES,
    TORIRS_PERF_CTR_D3D9_STREAM_SWITCHES,
    /* Opt-in depth-mode work. Legacy --d3d9 leaves these at zero. */
    TORIRS_PERF_CTR_D3D9_Z_OPAQUE_TRIANGLES,
    TORIRS_PERF_CTR_D3D9_Z_BLENDED_TRIANGLES,
    TORIRS_PERF_CTR_D3D9_Z_SORTED_MODELS,

    /*
     * GPU retained-resource traffic, for --opengl3 and --webgl1.
     *
     * Deliberately the same questions the D3D9 block above answers, because the
     * contract is the same one (WINDOWS-D3D9-UPLOAD-001): in a steady scene
     * where nothing loaded, the atlas and static vertex counters must be zero,
     * and only the index stream moves — painter order changes every frame.
     *
     * The chunk counters are WebGL1's alone. It has no VAOs and indexes with 16
     * bits, so a draw range that spans a 65536-vertex boundary becomes several
     * draws with the attribute pointers re-based between them; chunks-per-range
     * is how much that is costing.
     */
    TORIRS_PERF_CTR_GL_ATLAS_UPLOAD_BYTES,
    TORIRS_PERF_CTR_GL_ATLAS_UPLOADS,
    TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOADS,
    TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS,
    TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES,
    TORIRS_PERF_CTR_GL_IBO_UPLOADS,
    TORIRS_PERF_CTR_GL_DRAW_RANGES,
    TORIRS_PERF_CTR_GL_DRAW_CALLS,
    TORIRS_PERF_CTR_GL_ATTRIB_REBINDS,
    TORIRS_PERF_CTR_GL_2D_BATCH_FLUSHES,
    /* Depth-buffered world pass (--webgl1-zbuffer / --opengl3-zbuffer). The
     * same three questions D3D9's z counters answer: in a scene with no
     * translucent faces gl_z_sorted_models must be zero while
     * gl_z_opaque_triangles is nonzero, because only models with real
     * translucency pay the priority/depth sort. */
    TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES,
    TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES,
    TORIRS_PERF_CTR_GL_Z_SORTED_MODELS,

    /** Logic ticks run in one frame. The scene advances once per tick, so a
     * frame running 0 or 2 of them stutters even when the frame rate is
     * steady; the distribution matters far more than the mean. */
    TORIRS_PERF_CTR_LOGIC_TICKS,

    /** Server packets handed to the serial exec pipeline in one tick. */
    TORIRS_PERF_CTR_PROTO_PACKETS,
    /** Obj inventory icons rasterized from scratch (cache misses only). */
    TORIRS_PERF_CTR_UI_ICON_RASTER,
    /** Interface component models built from scratch (cache misses only). */
    TORIRS_PERF_CTR_UI_MODEL_BUILD,

    /** Npc spawns/retypes served from the lit-model instance cache. */
    TORIRS_PERF_CTR_NPC_MODEL_CACHE_HIT,
    /** Npc spawns/retypes that had to merge, recolour and light from scratch.
     * A steady stream of these on a walk means the LRU is thrashing. */
    TORIRS_PERF_CTR_NPC_MODEL_CACHE_MISS,

    TORIRS_PERF_CTR_COUNT
};

#ifndef TORIRS_PERF_DISABLE

extern int g_torirs_perf_enabled;

void TorirsPerf_Init(int enabled);
void TorirsPerf_Shutdown(void);
void TorirsPerf_FrameBegin(void);
void TorirsPerf_FrameEnd(void);
void TorirsPerf_Report(void);

void TorirsPerf_StageBegin(enum TorirsPerfStage stage);
void TorirsPerf_StageEnd(enum TorirsPerfStage stage);
/** Record a duration measured after TorirsPerf_FrameEnd closed the frame timer.
 * The sample is carried into the next frame's bucket — FrameBegin would
 * otherwise clear it — so frame N's pacing wait is reported with frame N+1. */
void TorirsPerf_StageCarry(enum TorirsPerfStage stage, uint64_t ns);
void TorirsPerf_Count(enum TorirsPerfCounter counter, int64_t n);
void TorirsPerf_CountSet(enum TorirsPerfCounter counter, int64_t n);

/* Scoped helper: TorirsPerf_StageBegin on construct, End on scope exit via
 * a for-loop trick so it works in C without cleanup attributes. */
#define TORIRS_PERF_SCOPE(stage)                                               \
    for( int _tps_once = (g_torirs_perf_enabled                                \
                              ? (TorirsPerf_StageBegin(stage), 1)              \
                              : 0),                                            \
             _tps_done = 0;                                                    \
         !_tps_done;                                                           \
         _tps_done = 1,                                                        \
         (_tps_once ? TorirsPerf_StageEnd(stage) : (void)0) )

/* Explicit form of TORIRS_PERF_SCOPE, for functions whose measured region has
 * several exits. Every path taken after BEGIN must reach END. */
#define TORIRS_PERF_STAGE_BEGIN(stage)                                         \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_StageBegin((stage));                                    \
    } while( 0 )

#define TORIRS_PERF_STAGE_END(stage)                                           \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_StageEnd((stage));                                      \
    } while( 0 )

#define TORIRS_PERF_CARRY(stage, ns)                                           \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_StageCarry((stage), (ns));                              \
    } while( 0 )

#define TORIRS_PERF_COUNT(ctr, n)                                              \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_Count((ctr), (n));                                      \
    } while( 0 )

#define TORIRS_PERF_COUNT_SET(ctr, n)                                          \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_CountSet((ctr), (n));                                   \
    } while( 0 )

#define TORIRS_PERF_FRAME_BEGIN()                                              \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_FrameBegin();                                           \
    } while( 0 )

#define TORIRS_PERF_FRAME_END()                                                \
    do                                                                         \
    {                                                                          \
        if( g_torirs_perf_enabled )                                            \
            TorirsPerf_FrameEnd();                                             \
    } while( 0 )

#else /* TORIRS_PERF_DISABLE */

#define TORIRS_PERF_SCOPE(stage) for( int _tps_done = 0; !_tps_done; _tps_done = 1 )
#define TORIRS_PERF_STAGE_BEGIN(stage) ((void)0)
#define TORIRS_PERF_STAGE_END(stage) ((void)0)
#define TORIRS_PERF_CARRY(stage, ns) ((void)0)
#define TORIRS_PERF_COUNT(ctr, n) ((void)0)
#define TORIRS_PERF_COUNT_SET(ctr, n) ((void)0)
#define TORIRS_PERF_FRAME_BEGIN() ((void)0)
#define TORIRS_PERF_FRAME_END() ((void)0)
static inline void TorirsPerf_Init(int enabled) { (void)enabled; }
static inline void TorirsPerf_Shutdown(void) {}
static inline void TorirsPerf_Report(void) {}

#endif /* TORIRS_PERF_DISABLE */

#ifdef __cplusplus
}
#endif

#endif /* TORIRS_PERF_H */
