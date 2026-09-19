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

struct App_DriveRing
{
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

/** Shutdown: drop the coroutine, close the ledger, forget the app. */
void PluginDrive_Shutdown(void);

/*
 * The quest script the process was asked to run (TORIRS_QUEST_SCRIPT), or
 * NULL.  A run with no quest script still loads the driver plugin -- that is
 * the shape the load gate uses -- it simply never creates a thread.
 * Owner: core-scheduler.
 */
char const* PluginDrive_QuestScriptPath(void);

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
 * driver state.  Owner: core-scheduler (the flag), core-events (the main.c
 * branch).
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

/** Does this frame's world pickset hold `element_id`?  Meaningless before a
 *  frame has RENDERED at the moved-to point -- the pickset is stamped at the
 *  render-time hover point -- which is why click_minimenu moves, waits a
 *  frame, and only then asks. */
enum DriveResult DrivePointer_PickHolds(struct App* app, int element_id, int* out_held);

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
    struct App* app, enum DrivePickKind kind, int id, int option);

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
 * DRIVE_NOT_FOUND when the component is not in the tree or the dispatcher
 * refused the row -- which is the ordinary answer when the container's tab is
 * not the displayed one, because app_minimenu_ui_pick_live rejects a cell
 * whose node or ancestor is display-hidden.
 */
enum DriveResult DrivePointer_InvOp(
    struct App* app, int component_id, int slot, int obj_id, int count, int option);

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

struct DriveNpcRow
{
    int slot;
    int npc_id;
    int base_npc_id;
    int tile_x, tile_z, level;
    int element_id;
    /** Normalised: WorldEntity_NPC.name is sized 64 because it stores the
     *  <col=...> tagged form, so the tags come off here. */
    char name[64];
};

struct DriveLocRow
{
    int loc_id;
    int tile_x, tile_z, level;
    int element_id;
};

struct DriveObjRow
{
    int obj_id;
    int count;
    int tile_x, tile_z, level;
    int element_id;
};

/** Pool walks in the shape of content_test.c's npc_json / scenery_json,
 *  nearest first by tile distance from the local player.  `radius` <= 0 means
 *  the whole pool. */
enum DriveResult DriveUi_Npcs(
    struct App* app, int radius, struct DriveNpcRow* out, int cap, int* out_count);
enum DriveResult DriveUi_Locs(
    struct App* app, int radius, struct DriveLocRow* out, int cap, int* out_count);
enum DriveResult DriveUi_Objs(
    struct App* app, int radius, struct DriveObjRow* out, int cap, int* out_count);

/** App_LocalPlayerTiles; its false return is DRIVE_NOT_VISIBLE. */
enum DriveResult DriveUi_PlayerTile(
    struct App* app, int* out_x, int* out_z, int* out_level);

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
 *  ledger.tsv, shots/NN-name.png, result. */
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
 * measured B0 spike: exactly one render at boot, then app->world_pickset
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
