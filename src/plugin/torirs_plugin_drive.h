#ifndef TORIRS_PLUGIN_DRIVE_H
#define TORIRS_PLUGIN_DRIVE_H

/*
 * quest-driver: the engine seam the test-only `api.drive` Lua module is built
 * out of.  docs/QUEST_DRIVER_PLAN.md is authoritative on mechanism and
 * docs/ARCHITECT.md on who owns which file; this header is the contract the
 * two halves meet at, and it is the ONE file every builder reads.
 *
 * Three rules hold everywhere below.
 *
 *   1. Every verb answers with an `enum DriveResult`.  `DRIVE_OK` is the only
 *      success; the rest name why not, and a Lua verb returns
 *      (DriveResultName(result), detail).
 *   2. Nothing here is reached in an ordinary build.  The module registers
 *      only when ContentTest_Enabled() is true, and every definition in the
 *      driver's .c files sits inside the same
 *      `#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER` gate
 *      src/game/content_test.c uses -- the driver talks to the embedded
 *      server in-process and has nothing to say without one.
 *   3. A contract violation asserts (CLAUDE.md).  A NULL App, a NULL out
 *      pointer, a negative capacity are bugs in the driver, not runtime
 *      states; `not_found` is for a name the CONTENT does not have.
 *
 * Layering: these files include BOTH app.h and lua.h.  That is deliberate and
 * it is why they are listed in the client's SRCS with the -I$(LUA_DIR) rule
 * torirs_plugin_lua.c gets, and why none of them ever appears in a plugin-host
 * unit-test link line.  The plugin HOST stays app-free; the quest DRIVER is a
 * client-only, test-only module that happens to live beside it.
 */

#include <stdint.h>

struct App;
struct lua_State;
struct ToriRSServer;
struct ToriRSServerEmbed;
struct ToriRS_CmdBus;

/* ------------------------------------------------------------------ result */

/*
 * The verb result set, fixed by the design doc.  Do not add a member without
 * changing docs/QUEST_DRIVER_DESIGN.md: a test reads these strings.
 */
enum DriveResult
{
    DRIVE_OK = 0,
    DRIVE_TIMEOUT,
    DRIVE_NOT_FOUND,
    DRIVE_REFUSED,
    DRIVE_COVERED,
    DRIVE_NO_ROW,
    DRIVE_NOT_VISIBLE,
    DRIVE_CLOSED,
    DRIVE_UNSUPPORTED,
    DRIVE_RESULT_COUNT
};

/** "ok", "timeout", ... .  Never NULL: an out-of-range value asserts. */
char const* DriveResultName(enum DriveResult result);

/* ------------------------------------------------------------------- events */

/*
 * Drive events.  One fixed ring on struct App, appended by App_DriveEvent()
 * from the stamp sites marked `DRIVE_STAMP:` in the tree (grep for them), read
 * by cursor by the scheduler.  The ring NEVER allocates and never blocks a
 * stamp: a full ring drops its oldest entry and a reader whose cursor fell off
 * the end is told so (DRIVE_REFUSED) rather than silently skipping events,
 * because a skipped edge is a test that hangs for a reason nobody can see.
 *
 * Payload per kind -- a, b, c, d are otherwise 0:
 *
 *   DRIVE_EVENT_SUB_OPENED      a=target_uid  b=interface_id  c=type
 *   DRIVE_EVENT_SUB_MOUNTED     a=target_uid  b=interface_id  c=type
 *   DRIVE_EVENT_SUB_CLOSED      a=target_uid  b=old group_id
 *   DRIVE_EVENT_CHAT_OPENED     a=component_id                     (dat1)
 *   DRIVE_EVENT_SLOT_MOUNTED    a=owner_index b=iface_id           (dat1)
 *   DRIVE_EVENT_RESUME_ANSWERED a=component_id
 *   DRIVE_EVENT_VARP_CHANGED    a=varp_id     b=value
 *   DRIVE_EVENT_INV_CHANGED     a=container_id
 *   DRIVE_EVENT_OBJ_ADDED       a=obj_id  b=count  c=tile_x  d=tile_z
 *   DRIVE_EVENT_OBJ_REMOVED     a=obj_id  b=0      c=tile_x  d=tile_z
 *   DRIVE_EVENT_MAP_FLAG        a=-1 when cleared, else a=tile_x b=tile_z
 *   DRIVE_EVENT_CHAT_MESSAGE    a=type    b=message serial (RS_ChatMessage)
 *   DRIVE_EVENT_SERVER_TICK     a=world cycle
 *   DRIVE_EVENT_NPC_SPAWN       a=npc slot  b=npc_id
 *   DRIVE_EVENT_NPC_DESPAWN     a=npc slot  b=npc_id
 *   DRIVE_EVENT_NPC_RETYPE      a=npc slot  b=npc_id  c=base_npc_id
 *   DRIVE_EVENT_INV_PACKET      a=container_id                     (dat1)
 *   DRIVE_EVENT_NPC_SEQ         a=npc SERVER slot (DriveNpcRow.slot, not
 *                                 the world index NPC_SPAWN carries)
 *                               b=seq id (-1 = the server stopped it)
 *                               c=server tick (world cycle / 30) it arrived
 *   DRIVE_EVENT_NPC_FACE        a=npc SERVER slot  b=tile x  c=tile z
 *                                 (absolute; the FACE_COORD op's wire
 *                                 half-tiles halved, so a sized square
 *                                 reads as its centre tile)
 *                               d=server tick (world cycle / 30) it arrived
 *
 * The level of a tile payload rides in `c`'s high half nowhere: pass level in
 * `d` only where the table above says so.  If a kind needs a fifth number,
 * split it into two events rather than widening the record -- the ring is on
 * struct App and every byte is paid for by every client.
 */
enum App_DriveEventKind
{
    DRIVE_EVENT_NONE = 0,
    DRIVE_EVENT_SUB_OPENED,
    DRIVE_EVENT_SUB_MOUNTED,
    DRIVE_EVENT_SUB_CLOSED,
    DRIVE_EVENT_CHAT_OPENED,
    DRIVE_EVENT_SLOT_MOUNTED,
    DRIVE_EVENT_RESUME_ANSWERED,
    DRIVE_EVENT_VARP_CHANGED,
    DRIVE_EVENT_INV_CHANGED,
    DRIVE_EVENT_OBJ_ADDED,
    DRIVE_EVENT_OBJ_REMOVED,
    DRIVE_EVENT_MAP_FLAG,
    DRIVE_EVENT_CHAT_MESSAGE,
    DRIVE_EVENT_SERVER_TICK,
    DRIVE_EVENT_NPC_SPAWN,
    DRIVE_EVENT_NPC_DESPAWN,
    DRIVE_EVENT_NPC_RETYPE,
    DRIVE_EVENT_INV_PACKET,
    DRIVE_EVENT_NPC_SEQ,
    DRIVE_EVENT_NPC_FACE,
    DRIVE_EVENT_KIND_COUNT
};

/** "sub_mounted", ... .  The name a Lua await predicate matches on.  Never
 *  NULL; an out-of-range kind asserts. */
char const* DriveEventKindName(enum App_DriveEventKind kind);

/** -1 when `name` is not an event kind.  A test's typo in an await
 *  descriptor is a legitimate runtime answer, not a contract violation. */
int DriveEventKindFromName(char const* name);

#define APP_DRIVE_RING_CAPACITY 512

struct App_DriveEvent
{
    /** 1-based and monotonic for the life of the process.  0 is "no event";
     *  a cursor of 0 means "everything the ring still holds". */
    uint32_t serial;
    int32_t kind; /**< enum App_DriveEventKind */
    int32_t cycle; /**< world cycle at the stamp, for deadline arithmetic */
    int32_t a, b, c, d; /**< per-kind, per the table above */
};

/* A projectile as its MAP_PROJANIM packet said it, stamped with the world
 * cycle the packet was applied on. The world's projectile entity is spawned
 * by a task that first awaits the spotanim's assets (Task_AppSpawn), so a
 * first-seen spotanim's projectile reached the drive rows a tick late and a
 * tick into its flight -- where scriptrun, which keeps the packet, listed it
 * in the packet's tick (2026-10-09: the two tick logs parted on an urn one
 * lane carried and the other did not). The drive's projectile rows come
 * from these records, with scriptrun's arithmetic. Absolute tiles. */
#define APP_DRIVE_PROJECTILE_CAP 64
struct App_DriveProjectile
{
    int32_t src_x, src_z, dst_x, dst_z, level;
    int32_t spotanim, target, start_delay, end_delay;
    int32_t cycle; /**< world cycle at the apply */
};
/* A loc change as its packet applied it (LOC_ADD_CHANGE; LOC_DEL is loc_id
 * -1), held until the world's scenery shows it. The world applies a change
 * through a task that first awaits the new loc's models (App_WorldLocChangeOps,
 * the reference's changeLocAvailable gate), so a first-seen loc type reached
 * api_drive.locs a tick late -- where scriptrun, which keeps the packet,
 * listed it in the packet's tick (2026-10-09: Maiden's first blood trail, one
 * tick apart, and the freezer's plan parted). The drive's loc rows answer
 * these tiles from the packet. Absolute tiles; `layer` is
 * World_LocShapeToLayer(shape); `base_x/z` the scene base it was applied in. */
#define APP_DRIVE_LOC_CHANGE_CAP 256
struct App_DriveLocChange
{
    int32_t x, z, level, layer;
    int32_t loc_id, shape, angle;
    int32_t base_x, base_z;
    /* The loc lane ticket of the change this record notes (App.loc_lane_
     * enqueued as the change was queued). The record is held at least until
     * the lane has released it (App.loc_lane_applied), so an add and the
     * delete behind it on one tile and layer cannot retire on the scene's
     * empty layer while the add has yet to land. */
    uint32_t lane_ticket;
};
/* A map graphic as its MAP_ANIM packet said it, stamped with the world cycle
 * the packet was applied on. The world's graphic entity waits on its
 * spotanim's assets the way a projectile does, so a first-seen graphic reached
 * api_drive.spotanims a tick late -- where scriptrun, which keeps the packet,
 * listed it in the packet's tick (2026-10-10: Bloat's falling flesh, seen a
 * tick apart, and the relay's two lanes walked apart in his room). The
 * drive's spotanim rows come from these records, with scriptrun's arithmetic:
 * listed for APP_DRIVE_MAP_ANIM_LIFE cycles from the apply (scriptrun_core.c
 * retires a floor graphic 40 ticks after it lands). Absolute tiles. */
