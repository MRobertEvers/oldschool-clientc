#ifndef TORIRS_PERF_H
#define TORIRS_PERF_H

/*
 * Per-stage frame timers and counters for the performance harness.
 *
 * Enable with TORIRS_PERF=1 (or TorirsPerf_Init(1)). When off, every macro is
 * a load-and-branch (or nothing under -DTORIRS_PERF_DISABLE). Stages and
 * counters are a fixed compile-time enum — no strings at runtime, no hashing.
 *
 * Report at exit via TorirsPerf_Report(): per-stage mean/p50/p95/max, counter
 * totals, frames over 20 ms, effective fps. Optional TORIRS_PERF_CSV=<path>
 * writes a machine-readable CSV for tools/perf/compare.py.
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
    TORIRS_PERF_STAGE_PRESENT,
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
