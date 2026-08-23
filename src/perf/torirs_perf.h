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
    /** Embedded ToriRSServer pump + world tick (net_transport_embed). */
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
    /** The full-tree UITree_LayoutResolve the settle loop runs once per
     * iteration, nested inside `cs2_settle`. `cs2_settle` minus this is the
     * task pump — the split that says whether settle cost is script dispatch
     * or repeated layout, which the two have to be told apart to optimize. */
    TORIRS_PERF_STAGE_CS2_SETTLE_LAYOUT,
    /** The letterbox bars around the image rectangle, nested in `present`. Four
     *  FillRects that are all empty whenever the window matches the canvas. */
    TORIRS_PERF_STAGE_PRESENT_FILL,
    /** The one blit that puts the finished DIB on the window, nested in
     *  `present`. `present` minus fill minus this is GetDC/ReleaseDC and the
     *  client-rect query — the split target 12 needs to know whether the cost
     *  is the copy, the scaler, or the window-DC round trip. */
    TORIRS_PERF_STAGE_PRESENT_BLIT,
    /** Every `RS_CS2Host_Exec` dispatch, nested inside `cs2`. The split that
     *  decides between targets 8 and 14: `cs2` divided by `cs2_scripts` is
     *  ~26.5 us per invocation against ~123 opcodes, which no bytecode dispatch
     *  loop can account for, and VM acquire/init/release together are only 0.6%
     *  of the stage. If this stage is most of `cs2`, the cost is host work the
     *  scripts request — tree writes, layout, lookups — and predecoding the
     *  interpreter (target 8) would optimize the part that is already cheap.
     *
     *  Two clock reads on a path that runs ~315 times a frame costs ~4% of the
     *  stage it measures, which is fine for an attribution run and is why this
     *  stays behind TORIRS_PERF like every other stage rather than becoming
     *  something the release path pays for. */
    TORIRS_PERF_STAGE_CS2_HOST_OP,
    /** `app_cs2_enqueue_followups`, the third limb of the settle loop, nested in
     *  `cs2_settle`. With `cs2_settle_layout` measured at 64 ns and host ops at
     *  9.7% of the whole `cs2` stage, the 284 us the settle loop costs has to be
     *  either the task pump (`TaskRunner_SettleFrame`) or this — and the two have
     *  unrelated fixes, so guessing between them would be the sixth wrong guess
     *  in a row. `cs2_settle` minus this minus `cs2_settle_layout` is the pump. */
    TORIRS_PERF_STAGE_CS2_SETTLE_FOLLOWUPS,
    /** Every `cs2vm2_run_script_body` call, nested inside `cs2` (and inside
     *  `cs2_settle` for the scripts the pump runs). `cs2_settle` minus this is
     *  the task machinery proper: queue, task alloc, state machines. That
     *  subtraction is the only way to size it without reconstructing it from
     *  per-script means times per-frame call rates, which is the arithmetic that
     *  produced the obsolete "183 us" figure. */
    TORIRS_PERF_STAGE_CS2_SCRIPT,
    /** `ToriRS_TaskQueue_Run` — queue mechanics plus every task body it resumes,
     *  script bodies included. `cs2_script` is nested inside it. */
    TORIRS_PERF_STAGE_TASK_QUEUE_RUN,
    /** `Task_CS2Run`'s per-dispatch prologue: VM acquire, thread bind, canvas
     *  and window mode, arg binding, `RS_CS2Host_ScriptStarted`. Runs once per
     *  script dispatch, ~17.6 times a frame. Nested inside `task_queue_run`,
     *  disjoint from `cs2_script`. */
    TORIRS_PERF_STAGE_CS2_TASK_START,
    /** Script-body nanoseconds that fell *inside* the `cs2` bracket, carried in
     *  rather than timed, so `cs2` minus this is exact. Do NOT subtract
     *  `cs2_script` from `cs2` to get the same thing — `cs2_script` counts every
     *  script body anywhere, and the task pump also runs outside `cs2` (the
     *  exec_runner settles on its own). Subtracting non-nested stages is what
     *  produced the "183 us of task machinery" that never existed. */
    TORIRS_PERF_STAGE_CS2_SCRIPT_IN,
    /** `PlatformX_IO_Pending` + `PlatformX_IO_Process`. Two Pending calls per
     *  settle iteration (loop head and loop condition), so a linear scan here is
     *  paid ~10 times a frame whether or not any IO is outstanding. */
    TORIRS_PERF_STAGE_TASK_IO,
    /* The `render` stage split by what the command stream asked for. `render`
     * is 77% of the i686 frame, and as one opaque bracket it made every
     * statement about "the rasterizer" inference. These six are disjoint and
     * together are `render` minus the command-bus walk itself.
     *
     * This is the gate for the R1-R4 rasterizer targets in
     * docs/CS2_OPTIMIZATION_TARGETS.md, so it stays. Cost when perf is off is
     * one predicted branch per command.
     *
     * Two clock reads per command is real overhead when perf is ON -- see
     * `r_cmds` for the divisor -- so read the split as a ratio between the
     * classes, not as absolute nanoseconds. */
    TORIRS_PERF_STAGE_R_CLEAR,
    TORIRS_PERF_STAGE_R_MODEL,
    TORIRS_PERF_STAGE_R_SPRITE,
    TORIRS_PERF_STAGE_R_FONT,
    TORIRS_PERF_STAGE_R_RECT,
    TORIRS_PERF_STAGE_R_OTHER,

    /* The three phases of soft3d_draw_model, nested inside `r_model`.
     *
     * `r_model` is 82.8% of `render` on the XP lane, and subtracting the two
     * span fills off it leaves 57% of the whole frame unaccounted for. That
     * remainder was a residual -- render minus what the ablation switches could
     * turn off -- not a measurement, and a residual is not something to
     * optimise against. These name the three calls the phase pipeline already
     * makes, so the remainder splits into per-vertex projection, the face
     * bucket sort, and the raster walk that is left once the fills are ablated.
     *
     * Six extra clock reads per drawn model when perf is ON. Divide by
     * `r_model_drawn`, and read them against each other rather than as
     * absolute nanoseconds -- same caveat as the six stages above. */
    TORIRS_PERF_STAGE_R_PROJECT,
    TORIRS_PERF_STAGE_R_SORT,
    TORIRS_PERF_STAGE_R_RASTER,
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
    /** Hook registrations that named the binding already in the slot, so the
     *  free/malloc/strdup round trip and the set resync were both skipped.
     *  APPLY_HOOK minus this is the registration work that really happened. */
    TORIRS_PERF_CTR_UITREE_APPLY_HOOK_SKIP,
    TORIRS_PERF_CTR_UITREE_APPLY_OTHER,
    /** Applies whose value was already the one being written, so the node was
     *  left clean and does not re-emit or repaint. Counted across GEO, CONTENT
     *  and OTHER together — the interesting ratio is against their sum. */
    TORIRS_PERF_CTR_UITREE_APPLY_NOCHANGE,
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
    /** Time in `CS2VM2_Free` on the way back to the pool. The pool parks
     *  torn-down blocks, not warm VMs, so this is the other half of the
     *  per-script fixed cost that a 100% hit rate does not remove. */
    TORIRS_PERF_CTR_CS2_VM_RELEASE_NS,
    TORIRS_PERF_CTR_CS2_FRAME_POOL_HIT,
    TORIRS_PERF_CTR_CS2_FRAME_POOL_MISS,
    /** Frame pushes (a script entry or a gosub), and the bytes those pushes
     *  zero. Separate from the pool counters above, which count block
     *  *allocation* — a warm thread reuses one block across many pushes, so the
     *  pool hit rate says nothing about how often a frame is cleared. */
    TORIRS_PERF_CTR_CS2_FRAME_PUSH,
    TORIRS_PERF_CTR_CS2_FRAME_CLEAR_BYTES,
    /** Bytes handed to a frame's locals buffers, counted where they are grown.
     *  Buffers never shrink and pooled blocks keep theirs, so the run total is
     *  also the high-water footprint of every locals array in the VM — the
     *  number to compare against the 1.51 MB the fixed CS2VM_MAX_LOCALS arrays
     *  used to reserve. */
    TORIRS_PERF_CTR_CS2_FRAME_LOCALS_BYTES,

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

    /** Presents that took the 1:1 BitBlt because the window matches the canvas. */
    TORIRS_PERF_CTR_PRESENT_BLIT_1TO1,
    /** Presents that took StretchBlt because the window does not. The scaler is
     *  the expensive one and COLORONCOLOR vs HALFTONE differ by an order of
     *  magnitude, so target 12 has to know which of these it is looking at. */
    TORIRS_PERF_CTR_PRESENT_BLIT_STRETCH,
    /** Pixels the blit moved, summed over the run: the bandwidth the present
     *  path costs, independent of how fast this machine's GDI is. */
    TORIRS_PERF_CTR_PRESENT_BLIT_PIXELS,
    /** Pixels the letterbox bars cleared. Zero when the window fits. */
    TORIRS_PERF_CTR_PRESENT_FILL_PIXELS,

    /** Components the fixed-chrome strip measure ran the parent-chain
     *  visibility walk on. The measure scans the whole tree every frame
     *  (`uitree_components` is that number); this counts how many survive the
     *  geometry rejects and pay for the ancestor walk. It read 1 per component
     *  before the rejects were ordered ahead of it, and reads 2 per frame now.
     *  If it ever climbs back towards the tree size, the ordering has been
     *  undone. */
    TORIRS_PERF_CTR_CHROME_STRIP_VISCHECK,

    /** DIAGNOSTIC (Opt 11 scoping): how the emit list moves frame to frame.
     *  `same` counts frames whose descriptor array is byte-identical to the
     *  previous frame's, `diff` the rest, and `desc_diff` the number of
     *  individual descriptors that changed, summed over the run — divide by
     *  `emit_list_diff` for the average churn of a frame that moved. Together
     *  they say whether retaining the list is worth anything before any
     *  invalidation machinery exists to retain it with. The comparison itself
     *  costs a memcmp + memcpy of the whole array per frame, so these three
     *  inflate the `emit` stage; read the counters, not the clock, in a build
     *  that carries them. */
    TORIRS_PERF_CTR_EMIT_LIST_SAME,
    TORIRS_PERF_CTR_EMIT_LIST_DIFF,
    TORIRS_PERF_CTR_EMIT_DESC_DIFF,
    /** Opt 11 retention, shadow mode: frames the dirty_gen + hover gate
     *  called quiet AND whose emit list did in fact repeat — the skips that
     *  would have been correct, i.e. the size of the prize. */
    /** Opt 11 retention, shadow mode: dirty_gen bumps per frame — how much
     *  UI damage a quiet frame actually claims. */
    TORIRS_PERF_CTR_EMIT_DIRTY_BUMPS,
    /** Opt 11 retention: bumps landing on a node the emit walk cannot reach
     *  because it or an ancestor is hidden — a closed interface still
     *  ticking its 3D model. */
    TORIRS_PERF_CTR_EMIT_DIRTY_UNREACHED,
    TORIRS_PERF_CTR_EMIT_DIRTY_MARK,
    TORIRS_PERF_CTR_EMIT_DIRTY_TOPO,
    TORIRS_PERF_CTR_EMIT_GEN_QUIET,
    /** Opt 11 retention, shadow mode: frames the gate called quiet whose
     *  list changed anyway — skips that would have served a stale list.
     *  This must be 0 before the skip may be turned on. */
    TORIRS_PERF_CTR_EMIT_GEN_UNSOUND,
    /** `TaskRunner_Step` calls inside one frame's settle. With the settle loop
     *  measured at ~293 us and its layout and followup limbs at 65 ns and 276 ns,
     *  everything is inside `ToriRS_TaskQueue_Run` — which holds both the VM and
     *  the queue scheduler. This number tells them apart without timing either:
     *  ~17 steps (one per script) means the interpreter is the cost and target 8
     *  comes back off the shelf; hundreds or thousands means the queue is being
     *  rescanned per yield and the fix is in the scheduler, not the VM. */
    TORIRS_PERF_CTR_TASK_STEPS,
    /** `task_run` calls, i.e. protothread resumes. A step runs the queue until
     *  it yields, so this is >= task_steps and is the divisor that says whether
     *  `task_queue_run` is a few expensive bodies or many cheap re-entries. */
    TORIRS_PERF_CTR_TASK_RESUMES,
    /** Resumes that ended the task (PT_ENDED or PT_EXITED). */
    TORIRS_PERF_CTR_TASK_ENDS,
    /** Reached dirty marks raised by `UITreeAnim_Advance`, from bracketing it
     *  with `g_torirs_dirty_mark_seq`. Scripts were just measured at ~0.32 of
     *  the 27.43 marks a frame, so ~99% of the damage that holds target 11's
     *  retention gate at zero comes from somewhere outside the VM, and model
     *  rotation is the only mark site that runs unconditionally every frame.
     *  If this is ~27, the gate is defeated by animation — which is a *correct*
     *  dirty signal, and target 11's skip is unreachable while any rotating
     *  model is on screen rather than merely unimplemented. */
    TORIRS_PERF_CTR_ANIM_MARKS,
    /** Frames where the emit retention gate held on the tree term / the hover
     *  term, counted separately. The gate's third term is
     *  `UITree.layout_resolve_seq`, which has no counter of its own; the frames
     *  it actually retains are `emit_retained`. */
    TORIRS_PERF_CTR_GATE_TREE_QUIET,
    TORIRS_PERF_CTR_GATE_HOVER_QUIET,
    /** Frames where the emit walk was skipped and last frame's list reused. */
    TORIRS_PERF_CTR_EMIT_RETAINED,
    /** Frames the gate called quiet but that were walked anyway because the
     *  previous list carried same-frame host pointers. See
     *  UITreeEmitBuffer.volatile_refs. */
    TORIRS_PERF_CTR_EMIT_RETAIN_BLOCKED,

    /** Render commands executed, by class. The divisor for the `r_*` stages,
     *  and on its own the answer to whether the 2D chrome is a few big blits
     *  or thousands of small ones. (Measured: thousands of small ones --
     *  1,674.9 commands per frame, 1,510.3 of them models.) */
    TORIRS_PERF_CTR_R_CMDS,
    TORIRS_PERF_CTR_R_CMDS_MODEL,
    TORIRS_PERF_CTR_R_CMDS_SPRITE,
    TORIRS_PERF_CTR_R_CMDS_FONT,
    TORIRS_PERF_CTR_R_CMDS_RECT,

    /** What the three model gates do with those submissions. `r_cmds_model` is
     *  what the painter handed the renderer; these say how far each one got.
     *
     *  A model rejected by FastCull/AabbCull (toridraw_render.u.c:2638) costs a
     *  rotation-invariant sphere test and an 8-point AABB and nothing else. One
     *  that passes pays a transform for every vertex whether or not a single
     *  pixel survives. One that then sorts to zero faces paid that projection
     *  in full and rasterizes nothing.
     *
     *  So `r_model_culled / r_cmds_model` is the answer to "is the culling
     *  working", and `r_model_faces / r_model_drawn` is what the rasterizer is
     *  actually being asked to fill. Counted here rather than inside toridraw
     *  because the verdicts are already in hand at the soft3d call site. */
    TORIRS_PERF_CTR_R_MODEL_CULLED,
    TORIRS_PERF_CTR_R_MODEL_DRAWN,
    TORIRS_PERF_CTR_R_MODEL_SORT_EMPTY,
    TORIRS_PERF_CTR_R_MODEL_FACES,

    TORIRS_PERF_CTR_COUNT
};