#define APP_DRIVE_MAP_ANIM_CAP 1024
#define APP_DRIVE_MAP_ANIM_LIFE (40 * 30)
struct App_DriveMapAnim
{
    int32_t x, z, level, spotanim;
    int32_t cycle; /**< world cycle at the apply */
};
struct App_DriveRing
{
    struct App_DriveProjectile projectiles[APP_DRIVE_PROJECTILE_CAP];
    int projectile_count;
    struct App_DriveMapAnim map_anims[APP_DRIVE_MAP_ANIM_CAP];
    int map_anim_count;
    struct App_DriveLocChange loc_changes[APP_DRIVE_LOC_CHANGE_CAP];
    int loc_change_count;
    /* Bumped whenever loc_changes is written or retired: the key the route
     * and plan collision view (torirs_plugin_drive_ui.c) is cached on. */
    uint32_t loc_change_serial;
    /* A server tick is HALF-APPLIED: the exec runner has started on a packet
     * of a tick whose fence (SERVER_TICK_END, or PLAYER_INFO on a revision
     * with none) has not run yet. Packets apply over several frames and the
     * driver pumps every frame, so without this a level read the world with
     * part of a tick in it -- a cheat's reply, the instance's state -- a tick
     * before scriptrun, whose bots only ever run on whole ticks (2026-10-09:
     * the Maiden leader's entry two ticks apart). The driver waits it out
     * (drive_pump_once). `batch_open_cycle` is the logic cycle it opened on,
     * for the lost-fence backstop. */
    int batch_open;
    int batch_open_cycle;
    struct App_DriveEvent entries[APP_DRIVE_RING_CAPACITY];
    /** Serial of the newest entry; 0 before the first stamp. */
    uint32_t newest_serial;
    /** Serial of the oldest entry the ring still holds; 0 while empty. */
    uint32_t oldest_serial;
    /** Where the next append lands. */
    int write_index;
    /** How many entries are live, capped at APP_DRIVE_RING_CAPACITY. */
    int count;
    /** Entries dropped because a reader never came back.  Reported once by
     *  the scheduler; a climbing number means the pump is not running. */
    uint32_t dropped;
};

/*
 * Stamp one event.  Cheap enough to sit on a packet path: a bounds check, a
 * struct store and two counters, with no allocation and no logging.
 *
 * `app` may be NULL at exactly one kind of site -- a stamp reached during
 * teardown -- so every stamp site passes the app it already has and this
 * function asserts it.  Called unconditionally: the ring is on struct App in
 * every build, because a stamp guarded by an env var is a stamp nobody
 * notices has rotted.  Owner: core-events.
 */
void App_DriveEvent(
    struct App* app,
    enum App_DriveEventKind kind,
    int32_t a,
    int32_t b,
    int32_t c,
    int32_t d);

/*
 * Read every entry newer than `after_serial` into `out`, newest LAST (the
 * order they were stamped in).
 *
 * Returns DRIVE_OK, or DRIVE_REFUSED when `after_serial` is older than the
 * oldest surviving entry -- the caller has missed events and must say so
 * rather than pretend.  `*out_count` is the number written, which can be
 * fewer than the number of entries newer than `after_serial` when `cap`
 * truncates the batch.  `*out_serial` is the last serial written into this
 * batch (not the ring's newest), so a truncated read is followed by a
 * re-poll that resumes exactly where it stopped instead of skipping the
 * entries `cap` cut off.  Owner: core-events.
 */
/* Record a MAP_PROJANIM as applied (rs_gameproto_exec.c), absolute tiles. */
void App_DriveProjectileNote(
    struct App* app, int src_x, int src_z, int dst_x, int dst_z, int level, int spotanim,
    int target, int start_delay, int end_delay);
/* Record a MAP_ANIM as applied (rs_gameproto_exec.c), absolute tiles. */
void App_DriveMapAnimNote(struct App* app, int x, int z, int level, int spotanim);
/* Record a LOC_ADD_CHANGE (or a LOC_DEL, loc_id -1) as applied
 * (rs_gameproto_exec.c), scene tiles at the loc's cache level: it replaces any
 * pending change for the same tile and layer, as the packet does. */
void App_DriveLocChangeNote(
    struct App* app, int scene_x, int scene_z, int level, int loc_id, int shape, int angle);

enum DriveResult App_DriveEventsRead(
    struct App* app,
    uint32_t after_serial,
    struct App_DriveEvent* out,
    int cap,
    int* out_count,
    uint32_t* out_serial);

/* ---------------------------------------------------------------- lifecycle */

/*
 * Install the module.  Called once from the app's plugin boot, before any
 * script is compiled, and only when ContentTest_Enabled().  It remembers `app`
 * (the driver is a singleton by construction: one client, one quest) and hands
 * torirs_plugin_lua.c the test-module installer.
 *
 * A build without EMBED_SERVER compiles this to nothing.  Owner: core-scheduler.
 */
void PluginDrive_Init(struct App* app);

/** The App the driver was initialised with.  NULL before PluginDrive_Init,
 *  which every drive seam asserts against -- a verb reached with no app is a
 *  registration bug, not a runtime state. */
struct App* PluginDrive_App(void);

/**
 * The embedded server's world, or NULL when this process has none.
 *
 * NULL is a runtime state, not a bug: a socket-server run loads the driver
 * with no in-process server at all (DriveCore_Cheat answers `unsupported` for
 * exactly that case), so every caller tests it rather than asserting on it.
 *
 * It exists so a seam that does NOT live in torirs_plugin_drive.c can read the
 * server's own copy of something the client cannot hold -- see
 * DriveState_VarpContent.  Owner: core-scheduler (the embed handle is its).
 */
struct ToriRSServer* PluginDrive_EmbedWorld(void);

/* The server tick this client is on (lua_drive_tick's answer), and the
 * server tick a client cycle stamp belongs to (torirs_plugin_drive.c). */
int PluginDrive_ServerTick(void);
int PluginDrive_ServerTickOfCycle(int cycle);

/** Shutdown: drop the coroutine, close the ledger, forget the app. */
void PluginDrive_Shutdown(void);

/*
 * The quest script the process was asked to run (TORIRS_QUEST_SCRIPT), or
 * NULL.  A run with no quest script still loads the driver plugin -- that is
 * the shape the load gate uses -- it simply never creates a thread.
 *
 * Always NULL in an on-demand client (PluginDrive_OnDemand below): there the
 * script is the one api.drive.start named, and it is the driver's own state,
 * not this process's -- so content_test.c never puts a watched client on the
 * quest-mode virtual clock.  Owner: core-scheduler.
 */
char const* PluginDrive_QuestScriptPath(void);

/*
 * ON DEMAND (raid seam23, script_start_on_demand): TORIRS_DRIVE_ON_DEMAND=1.
 *
 * A client a person launches and watches -- the Scripts tab
 * (script/plugins/script_runner.lua) -- installs `api.drive` like a test run
 * does, but nothing starts at world-ready: api.drive.start(path, session_dir)
 * starts an already-wrapped script file (run.py's wrapper; the tab uses
 * api.drive.play since seam24) on the next
 * frame, api.drive.stop() ends it at its next yield, api.drive.status() reads
 * where it is.  A finished or stopped script does NOT end the client
 * (PluginDrive_Finished answers 0): the driver tears the coroutine down at the
 * next frame boundary, reloads the quest-driver plugin (a fresh Lua state, so
 * no per-script state of any part outlives its run), and is idle again.
 *
 * Without the knob every one of these is inert and every test run is what it
 * was.  Read once.  Owner: core-scheduler.
 */
int PluginDrive_OnDemand(void);

/*
 * THE SCRIPTS TAB (raid seam24, scripts_tab_every_script), in the same
 * on-demand client: api.drive.tests([refresh]) reads the scripts manifest
 * (task_plugin_io.h TESTS_MANIFEST_DEFAULT_PATH, `[test:<id>]` sections) as a
 * SCRIPT item through the IO layer, as the plugin host reads
 * plugins/plugins.ini; api.drive.play({id, source, fixture, ...}) reads the
 * test's source and fixture the same way -- again on every Play, which is the
 * hot reload -- writes the fixture as the save of a fresh account and runs
 * the RAW test file through QD.core_run_test (quest_driver/core.lua), which
 * logs out, logs in as that account and does what run.py's wrapper does.
 * api.drive.status gains play, id, suite, account, leg, legs, refusal.
 * Without the knob both refuse. Owner: core-scheduler.
 */

/*
 * Defined in torirs_plugin_lua.c beside PluginLua_ThreadCreate, declared here
 * because the driver is its one caller: the same one-coroutine thread, whose
 * bootstrap is `t.core_run_test(loader, options)` instead of
 * `loader().run(t)` -- a raw test file has no wrapper to call. Resume it with
 * (QD_ROOT, options): two arguments after the loader.
 */
struct lua_State* PluginLua_TestThreadCreate(
    char const* plugin_name,
    char const* chunk_name,
    char const* source,
    int source_len,
    int* out_registry_ref);

/*
 * Once a frame from main.c, outside every plugin callback, ONLY in an
 * on-demand client that is not also a content-test run.  `embed` is the
 * in-process world (NetTransport_TestClock, fed by main.c with a clock that
 * reproduces the transport's own) and `bus` the frame's command bus: the two
 * handles content_test.c hands a test run's driver every frame.  Either may be
 * NULL (a socket-server run has no embed).  Owner: core-scheduler.
 */
void PluginDrive_OnDemandHandOver(struct ToriRSServerEmbed* embed, struct ToriRS_CmdBus* bus);

/*
 * Once a frame from main.c in an on-demand client, outside every plugin
 * callback: ends a stopped script (its ledger gets the `run.unfinished` row
 * and SUMMARY an unfinished run's does), releases a finished one's coroutine
 * and reloads the quest-driver plugin.  Reloading a plugin from inside a
 * plugin's own callback is not a state the host supports, which is why this
 * is main.c's call and not the pump's.  Owner: core-scheduler.
 */
void PluginDrive_FrameBoundary(void);

/*
 * Does the driver want the content-test virtual clock stepped this frame?
 *
 * src/game/content_test.c asks this instead of waiting on its request/response
 * mailbox when TORIRS_QUEST_SCRIPT is set: a quest test drives itself and the
 * mailbox would deadlock it.  Answers 0 while a screenshot is outstanding, so
 * the capture is not raced by the next step.  Owner: core-scheduler.
 */
int PluginDrive_ClockWantsStep(struct App* app);

/* --------------------------------------------------- manifest concatenation */

/*
 * The sandbox has no `require`, so the driver ships as one chunk assembled
 * from several files.  src/plugin/task_plugin_io.c asks these three questions
 * for every manifest entry it is about to load; everything that is not the
 * driver answers 0 parts and takes the ordinary single-file path.
 *
 * Order matters and is fixed here: the parts define the verb namespaces, the
 * manifest's own `source=` comes LAST and is the only file with a top-level
 * `return plugin`.  A part with a top-level return would truncate the chunk.
 *
 * Owner: core-scheduler.
 */
int PluginDrive_ScriptPartCount(char const* plugin_name);
char const* PluginDrive_ScriptPartPath(char const* plugin_name, int index);

/** Accumulate one part.  `PluginDrive_ComposeReset` before the first part,
 *  then one Append per part and per the entry source, then Take.  Take hands
 *  over the buffer and its length and resets; the caller frees it. */
void PluginDrive_ComposeReset(void);
void PluginDrive_ComposeAppend(char const* data, int length);
char* PluginDrive_ComposeTake(int* out_length);

/* ------------------------------------------------------------ Lua interface */

/*
 * Each verb group owns ONE of these.  It defines its own
 * `static struct LuaFn const LUA_DRIVE_<GROUP>_FNS[]` (the inventory test
 * reads those arrays and pins them against plugin_api.meta.lua) and appends it
 * to the table on top of the stack with PluginLua_AppendModule.
 *
 * torirs_plugin_drive.c calls these in a fixed order to build the one flat
 * `api.drive` table.  NAMES MUST NOT COLLIDE across groups -- the inventory
 * test fails a duplicate, and the later registration would silently win.
 */
void PluginDriveCore_RegisterLua(struct lua_State* L, void* script); /* core-scheduler */
void PluginDriveState_RegisterLua(struct lua_State* L, void* script); /* core-state */
void PluginDriveChat_RegisterLua(struct lua_State* L, void* script); /* verbs-chat */
void PluginDriveRead_RegisterLua(struct lua_State* L, void* script); /* verbs-read */
void PluginDrivePointer_RegisterLua(struct lua_State* L, void* script); /* verbs-pointer */
void PluginDriveUi_RegisterLua(struct lua_State* L, void* script); /* verbs-ui */
/* waves seam los_and_pack: api.drive.server_los / server_npc_pack
 * (torirs_plugin_drive_los.c, over torirs_server_los_query.c). */
void PluginDriveLos_RegisterLua(struct lua_State* L, void* script);
/* waves seam npc_record_reads: api.drive.npc_record / seq_length / npc_pose
 * (torirs_plugin_drive_record.c): an npc type's client and server record, a
 * sequence's length as the client steps it, an npc's movement track. */
void PluginDriveRecord_RegisterLua(struct lua_State* L, void* script);

/*
 * Shared Lua argument helpers, defined in torirs_plugin_drive.c so six files
 * do not each grow their own.  Every one of them raises a Lua error on a bad
 * argument (luaL_error longjmps): a quest test passing a string where an id
 * belongs is a test bug and must stop the test, not return `not_found`.
 */
int PluginDrive_ArgInt(struct lua_State* L, int index);
int PluginDrive_ArgOptInt(struct lua_State* L, int index, int fallback);
char const* PluginDrive_ArgString(struct lua_State* L, int index);

/** Push (result_name, detail) and return 2 -- the shape EVERY verb returns.
 *  `detail` may be NULL for "no detail". */
int PluginDrive_PushResult(struct lua_State* L, enum DriveResult result, char const* detail);

/* ------------------------------------------------------------- core: awaits */

/*
 * The await primitive.  `drive.await(descriptor, deadline_ticks)` yields the
 * coroutine; the scheduler re-checks the descriptor on every matching drive
 * event and on every server tick, and resumes with (result, detail).
 *
 * EDGE + LEVEL: the descriptor's level predicate is evaluated once when the
 * await registers, and an already-true predicate returns DRIVE_OK without
 * yielding at all.  The one deliberate exception is the chat-message await,
 * which is scoped to messages newer than the serial at registration (plan
 * 5.7) -- that scoping is the descriptor's business, not the scheduler's.
 *
 * A descriptor is a Lua table:
 *   { event = "sub_mounted",        -- optional event kind to wake on
 *     match = function(ev) ... end, -- optional per-event edge test
 *     level = function() ... end,   -- optional level predicate
 *     note  = "what this waits for" }
 * with at least one of `match`/`level`.  An await with neither is a test bug
 * and raises.
 *
 * Deadlines are in SERVER TICKS, counted off DRIVE_EVENT_SERVER_TICK.  A
 * deadline of 0 means "level only, never yield".
 *
 * Owner: core-scheduler.  Everything else calls it from Lua.
 */

/* ------------------------------------------------- core: coroutine, in C */

/*
 * The scheduler.  The sandbox has no `coroutine` table and no `pcall` on
 * purpose (an instruction-budget error must not be catchable), so the thread
 * is created, hooked and resumed entirely from C.  The one hard-won rule:
 * lua_newthread copies the parent's hook ONLY at creation, so the step-budget
 * hook is re-armed on the COROUTINE's own lua_State at EVERY resume.
 *
 * These live in torirs_plugin_lua.c because they need `struct LuaScript` --
 * the budget, the fault routing and the api scope are all its business.
 * Declared here because the scheduler in torirs_plugin_drive.c is their only
 * caller.  See torirs_plugin_lua.h.  Owner: core-scheduler.
 */

/* ------------------------------------------------------------ core: verdict */

/*
 * The exit fence.  t.finish(code) sets a flag main.c checks in the same branch
 * as TORIRS_MAX_FRAMES, and writes <session>/result via a temp file and
 * rename, so a reader never sees a half-written verdict.
 *
 * PluginDrive_Finished() answers 0 until then; main.c must not test any other
 * driver state.  In an on-demand client (PluginDrive_OnDemand) it answers 0
 * always: t.finish writes the SUMMARY and returns the driver to idle, and the
 * client a person is watching stays up.  Owner: core-scheduler (the flag),
 * core-events (the main.c branch).
 */
void PluginDrive_Finish(int code);
int PluginDrive_Finished(int* out_code);

/* ================================================================= SEAMS ===
 *
 * Below: the C the Lua thunks call.  One block per owner.  A builder DEFINES
 * only the functions in its own block, in its own .c file, and CALLS anything
 * it needs from the others.  Nobody edits another block.
 *
 * Signature conventions, for all of them:
 *   - `app` is never NULL (assert it).
 *   - an `out_*` pointer is never NULL when the verb can answer (assert it).
 *   - ids are engine ids: component ids are packed (iface<<16|child) on the
 *     cache lane and flat on dat1, exactly as the tree stores them, and no
 *     driver code may spell either one as a literal.
 * ========================================================================= */

/* ------------------------------------------------- core-state: var/inv/etc */

enum DriveSymbolKind
{
    DRIVE_SYMBOL_NPC = 0,
    DRIVE_SYMBOL_OBJ,
    DRIVE_SYMBOL_LOC,
    DRIVE_SYMBOL_COMPONENT,
    DRIVE_SYMBOL_INTERFACE,
    DRIVE_SYMBOL_VARP,
    DRIVE_SYMBOL_VARBIT,
    DRIVE_SYMBOL_STAT,
    DRIVE_SYMBOL_INV,
    /* waves seam npc_record_reads: t.seq.length takes a seq symbol. */
    DRIVE_SYMBOL_SEQ,
    /* raid solvers name a boss's projectiles (t.raid.verzik_p2_solve: the
     * urnbomb and the Athanatos flight); scriptrun already served it. */
    DRIVE_SYMBOL_SPOTANIM,
    DRIVE_SYMBOL_KIND_COUNT
};

/** Content symbol -> id.  DRIVE_NOT_FOUND when this content pack has no such
 *  name, which is a legitimate answer a test can assert on.  Owner:
 *  core-scheduler (it is the trampoline every group uses). */
enum DriveResult DriveSymbol_Lookup(
    enum DriveSymbolKind kind, char const* name, int* out_id);

/** id -> content symbol, for the reverse reads (inv.slot returns a NAME so a
 *  test never compares numbers).  `out` is NUL-terminated on DRIVE_OK. */
enum DriveResult DriveSymbol_Name(
    enum DriveSymbolKind kind, int id, char* out, int out_cap);

struct DriveSkillSnapshot
{
    int level;
    int base_level;
    int experience;
    /** last_seen_level != 0.  A fresh account's table is NOT empty, so this
     *  is the only honest "has the server told us yet" test. */
    int stated;
};

/* core-state, src/plugin/torirs_plugin_drive_state.c */
enum DriveResult DriveState_Varp(struct App* app, int varp_id, int* out_value);
enum DriveResult DriveState_Varbit(struct App* app, int varbit_id, int* out_value);
enum DriveResult DriveState_VarbitBaseVarp(struct App* app, int varbit_id, int* out_varp);
/** The client's record of the server-authoritative value (varps.var_serv[]).
 *  var.expect exists to catch client != server, so it must not read the same
 *  number twice. */