#ifndef TORIRS_PERF_DISABLE

extern int g_torirs_perf_enabled;

/** Monotonic count of `UITree_CcCreate` calls, readable mid-run.
 *
 *  The perf counters are write-only from the caller's side, and target 7 needs
 *  to read this one *inside* a script to attribute the ~7.85 components a frame
 *  the client creates to the script that creates them. Bracketing a RunScript
 *  call with two reads of this gives that attribution for free.
 *
 *  Unconditional rather than gated: it is one increment on a path that runs
 *  ~7.85 times a frame, which is unmeasurable, and gating it behind
 *  TORIRS_PERF would make the number unavailable in exactly the profile runs
 *  that want it. */
extern uint64_t g_torirs_cc_create_seq;

/** Monotonic count of *reached* `UITree_MarkNodeDirty` calls — the ones that
 *  bump `dirty_gen` — readable mid-run, same rationale as the counter above.
 *
 *  This is the number target 7 actually needs. The create attribution proved the
 *  five per-frame hot scripts create nothing, so whatever keeps target 11's
 *  retention gate from ever firing is a property write, and the `cc_*` setters
 *  already compare-before-mark: every increment here is a write that genuinely
 *  changed a value. Bracketing a RunScript call with two reads of this says
 *  which script is doing damage the emit walk can see. */