enum DriveResult DriveState_VarpServer(struct App* app, int varp_id, int* out_value);
/** The varbit-width equivalent of DriveState_VarpServer: the bits
 *  VarPManager_GetVarbit would read, but out of var_serv[] instead of var[].
 *  var.expect needs this for a varbit-named quest var, and no existing engine
 *  accessor reads var_serv through the bit lens (VarPManager_GetVarbit only
 *  ever reads var[]) -- this mirrors its bit math rather than widening that
 *  file, which this group does not own (A9). */
enum DriveResult DriveState_VarbitServer(struct App* app, int varbit_id, int* out_value);
/**
 * The SERVER's own value for a varp, read in-process out of the embedded
 * server's active player, bypassing the client's varp table entirely.
 *
 * Why it has to exist.  DriveState_Varp and DriveState_VarpServer both read
 * `app->varps` -- the CLIENT's arrays -- and the client only ever holds a varp
 * the server actually transmitted.  `ToriRSServer_SendVarpSmall` refuses to
 * encode an id the connected client's varp array cannot address (the official
 * client treats it as a fatal protocol error), so a content-allocated varp
 * above the cache's highest id is never sent, the client's array never grows
 * to cover it, and BOTH of those reads answer `not_found` for the rest of the
 * run.  Measured on `rovingelves_quest` (id 6262 against a cache topping out
 * near 5704): client not_found, server not_found, at every stage, however long
 * a test polls -- while the server's own copy holds the right stage the whole
 * time.  This is the only channel that can read such a var, and quest.lua's
 * `quest.varp_complete` / `quest.stage` fall back to it when, and only when,
 * both client-side reads have answered `not_found`.
 *
 * NOT a replacement for the pair above: a var the client CAN hold must still
 * be graded on client-vs-server, which is the desync `var.expect` exists to
 * catch.  This read cannot see a desync at all -- it is the server's number
 * and nothing else -- so a caller that reaches for it says so in its row.
 *
 * `unsupported` when this process has no embedded server (a socket-server
 * run), `not_found` when nobody is logged in yet or the id is past
 * TORIRSSERVER_VARP_COUNT.
 */
enum DriveResult DriveState_VarpContent(struct App* app, int varp_id, int* out_value);
enum DriveResult DriveState_InvCount(
    struct App* app, int container_id, int obj_id, int* out_total);
enum DriveResult DriveState_InvSlot(
    struct App* app, int container_id, int slot, int* out_obj_id, int* out_count);
enum DriveResult DriveState_InvCapacity(struct App* app, int container_id, int* out_capacity);
enum DriveResult DriveState_Skill(
    struct App* app, int stat_index, struct DriveSkillSnapshot* out_snapshot);

struct DriveChatMessage
{
    int type;
    int serial;
    char sender[64];
    char text[256];
};

/** Newest first.  Reads app->chat.messages directly: App_NotifyChatMessage
 *  misses clan chat and the synthetic logout line, RS_Chat_AddMessage sees
 *  every line. */
enum DriveResult DriveState_Messages(
    struct App* app, struct DriveChatMessage* out, int cap, int* out_count);
/** The serial the next message will exceed.  An await registered now must not
 *  be satisfied by a line that was already in the ring. */
enum DriveResult DriveState_MessageSerial(struct App* app, int* out_serial);

/* --------------------------------------------------------------- verbs-chat */

/* verbs-chat, src/plugin/torirs_plugin_drive_chat.c */

/** The interface id mounted under chat_modal_host, or DRIVE_NOT_VISIBLE when
 *  nothing is.  Liveness is UITree_GroupPresent (D3), never
 *  interface_parents (CS2-only) and never modal_host_uid (D4: set on open,
 *  never cleared on close). */
enum DriveResult DriveChat_ModalGroup(struct App* app, int* out_interface_id);

/** Arm the resume-pausebutton seam for `component_id` -- the ONLY way a
 *  dialogue row is clicked (D1: a chatmenu row carries no cache op and a real
 *  click builds RESUME_PAUSEBUTTON with action_index -1).
 *  DRIVE_REFUSED when a resume is already outstanding. */
enum DriveResult DriveChat_Resume(struct App* app, int component_id);

/** UITree_PausePendingIndex: the component id a resume is outstanding on, or
 *  -1.  Compared BY COMPONENT ID, never by presence (D6: every sub-interface
 *  mount calls UITree_SetPausePending(tree, -1) unconditionally). */
enum DriveResult DriveChat_PausePending(struct App* app, int* out_component_id);

/** app->host.close_modal_requested.  Idempotent: already-clear answers
 *  DRIVE_OK immediately rather than hanging. */
enum DriveResult DriveChat_CloseModal(struct App* app);

/** The meslayer mode VarC.  Without it, digits typed for a count prompt go
 *  into ordinary chat and become a public message, so chat.count refuses to
 *  type until this answers. */
enum DriveResult DriveChat_MeslayerMode(struct App* app, int* out_mode);

/** Is the row actually armed -- the same RS_MINIMENU_EVENT_CLICK test
 *  rs_minimenu_build.c:1073 makes per row, through UIIfEventTable_Effective.
 *  DRIVE_NOT_VISIBLE when the component is not live. */
enum DriveResult DriveChat_ClickArmed(struct App* app, int component_id, int* out_armed);

#define DRIVE_CHAT_OPTIONS_ROW_MAX 5

struct DriveChatOptions
{
    char title[128];
    char rows[DRIVE_CHAT_OPTIONS_ROW_MAX][128];
    /** How many of `rows` are live, 0..DRIVE_CHAT_OPTIONS_ROW_MAX. */
    int row_count;
};

/*
 * chatmenu's title and rows are cc_create'd at runtime (chatbox_multi_init /
 * chatbox_multi_addoption): no .if section names them and no content-pack
 * symbol exists for them, so DriveUi_Component cannot resolve one -- only the
 * dialog_options_title / dialog_options_row_N revconfig roles can, which is
 * what this reads through. Kept in verbs-chat rather than routed through
 * api.widgets: nothing else in this chunk exposes api.widgets to a part file
 * other than core.lua (only api.drive is captured as a chunk-local upvalue in
 * quest_driver/core.lua's core_bind), and extending that is core-scheduler's
 * file -- see the report filed for this pass.
 *
 * DRIVE_NOT_VISIBLE unless rows 1 AND 2 both resolve -- the readiness floor
 * plan 5.5 sets, because RUNCLIENTSCRIPT is held to the tick fence
 * (app_cs2_flush.c:71) and the container can mount still empty for a frame;
 * row 1 alone can answer mid-rebuild.
 */
enum DriveResult DriveChat_Options(struct App* app, struct DriveChatOptions* out);

/** The live component id of chatmenu row `row` (1..DRIVE_CHAT_OPTIONS_ROW_MAX),
 *  resolved through the same dialog_options_row_N role DriveChat_Options
 *  reads text from -- the only way to name a cc_create'd row with no content
 *  symbol. DRIVE_NOT_VISIBLE when that row is not live right now. */
enum DriveResult DriveChat_OptionRow(struct App* app, int row, int* out_component_id);

/* --------------------------------------------------------------- verbs-read */

/* verbs-read, src/plugin/torirs_plugin_drive_read.c */

enum DriveWidgetModelKind
{
    DRIVE_WIDGET_MODEL_NONE = 0,
    DRIVE_WIDGET_MODEL_NPC,
    DRIVE_WIDGET_MODEL_OBJ,
    DRIVE_WIDGET_MODEL_MODEL
};

/*
 * What identity a component was told to show.  Reads struct App::if_heads[] --
 * the RAW identity the server sent on IF_SETNPCHEAD / IF_SETOBJECT, stored
 * synchronously by app_if_head_store long before the composite is built, so
 * there is no second await.  c->u.rs_model.gamecache_model_id cannot answer
 * "which npc is this" and must not be used.
 */
enum DriveResult DriveRead_WidgetModel(
    struct App* app, int component_id, int* out_kind, int* out_id);

/*
 * Text / presented / own-hidden reads for a live component.  Read
 * struct App::tree directly -- the same UITree_FindByComponentId +
 * UITree_NodeOrAncestorDisplayHidden + UITree_NodeNativeVisible walk
 * torirs_plugin_bridge.u.c's PLUGIN_WIDGET_TEXT/PLUGIN_WIDGET_STATE cases use
 * for the general plugin API -- rather than through api.widgets, which no
 * part file but core.lua can reach today (quest_driver/core.lua's
 * core_bind captures only api.drive as the chunk-local `api_drive` upvalue,
 * and extending that is core-scheduler's file). verbs-chat solved the same
 * problem the same way for chatmenu's rows (DriveChat_Options /
 * DriveChat_OptionRow, above) rather than route through api.widgets.
 *
 * DRIVE_NOT_FOUND when component_id does not resolve to a live node --
 * *out_text is "", out_presented / out_own_hidden are 0/1 (this header takes
 * no stdbool dependency; 1 = own_hidden true, so a not-found component reads
 * as hidden) even in that case, so a caller that only checks DRIVE_OK still
 * gets a safe default. *out_text is "" (not NULL) for a component that
 * resolves but is not a text node.
 */
enum DriveResult DriveRead_WidgetText(
    struct App* app, int component_id, char const** out_text);
enum DriveResult DriveRead_WidgetPresented(
    struct App* app, int component_id, int* out_presented);
enum DriveResult DriveRead_WidgetOwnHidden(
    struct App* app, int component_id, int* out_own_hidden);

/*
 * Where a live component is drawn: canvas pixels, the space
 * api_drive.mouse_move / mouse_button take, with scroll and an in-flight drag
 * folded in (UITree_NodeDrawnBounds, the read PLUGIN_WIDGET_BOUNDS answers
 * for the general plugin API). Layout is brought up to date first. A widget
 * drag (ui.lua QD.ui.drag) presses inside these bounds and releases where
 * the widget's top-left should land. DRIVE_NOT_FOUND when component_id does
 * not resolve to a live node.
 */
enum DriveResult DriveRead_WidgetBounds(
    struct App* app, int component_id, int* out_x, int* out_y, int* out_width,
    int* out_height);

/*
 * The component a press at canvas pixel (x, y) would land on: the TOP-MOST
 * menu-relevant node there (UITree_CollectNodesAt, the stack the client's own
 * press and minimenu read, with its visibility, clip, scroll and
 * no-click-through rules). -1 when nothing there takes a press. A drag of
 * overlapping widgets (the jigsaw's loose pile) needs this to press the piece
 * it means rather than whichever was created after it.
 */
enum DriveResult DriveRead_WidgetAt(
    struct App* app, int x, int y, int* out_component_id);

/* ------------------------------------------------------------ verbs-pointer */

/* verbs-pointer, src/plugin/torirs_plugin_drive_pointer.c */

enum DrivePickKind
{
    DRIVE_PICK_NPC = 0,
    DRIVE_PICK_PLAYER,
    DRIVE_PICK_LOC,
    DRIVE_PICK_OBJ,
    /*
     * A carried item's CELL, which is a UI pick and not a world one: it has no
     * element_id, no tile and no screen projection, so it never reaches
     * DrivePointer_ScreenPosition, DrivePointer_MenuRowFind or
     * DrivePointer_WorldOp.  It exists because the four backpack verbs
     * (player.inv_op/equip/drop and use_on's arming half) dispatch through
     * app_minimenu_inv_action's OPHELD ladder, which app_minimenu_run_option
     * reaches ONLY from its UI_MINIMENU_PICK_INV_SLOT case -- the
     * UI_MINIMENU_PICK_UI pick app_plugin_click_node fabricates is a different
     * branch that sends IF_BUTTON instead, and on rev-239's backpack op 1 is
     * the shift-click-drop script, so borrowing it dropped the item on the
     * floor.  See DrivePointer_InvOp.
     */
    DRIVE_PICK_INV_SLOT,
    DRIVE_PICK_KIND_COUNT
};

/** Project a world target to a canvas point.  NPC reuses
 *  App_NpcScreenPosition; loc/obj/player need the new readers, and the obj
 *  reader projects stack->draw_position, never a tile centre.
 *  DRIVE_NOT_VISIBLE when it projects off-screen or behind the near plane. */
enum DriveResult DrivePointer_ScreenPosition(
    struct App* app,
    enum DrivePickKind kind,
    int id,
    int* out_x,
    int* out_y,
    int* out_element_id);

/** Project ONE npc copy, named by its scene element, to a canvas point
 *  (App_NpcElementScreenPosition). DRIVE_NOT_FOUND when no synced npc owns
 *  the element, DRIVE_NOT_VISIBLE when it projects off screen. */
enum DriveResult DrivePointer_NpcElementScreenPosition(
    struct App* app,
    int element_id,
    int* out_x,
    int* out_y);

/** Does this frame's world pickset hold `element_id`?  Meaningless before a
 *  frame has RENDERED at the moved-to point -- the pickset is stamped at the
 *  render-time hover point -- which is why click_minimenu moves, waits a
 *  frame, and only then asks. */
enum DriveResult DrivePointer_PickHolds(struct App* app, int element_id, int* out_held);

/**
 * WHICH PIXEL the pickset above answers for, and the world viewport it was
 * drawn in.
 *
 * `valid` is 0 when no frame has hittested since the pointer last left the
 * viewport. Without this a caller that moved the pointer cannot tell a
 * pickset that does NOT hold its element from one that has not been
 * re-stamped yet, and every answer it builds on top -- "covered", "there is
 * no row for it" -- inherits the ambiguity. The quest driver's own hover
 * search waits on `x`/`y` reaching the pixel it asked for before it believes
 * a `held=false`, and uses the viewport rectangle to refuse a candidate pixel
 * the frame would never hittest at all.
 */
struct DrivePickPoint
{
    int valid;
    int x;
    int y;
    int view_x;
    int view_y;
    int view_w;
    int view_h;
};

enum DriveResult DrivePointer_PickPoint(struct App* app, struct DrivePickPoint* out_point);

enum DriveResult DrivePointer_MouseMove(struct App* app, int x, int y);
enum DriveResult DrivePointer_MouseButton(
    struct App* app, int button, int down, int x, int y);

enum DriveResult DrivePointer_MenuVisible(struct App* app, int* out_visible);

struct DriveMenuRow
{
    int action;
    int pick_kind;
    int target_id;
    int component_id;
    int slot;
    int centre_x;
    int centre_y;
    char text[64];
};

enum DriveResult DrivePointer_MenuRows(
    struct App* app, struct DriveMenuRow* out, int cap, int* out_count);

/*
 * Find the row by (action, pick kind, pick identity) -- never by row text, and
 * never by row order.
 *
 * `action` < 0 is the WILDCARD, and it is part of the contract rather than an
 * aside: with a select mode armed, add_world_select_row collapses the menu to
 * ONE row whose action is USEHELD_ON*, and use_on matches it on pick identity
 * alone.  DRIVE_NO_ROW when no row matches.
 */
enum DriveResult DrivePointer_MenuRowFind(
    struct App* app,
    int action,
    enum DrivePickKind kind,
    int target_id,
    struct DriveMenuRow* out_row);

/** op number (1..5) -> the action id the client's own builder would use for
 *  that slot.  The exported opnpc/oploc/opobj_action_for_slot. */
enum DriveResult DrivePointer_ActionForSlot(
    enum DrivePickKind kind, int slot, int* out_action);

/** The LOGGED bypass: fabricate a one-row menu and run the real dispatcher.
 *  It is never the default path. The ledger note the design doc requires
 *  (docs/QUEST_DRIVER_PLAN.md S5.3) is QD.note in pointer.lua's QD.drive.op
 *  -- the quest ledger a gate actually reads -- not a TORIRS_REPORT here;
 *  a C caller wanting a stderr trace of its own should add one at its own
 *  call site (QD-16). */
enum DriveResult DrivePointer_WorldOp(
    struct App* app, enum DrivePickKind kind, int id, int option, int want_element);

/*
 * The backpack half of the same bypass: fabricate the one INV_SLOT row a real
 * right-click on that cell would carry and run the real dispatcher, so the op
 * leaves through app_minimenu_inv_action (net_out_opheld) exactly as a hand
 * click does.  `component_id` is the inv node's component id, `slot` its cell
 * index, `obj_id`/`count` what that cell holds -- all four because a
 * UIMinimenuPick of this kind carries all four and
 * app_minimenu_ui_pick_live re-resolves (container, slot) and refuses when the
 * item there is not `obj_id`.
 *
 * `option` 1..5 is the numbered held op (OPHELD1..5); 0 is Examine (OPHELD6),
 * the same "<= 0 means examine" reading DrivePointer_WorldOp gives; and a
 * NEGATIVE option arms the held-item selection (OPHELDT_START, the "Use"
 * row), which is use_on's phase 1 and has no op number of its own.
 *
 * DRIVE_REFUSED when the dispatcher would drop the row, and `*out_reason` is
 * then the sentence naming why -- the ordinary case being a cell whose node or
 * an ancestor of it is display-hidden while the container's tab is not the
 * shown one.  This used to be a bare DRIVE_NOT_FOUND with nothing attached,
 * and a refusal that says nothing is indistinguishable from a press the server
 * received and ignored: two quests were filed BLOCKED against content over it
 * (Making History's Castle Wars dig, Roving Elves' consecration seed) because
 * "no chat line, no effect, not even content's own fallback message" is what
 * BOTH look like from Lua.  `out_reason` is written on every return, NULL on
 * DRIVE_OK, and must not be NULL itself.
 */
enum DriveResult DrivePointer_InvOp(
    struct App* app,
    int component_id,
    int slot,
    int obj_id,
    int count,
    int option,
    char const** out_reason);

/*
 * The same cell's "Use" arming, taken WHATEVER is armed right now -- the only
 * honest way to arm a second time, because DrivePointer_InvOp(..., option < 0)
 * with a live selection is encoded as an OPHELDU of the item on itself and
 * ends with nothing armed at all.  Already armed with this cell: nothing is
 * sent and `*out_was_armed` is 1.  Armed with another: that selection is
 * cleared first.  Otherwise: InvOp's own arming, its refusal proof included.
 * `out_was_armed` may be NULL.  The full reasoning, and the two ledger rows
 * that paid for it, are on the definition.
 */
enum DriveResult DrivePointer_InvArm(
    struct App* app, int component_id, int slot, int obj_id, int count, int* out_was_armed);

/*
 * Is `option` (1..5) a row this target would actually offer?  The world
 * bypass needs its own answer: app_minimenu_ui_pick_live returns 1
 * unconditionally for a world pick, so a fabricated row for an op the entity
 * does not have is dispatched happily and the server answers nothing.  NPC
 * reads visible_ops + the op name; loc and obj read their config's op list.
 * DRIVE_NOT_FOUND when nothing of `kind` carries `id`.
 */
enum DriveResult DrivePointer_OpAvailable(
    struct App* app, enum DrivePickKind kind, int id, int option, int* out_available);

/*
 * The live element id a content TYPE id resolves to, nearest the local player
 * first.  Everything a quest test names is a content symbol, so `target.id`
 * is an npc_id/loc_id/obj_id -- while every dispatcher below the menu is
 * keyed by ELEMENT id.  DrivePointer_ScreenPosition has always done this
 * conversion on its way to a pixel; WorldOp and MoveNear were handed the type
 * id and looked it up as an element id, which is why drive.op answered
 * not_found on an npc standing in front of the player.
 */
enum DriveResult DrivePointer_ElementId(
    struct App* app, enum DrivePickKind kind, int id, int* out_element_id);