extern uint64_t g_torirs_dirty_mark_seq;

/** Monotonic count of *topology* `dirty_gen` bumps — link, unlink, allocate,
 *  hide. These do NOT go through `UITree_MarkNodeDirty`, so the sequence above
 *  misses them entirely, which is how script 4725 measured at zero marks while
 *  skipping it took `gate_tree_quiet` from 0 to 1,944. Bracket a RunScript call
 *  with two reads of this to close that gap. */
extern uint64_t g_torirs_dirty_topo_seq;

/** Running total of script-body nanoseconds (outermost bodies only). Bracket
 *  any region with two reads of this to get the script share inside it. */
extern uint64_t g_torirs_cs2_script_ns;

/** `uitree.c` line of the most recent topology bump. Only useful where the
 *  bracketed delta is 1, which is exactly the case that matters here. */
extern int g_torirs_dirty_topo_line;

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

/** Independent gauge samplers. Each keeps its own arm, so one site consuming a
 * sample cannot starve another that runs later in the same tick. */
enum TorirsPerfGaugeSite
{
    /** UITreeIfaceStats_SampleGauges — component/hook census. */
    TORIRS_PERF_GAUGE_SITE_IFACE_STATS = 0,
    /** uitree_perf_snapshot — component array and layout free-list length. */
    TORIRS_PERF_GAUGE_SITE_UITREE_LAYOUT,
    TORIRS_PERF_GAUGE_SITE_COUNT
};