/** app_try_move to an ABSOLUTE tile.  A 0 return from the engine is
 *  DRIVE_REFUSED with no await registered. */
enum DriveResult DrivePointer_MoveTo(struct App* app, int tile_x, int tile_z);
/** app_try_move_npc / _loc.  Re-issued by the Lua verb every server tick
 *  while the await is pending, because the target can walk. */
enum DriveResult DrivePointer_MoveNear(struct App* app, enum DrivePickKind kind, int id);

/** Shared with content_test.c through App_SetCameraPose (owner: verbs-ui).
 *  pitch outside [128,383] or zoom outside [-1000,10000] is DRIVE_REFUSED
 *  with no state change. */
enum DriveResult DrivePointer_Camera(struct App* app, int yaw, int pitch, int zoom);

/** The follow camera's live pose, the three numbers DrivePointer_Camera
 *  writes: orbit.yaw, orbit.pitch and world_cam_zoom, read back as they ARE
 *  -- not as the driver last left them, because login, a settings row or a
 *  content camera can move them without the driver.  `out_owned` is 1 when
 *  the follow step owns the eye (app_world_camera_follow runs: no CAM_*
 *  script is up and the debug unlock is off), 0 when something else does
 *  and a pose written now would not be what the next frame draws.  QD.shot
 *  (ui.lua) reads this to aim a photograph and put back exactly what was
 *  there. */
enum DriveResult DrivePointer_CameraPose(
    struct App* app, int* out_yaw, int* out_pitch, int* out_zoom, int* out_owned);

/** Movement idleness only: route_length == 0 AND minimap.flag_tile_x < 0.
 *  Both, because they can settle a tick apart. */
enum DriveResult DrivePointer_PlayerIdle(struct App* app, int* out_idle);

/* ----------------------------------------------------------------- verbs-ui */

/* verbs-ui, src/plugin/torirs_plugin_drive_ui.c */

/** Lane-agnostic mount liveness: UITree_GroupPresent (D3). */
enum DriveResult DriveUi_GroupPresent(struct App* app, int interface_id, int* out_present);

/** Content symbol ("<iface>:<child>", always qualified -- D11) plus an
 *  optional dynamic sub index, to a component id. */
enum DriveResult DriveUi_Component(
    struct App* app, char const* symbol, int sub, int* out_component_id);

/** app_plugin_if_click (D2): one path that splits IF1 button types and IF3
 *  numbered ops.  NOT app->host.trigger_op, which is the CS2 host's ring and
 *  has no IF1 counterpart. */
enum DriveResult DriveUi_IfClick(struct App* app, int component_id, int op);

/** app_plugin_tab_select, which branches CS2 vs dat1 internally -- so no lane
 *  handling and no sidetab_N precheck here. */
enum DriveResult DriveUi_Tab(struct App* app, int tab_number);

/** Tab NAME to tab NUMBER, through app->revconfig_refs' "tab" kind: the
 *  profile's `[tabs]` map, or (on a lane with no `[tabs]` section) the
 *  `[role:panel_<name>] match=slot(sidebar, <n>)` derivation
 *  refs_add_tab_from_panel_role already folds into the same table
 *  (revconfig_refs.c). DRIVE_NO_ROW for a name neither source declares --
 *  the test author's typo, not a contract violation. */
enum DriveResult DriveUi_TabByName(struct App* app, char const* name, int* out_tab_number);

/** modal_host_uid, RE-VERIFIED live (D4).  A bare non-zero test is wrong at
 *  boot and permanently wrong after the session's first dialogue. */
enum DriveResult DriveUi_ModalLive(struct App* app, int* out_live);

/**
 * The open minimenu's rectangle, in canvas pixels -- the box
 * UIMinimenu_HitOption tests its rows inside -- and, when `has_point`, what a
 * press at (`point_x`, `point_y`) would do to it: `*out_hit` is
 * UIMinimenu_HitOption's own answer (>= 0 the option index it would SELECT,
 * the same index DrivePointer_MenuRows reports rows in; -1 swallowed by the
 * title bar or the close margin; -2 outside: the menu closes).  -2 when no
 * point is asked.  `*out_visible` 0 (and every other out untouched) when no
 * menu is up.
 *
 * Why a driver needs it (raid seam4 npc_state_size_and_stale_menu): while a
 * menu is up it owns the mouse, and a press of EITHER button on one of its
 * rows SELECTS that row (uitree_interact.c interact_minimenu) -- so a press
 * aimed at the world whose pixel falls on a row of a menu an earlier
 * `covered` press left open takes THAT row, another copy's Attack among
 * them.  The row centres DriveMenuRow carries cannot say where a row's band
 * or the box's sides are; the client's hit test can.  Owner: verbs-ui.
 */
enum DriveResult DriveUi_MenuRect(
    struct App* app,
    int has_point,
    int point_x,
    int point_y,
    int* out_visible,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height,
    int* out_hit);

struct DriveNpcRow
{
    int slot;
    int npc_id;
    int base_npc_id;
    int tile_x, tile_z, level;
    /**
     * The npc's LATEST SERVER TILE: the head of its route queue (the step the
     * last NPC_INFO pushed), where tile_x/tile_z is grid_position, which only
     * advances when the drawn model ARRIVES at a step -- a tick behind a
     * walking npc.  Equal to tile_x/tile_z once the route is walked.  (Verzik's
     * tornadoes, 2026-10-07: a seat reading tile_x saw its tornado one tile
     * further than the server had it, and every dodge was a tile late.)
     */
    int server_x, server_z;
    int element_id;
    /**
     * The overhead health bar, the ONLY hitpoints reading a client has.
     *
     * The client is never told an npc's hitpoints: the server sends a HEADBAR
     * block carrying a fill that is a FRACTION OF THE TYPE'S `width`, never a
     * hp count (src/game/rs_healthbar.h -- conflating the two is what once
     * made boss bars screen-wide).  So a combat verb reads a RATIO, and a
     * seven-hitpoint gardener at four hitpoints reads 17/30, not 4/7.
     *
     * `health_ratio` is `healthbar_end_fill` and not `start_fill`, for the
     * same reason app_plugin_fill_npc_for_world's own health pair is: the two
     * describe a fill ANIMATING from one value to the other, and the END is
     * where the server said it is going -- the value that reads 0 on the tick
     * something dies.  `health_scale` is the type's `width` denominator.
     * Both are -1 when the server has never sent this npc a bar, which is
     * every npc nobody has hit.
     */
    int health_ratio;
    int health_scale;
    /** The bar is inside its display window NOW (`healthbar_end_cycle >
     *  world->cycle`), the same test app_overlay_entities.c makes before it
     *  draws one.  A fight that has stopped leaves the last ratio behind with
     *  this 0, so a reader can tell "at 17/30 and being hit" from "was last
     *  seen at 17/30 some time ago". */
    int health_active;
    /**
     * The newest hitsplat on this npc: its damage value, and the cycle it
     * started on.  `hit_cycle` is the EDGE a combat verb waits for -- it is
     * strictly increasing per splat, so "a new hit landed" is
     * `hit_cycle > <the one I read before the click>` and needs no guess
     * about how long a splat lives.  0/-1 means this npc has no splat in its
     * slots at all.
     *
     * Only slots whose window covers the current cycle count
     * (`damage_start_cycles[i] <= cycle < damage_cycles[i]`, the live test
     * app_overlay_entities.c draws by): an expired slot keeps its values and
     * would otherwise read as a fresh hit forever.
     */
    int hit_damage;
    int hit_cycle;
    /** Normalised: WorldEntity_NPC.name is sized 64 because it stores the
     *  <col=...> tagged form, so the tags come off here. */
    char name[64];
    /**
     * The overhead SAY this npc is showing RIGHT NOW, and the client cycles
     * left before it goes.
     *
     * `npc_say` (torirs_server_scripts.c's SS_OP_NPC_SAY) is deliberately
     * NOT routed through the chatbox -- it is a fire-and-forget SAY mask on
     * NPC_INFO, landing in `npc->chat` (World_NpcSetChat, from
     * PKT_NPC_INFO_OP_SAY).  So for every `[opnpc<n>]` whose whole visible
     * answer is a word over the npc's head, this field is the ONLY reading a
     * client has that the press landed at all: there is no message row, no
     * dialogue and -- when the map refuses the npc's step -- no movement
     * either (quest_sheepherder's `prod_sheep`).
     *
     * 100 is WORLD_ENTITY_CHAT_MAXLEN (src/world/entity_facets.h); a longer
     * message truncates here rather than reaching for that header, which
     * this standalone one does not include.  Tags are stripped like `name`.
     *
     * `overhead_timer` is the facet's own countdown: set to 150 by
     * world_entity_set_chat on EVERY new message and decremented one per
     * client cycle (world_cycle.c), the message cleared at 0.  That reset is
     * what makes "it said it AGAIN" readable: the same words with a timer
     * that jumped back up is a second say, not the first one still hanging
     * about.  0 with an empty message is "nothing overhead".
     */
    char overhead[100];
    int overhead_timer;
    /**
     * What the npc is DOING, for a raid test that reacts to the boss rather
     * than to a timer (docs/RAID_ORCHESTRATOR.md section 4, `npc.state`).
     *
     * `anim_id` / `anim_frame` are the primary (action) track as it is being
     * DRAWN -- -1 / 0 when no action seq plays (the idle and walk loops are
     * the secondary track: `pose_anim` below). `spotanim_id` is the
     * attached graphic being drawn, -1 when none or once its one loop ended.
     *
     * `seq_id` / `seq_tick` are the newest SEQUENCE op the server SENT and
     * the server tick (world cycle / APP_SERVER_TICK_LOGIC_CYCLES) it arrived
     * on: the edge "it attacked on tick T" is read from, because a seq re-sent
     * while it is still playing does not restart the drawn track
     * (WorldEntity_NPC.seq_sent_id). `spotanim_sent_id` / `spotanim_tick`
     * are the same for the newest SPOTANIM op (a spell's impact graphic
     * outlives no frame-rate guess). -1 / -1 before the first op.
     *
     * `facing` is the wire face-entity the npc is locked onto: an npc slot
     * below 32768, 32768 + pid for a player, -1 for none
     * (WORLD_FACING_PLAYER_BASE).
     *
     * `face_x` / `face_z` / `face_tick` are the newest FACE_COORD op the
     * server sent (`npc_facesquare`): the absolute tile the npc was turned
     * to (the centre tile of a sized square) and the server tick it arrived
     * on. -1 / -1 / -1 before the first one. They outlive the turn: the
     * entity's own pending square (facing.square_x) is cleared the cycle the
     * turn is consumed (WorldEntity_NPC.face_sent_x). A face-entity lock
     * does not clear them; `facing` says whether one is held now.
     */
    int anim_id;
    int anim_frame;
    int spotanim_id;
    int seq_id;
    int seq_tick;
    int spotanim_sent_id;
    int spotanim_tick;
    int facing;
    int face_x;
    int face_z;
    int face_tick;
    /**
     * The npc's footprint in tiles (the npc config's `size`, 1 when the
     * config states none), as the client entity holds it NOW: World_NpcSetType
     * rewrites it on every transmog, so a boss that changes form (Xarpus's
     * static form 3 -> fighting form 5) reads its new footprint the tick the
     * client applies the new type.  `tile_x`/`tile_z` are the footprint's
     * south-west corner, so the npc covers [tile_x, tile_x + size) x
     * [tile_z, tile_z + size). (raid seam4 npc_state_size_and_stale_menu.)
     */
    int size;
    /**
     * The POSE the npc is drawing under (or instead of) the action track:
     * the client's secondary / locomotion track (WorldEntity_NPC.animation.
     * secondary), which world_cycle.c sets every cycle from the npc's own
     * idle set -- the readyanim standing, a walk/run variant while a route is
     * being walked (World_UpdateMoverMovementAndAnimation), the turnanim (or
     * the walkanim when the type has none) while a standing npc turns
     * (World_EntityFace). The server never sends these: a ready or walk loop
     * has no SEQUENCE op, so `anim_id` above is -1 the whole time an npc
     * stands or walks, and this is the only reading of what it plays.
     *
     * `pose_anim` / `pose_frame` are the track as drawn (-1 / -1 when the
     * track is empty: an idle set whose readyanim is -1, or an entity that
     * has not cycled yet). `pose_kind` names which slot of the npc's idle
     * set `pose_anim` is: "ready", "walk", "walk_back", "walk_left",
     * "walk_right", "run", "turn", "other" (a seq that matches no slot -- a
     * retype left the old pose on the track for the cycle before the next
     * pick) or "none". A standing npc is matched ready-first and a walking
     * one walk-first, because many types reuse one seq for two slots.
     *
     * The track keeps stepping underneath an action seq; whether it SHOWS
     * is the action seq's own business (its walkmerge / priority), so a row
     * with `anim_id` >= 0 is not evidence the pose is visible.
     *
     * `ready_anim` / `walk_anim` / `turn_anim` / `run_anim` are the idle set
     * the client resolved FOR THIS ENTITY now (WorldEntity_NPC.
     * idle_animations): World_NpcSetType rewrites them on every retype and
     * the BAS-change mask on NPC_INFO overrides single slots, so after
     * npc_changetype they are the new type's -- unlike a cache lookup of the
     * symbol, which answers the old type forever. -1 = the slot is empty.
     */
    int pose_anim;
    int pose_frame;
    char const* pose_kind; /* a string literal; never NULL in a filled row */
    int ready_anim;
    int walk_anim;
    int turn_anim;
    int run_anim;
};

struct DriveLocRow
{
    /** The id the MAP or a zone LOC packet put here -- what the client's
     *  scenery entity stores and what every id-matching reader in this file
     *  (drive_pointer_screen_position_loc) compares against.  For a multiloc
     *  that is the WRAPPER; the client never rewrites it to the child. */
    int loc_id;
    /** The multiloc child `loc_id` currently draws as, under the local
     *  player's varbits (VarPManager_ResolveTransform, the same resolve
     *  app_varp_refresh_loc_transforms and the server's op validation use).
     *  Equal to `loc_id` when the def carries no transform table, and -1 when
     *  the live slot says hidden.  This is the loc analogue of
     *  DriveNpcRow.npc_id-vs-base_npc_id, with the two sides the other way
     *  round: an npc entity stores the RESOLVED id and remembers its base,
     *  a scenery entity stores the BASE and resolves on the way to the
     *  model. */
    int resolved_loc_id;
    int tile_x, tile_z, level;
    int element_id;
    /** The shape the MAP placed this loc with (RSCACHE_LOC_SHAPE_*: 0-3 and 9
     *  walls, 4-8 wall decoration, 10-11 centrepiece scenery, 12-21 roofs, 22
     *  floor decoration), copied from WorldEntity_Scenery.shape.  QD.shot
     *  (script/plugins/quest_driver/ui.lua) reads it to tell a wall or a tree
     *  standing between the camera and the player from a grass tuft on the
     *  same tile -- the row carried no shape before, and a shape-blind count
     *  of locs cannot. */
    int shape;
    /**
     * The sequence the client is PLAYING on this loc's scene element now
     * (ToriDraw_SceneElement.anim_seq_id / anim_frame), and -1 / -1 when it
     * draws static. One reading for both ways a loc animates: a map-placed
     * loc whose record carries an anim (bound at scene build,
     * world_scenery.u.c scenery_load_animation) and a LOC_ANIM the server
     * sent (app_world_apply_seq). A seq that is still loading reads -1 --
     * the element is not drawing it yet -- and a one-shot loc anim reads -1
     * again once it has run out (a DynamicObject drops its seq, it does not
     * hold the last frame unless the seq's frameStep says so).
     */
    int seq;
    int seq_frame;
    /**
     * The looping area sound the client REGISTERED for this placement
     * (world->area_sounds: the emitter list the scene builder gathers from
     * the placed loc's resolved record, map-placed and server-placed alike,
     * and that the audio layer turns into looping voices). Matched on the
     * placement's tile and its id (base or resolved multiloc child).
     * `ambient_sound` is the continuous sound id, `ambient_range` the
     * inaudible distance in tiles, `ambient_inner` the full-volume radius,
     * `ambient_random` how many random alternatives the emitter also plays.
     * All -1 (ambient_random 0) when the client registered nothing for it.
     */
    int ambient_sound;
    int ambient_range;
    int ambient_inner;
    int ambient_random;
};

struct DriveObjRow
{
    int obj_id;
    int count;
    int tile_x, tile_z, level;
    int element_id;
};

/**
 * A free-standing map graphic (MAP_ANIM / `spotanim_map`): the tile hazard
 * half of `world.hazard_at` (Xarpus acid, Maiden blood, Olm crystals are
 * graphics on a tile, not locs). `spotanim_id` is the spotanimtype; -1 only
 * for a graphic the client invented. `active` is 0 while the graphic still
 * waits out its delay (a splash timed to a projectile's flight) and
 * `cycles_left` counts that wait plus its whole life; once active it is the
 * life left. CLIENT cycles (20 ms), not server ticks: a graphic's life is a
 * seq's frame durations and has no tick boundary.
 */
struct DriveSpotanimRow
{
    int spotanim_id;
    int tile_x, tile_z, level;
    int active;
    int cycles_left;
    int element_id;
    /** The sequence the client is playing for this graphic on its scene
     *  element (anim_seq_id / anim_frame; the spotanimtype's own seq, bound
     *  by app_world_apply_seq at spawn and looped, anim_loop). -1 / -1 while
     *  the seq is still loading or the record names none: what the element
     *  draws, not what the record says. */
    int seq;
    int seq_frame;
};

/**
 * A projectile in flight (MAP_PROJANIM). `src_*` is the tile it left from;
 * `dst_*` the tile it is aimed at NOW -- for a projectile that homes on an
 * entity that is the target's live tile (World re-aims it every cycle), for
 * a tile shot the fixed destination. `target` is the wire target id
 * (WorldEntity_Projectile.target): npc slot + 1, -(pid) - 1 for a player, 0
 * for a tile shot; `target_npc_slot` decodes the npc case (-1 otherwise).
 * `cycles_left` is client cycles to impact; `launched` is 0 while it still
 * waits out its start delay.
 */
struct DriveProjectileRow
{
    int spotanim_id;
    int src_tile_x, src_tile_z;
    int dst_tile_x, dst_tile_z;
    int level;
    int target;
    int target_npc_slot;
    int launched;
    int cycles_left;
    /** The flight as the server sent it, in client cycles from the packet
     *  (its end cycle): fixed for the projectile's life, so a reader keys a
     *  landing on it whenever it looks, where cycles_left depends on how far
     *  into the flight the client has stepped (live reads further in than
     *  scriptrun). */
    int duration;
    int element_id;
    /** As DriveSpotanimRow.seq / seq_frame: the projectile graphic's own
     *  sequence as its scene element plays it, -1 / -1 when none is bound. */
    int seq;
    int seq_frame;
};

/** Pool walks in the shape of content_test.c's npc_json / scenery_json,
 *  nearest first by tile distance from the local player.  `radius` <= 0 means
 *  the whole pool. */
enum DriveResult DriveUi_Npcs(
    struct App* app, int radius, struct DriveNpcRow* out, int cap, int* out_count);
enum DriveResult DriveUi_Locs(
    struct App* app, int radius, struct DriveLocRow* out, int cap, int* out_count);