/**
 * True when a COUNT_SET gauge sample is wanted from `site`, and consumes that
 * site's request.
 *
 * Gauges are last-sample-wins: a window flush and the exit report read one
 * value each, so resampling them on every logic tick is pure measurement
 * overhead — and the samplers behind them (UITreeIfaceStats_SampleGauges, the
 * layout free-list count) walk the whole interface tree. On the XP baseline
 * that cost roughly 6% of the CS2 time the harness was supposed to be
 * measuring. Sampling fires on the last frame of each window, plus a slow
 * fallback cadence, so the numbers a report quotes stay fresh.
 */
int TorirsPerf_GaugeSampleDue(enum TorirsPerfGaugeSite site);

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

/* Carries the script nanoseconds accrued since `mark` into `cs2_script_in`, and
 * returns 1 so it can drive a for-increment. See TORIRS_PERF_SCOPE_CS2. */
static inline int
TorirsPerf_Cs2ScriptCarry(uint64_t mark)
{
    TORIRS_PERF_CARRY(TORIRS_PERF_STAGE_CS2_SCRIPT_IN, g_torirs_cs2_script_ns - mark);
    return 1;
}

/* `TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)` plus the script-share carry. Every
 * entry to the CS2 stage must go through this rather than the bare scope:
 * `cs2` minus `cs2_script_in` is exact only if all four brackets carry, and one
 * that forgets reports its scripts as machinery. */
#define TORIRS_PERF_SCOPE_CS2()                                                \
    for( uint64_t _cs2_s0 = g_torirs_cs2_script_ns, _cs2_i = 0; _cs2_i < 1;    \
         _cs2_i += (uint64_t)TorirsPerf_Cs2ScriptCarry(_cs2_s0) )              \
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_CS2)

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
#define TORIRS_PERF_SCOPE_CS2() for( int _tpc_done = 0; !_tpc_done; _tpc_done = 1 )
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
enum TorirsPerfGaugeSite
{
    TORIRS_PERF_GAUGE_SITE_IFACE_STATS = 0,
    TORIRS_PERF_GAUGE_SITE_UITREE_LAYOUT,
    TORIRS_PERF_GAUGE_SITE_COUNT
};
static inline int TorirsPerf_GaugeSampleDue(enum TorirsPerfGaugeSite site)
{
    (void)site;
    return 0;
}

#endif /* TORIRS_PERF_DISABLE */

#ifdef __cplusplus
}
#endif

#endif /* TORIRS_PERF_H */