/**
 * Every placed copy of ONE loc id in the loaded scene (the root worldview's
 * scenery pool -- the same pool DriveUi_Locs walks, so map-placed and
 * server-placed locs alike), nearest first, filtered on the id BEFORE the
 * nearest-K window rather than after it.
 *
 * Why it is not DriveUi_Locs plus a filter (raid seam11
 * ticklog_raw_damage_and_loc_count): the Verzik room's map places
 * `tob_dungeon_verzik_death_cage` 32717 twenty-four times (m49_67.jl2:255-278)
 * and the AV spec counts them. DriveUi_Locs keeps the nearest 8192 rows of
 * every id; a scene with more scenery than that drops the far copies without
 * saying so, and a count read off its rows is a lower bound that looks like a
 * count. Here only the matching rows are sorted, and `*out_total` is every
 * match inside `radius` even when more than `cap` matched, so a caller can
 * tell "24" from "the first 24 of more".
 *
 * `loc_id` is the PLACED id (DriveLocRow.loc_id: for a multiloc, the
 * wrapper the map names). `radius` <= 0 means the whole scene. Rows carry
 * everything a DriveUi_Locs row does.
 */
enum DriveResult DriveUi_LocCopies(struct App* app,
                                   int loc_id,
                                   int radius,
                                   struct DriveLocRow* out,
                                   int cap,
                                   int* out_count,
                                   int* out_total);
enum DriveResult DriveUi_Objs(
    struct App* app, int radius, struct DriveObjRow* out, int cap, int* out_count);
/** Every map graphic and every projectile, nearest first like the readers
 *  above (a projectile ranks by its DESTINATION tile: the question a test
 *  asks of one is "where does it land"). */
enum DriveResult DriveUi_Spotanims(
    struct App* app, int radius, struct DriveSpotanimRow* out, int cap, int* out_count);
enum DriveResult DriveUi_Projectiles(
    struct App* app, int radius, struct DriveProjectileRow* out, int cap, int* out_count);

/**
 * A loc def's multiloc family: the child it resolves to NOW, and every id its
 * transform table (and its children's tables) can name.
 *
 * Why a test needs this, measured 2026-09-19 (build/quest_gate/priest, rows 25
 * and 27): `openghostcoffin` is id 15061, a multiloc whose slots are
 * `openghostcoffin_no_head` (15052) and `openghostcoffin_with_head` (15053),
 * and the Restless Ghost's `[oploc1,shutghostcoffin]` calls
 * `loc_change(openghostcoffin_no_head, 300)` -- the LEAF.  So after the coffin
 * opens the scene holds 15052 and 15061 is in no pool at all, while a test
 * that names the symbol a human would name gets `not_found` for a coffin
 * filling a third of the frame.  4,675 of this pack's 62,194 loc defs carry a
 * multiloc line, so this is a class, not one coffin.
 *
 * `out_resolved` is the live child (or `loc_id` itself where there is no
 * transform table, or -1 where the live slot is a hide).  `out_slots` is the
 * flattened family, dedup'd, nearest level first, EXCLUDING `loc_id`.
 *
 * DRIVE_TIMEOUT means the def is not resident: a CreateTask_LocLoad has been
 * queued on the app runner and the caller should poll again next frame (the
 * editor's catalogue does the same thing for the same reason,
 * editor_panel.c:970-976).  A loc that no map square and no packet has ever
 * placed -- which is exactly the multiloc WRAPPER a test names -- is normally
 * absent, so this answer is the common first one, not an error.
 * DRIVE_NO_ROW: no cache provider.  Owner: verbs-ui.
 */
enum DriveResult DriveUi_LocVariants(
    struct App* app, int loc_id, int* out_resolved, int* out_slots, int cap, int* out_count);

/** App_LocalPlayerTiles; its false return is DRIVE_NOT_VISIBLE. */
enum DriveResult DriveUi_PlayerTile(
    struct App* app, int* out_x, int* out_z, int* out_level);

/** The walk a click on dst would take, every tile in walk order (absolute),
 *  from the player's whole tile on the client's collision map --
 *  collision_map_route_tiles, the server's own flood (api.drive.route).
 *  entity_size > 0: to the reach of that size's entity with its south-west
 *  tile at dst. from_x/from_z >= 0: plan the walk from that absolute tile
 *  instead of the player's (a planner asking "and from there?"); -1 for the
 *  player's own. range >= 0 (with entity_size > 0): a ranged interaction's
 *  walk, cut where the server fires it -- the first tick's end within range
 *  and in line of sight (collision_route_cut_in_range); -1 for a melee walk.
 *  steps_per_tick: 2 running, 1 walking (the cut is per tick).
 *  DRIVE_NOT_FOUND: no route; DRIVE_REFUSED: dst or from outside the scene;
 *  DRIVE_NOT_VISIBLE: no player or world. */
enum DriveResult DriveUi_Route(
    struct App* app,
    int dst_x,
    int dst_z,
    int entity_size,
    int range,
    int steps_per_tick,
    int from_x,
    int from_z,
    int* out_x,
    int* out_z,
    int cap,
    int* out_count,
    int* out_arrive_x,
    int* out_arrive_z,
    int* out_nearest);

/** torirs_keymap.c's named table, wider than content_test.c's four names. */
enum DriveResult DriveUi_Key(struct App* app, char const* name, int down);
/** 1..63 printable ASCII, validated up front. */
enum DriveResult DriveUi_Text(struct App* app, char const* text);
/** App_RequestScreenshot plus the poll for capture_path, the way
 *  content_test.c:698-701 does it. */
enum DriveResult DriveUi_Shot(struct App* app, char const* name, char* out_path, int out_cap);

/* ------------------------------------------------------ core-scheduler seam */

/** ToriRSServer_ScriptsRunDebugproc, in-process, returning the verdict the
 *  handler throws away today: RAN -> ok, FAILED -> refused, NONE -> no_row.
 *  No stderr scraping. */
enum DriveResult DriveCore_Cheat(struct App* app, char const* text);

/** !App_AsyncPending && App_FrameSettled && !world_load_inflight. */
enum DriveResult DriveCore_Settled(struct App* app, int* out_settled);

/** The session directory (TORIRS_CONTENT_TEST) every artefact lands under:
 *  ledger.tsv, shots/NN-name.png, result.  In an on-demand client it is the
 *  directory the last api.drive.start named, or NULL before the first. */
char const* DriveCore_SessionDir(void);

/*
 * The in-process embedded server for THIS frame, or NULL on a socket-server
 * run. content_test.c is the one place that already turns a NetTransport into
 * a struct ToriRSServerEmbed* (NetTransport_TestClock); it hands the result
 * here once per frame because DriveCore_Cheat's whole reason to exist is
 * calling ToriRSServer_RunDebugprocForTest directly, in-process, with no
 * packet -- the socket-server fallback is U8, not built yet. */
void PluginDriveCore_SetEmbed(struct ToriRSServerEmbed* embed);

/*
 * The live struct ToriRS_CmdBus* for THIS frame -- content_test.c already
 * receives it as ContentTest_Begin's own `bus` parameter and hands it here
 * the same way it hands PluginDriveCore_SetEmbed the embedded server.
 * verbs-pointer's DrivePointer_MouseMove/MouseButton (torirs_plugin_drive_
 * pointer.c) push onto it; that file's own banner comment reports this as
 * a cross-group seam core-scheduler is the one who can wire.
 */
void PluginDriveCore_SetCmdBus(struct ToriRS_CmdBus* bus);

/** The bus PluginDriveCore_SetCmdBus was last handed, or NULL before the
 *  first content-test frame. */
struct ToriRS_CmdBus* PluginDriveCore_CmdBus(void);

/*
 * True while the one outstanding drive.await has a LEVEL predicate armed
 * (g_await.level_ref != LUA_NOREF) -- core.lua's own EDGE+LEVEL rule
 * (QUEST_DRIVER_DESIGN.md) means that predicate is re-read from live
 * rendered/engine state (the pickset, menu_visible, player_idle, ...) on
 * every pump, not just once on an event. content_test.c's
 * ContentTest_DrawRequested has no quest-mode equivalent of the mailbox's
 * `active && (capture_path[0] || click_phase >= 0 || hover_requested)`
 * forced-draw branch (docs/QUEST_DRIVER_REMAINING.md B0): quest mode never
 * sets `active`, so that whole branch is dead there, and a world frame with
 * nothing else marking need_redraw (no animation, no camera drift) can go
 * an unbounded number of logic frames without ever rendering again --
 * measured B0 spike: exactly one render at boot, then app->frame_view->world_pickset
 * frozen at that one frame's contents for the rest of the run. Forcing a
 * draw while this is true gives a level-await's next poll a current frame
 * to read, the same way `hover_requested` does for the mailbox. An
 * edge-only await (event= with no level=) does not need this: it resolves
 * off a discrete event, not off rendered state, so it is deliberately left
 * out.
 */
int PluginDriveCore_LevelAwaitPending(void);

/* --------------------------------------------------------- core-revconfig */

/*
 * The derived-role fallback.  revconfig's `derive=<fact>` roles carry no match
 * expression: the engine answers them at the publication fence through
 * UITreeRoleTable.fallback, which is consulted BEFORE the matcher chain.
 *
 * Facts, all of them lane-agnostic by construction:
 *   dialog_continue   the live node under chat_modal_host whose effective
 *                     IF_SETEVENTS carries RS_MINIMENU_EVENT_CLICK -- the same
 *                     test rs_minimenu_build.c:1073 makes per row
 *   pause_pending     UITree_PausePendingIndex(tree)
 *   chat_modal_host   app->slots.chat_com_id (dat1 only; the cache lane binds
 *                     this role by match expression instead)
 *   button_type(N)    the dat1 spelling of dialog_continue: buttontype=pause
 *                     baked into the .if, which if_button_action_for_type
 *                     already maps to RESUME_PAUSEBUTTON
 *
 * An unknown fact is a data error, reported the way a malformed match= line is
 * reported, and leaves the role with no matchers.  `make -C src
 * check-revconfig-roles` fails a derive= fact this function does not know.
 * Owner: core-revconfig, src/app/app_role_derive.c.
 */
int App_RoleDeriveFallback(void* user, char const* fact, int argument, int* out_component_id);

/** Install it on the tree's role table.  Called once, from app boot. */
void App_RoleDeriveInstall(struct App* app);

#endif /* TORIRS_PLUGIN_DRIVE_H */
