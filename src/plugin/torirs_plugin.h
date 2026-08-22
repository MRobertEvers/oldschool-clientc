#ifndef TORIRS_PLUGIN_H
#define TORIRS_PLUGIN_H

/*
 * The plugin contract: everything a plugin -- C or scripted -- may see or
 * touch. Nothing in this header includes an engine type, and no engine pointer
 * ever crosses it: world state arrives as copy-out POD snapshots and the
 * engine is reached only through the api function table.
 *
 * That is what makes the layer language-agnostic. A scripting adapter (Lua,
 * and whatever comes after it) is an ordinary C plugin that hosts N scripts
 * and forwards this same surface; the engine never learns a VM exists.
 *
 * Coordinates in snapshots are ABSOLUTE tiles (scene tile + world base tile),
 * so a plugin's saved state survives a scene rebuild. Fine positions are
 * scene-relative 128ths of a tile, matching WorldEntityFacet_DrawPosition,
 * because they are only ever handed straight back to api->project.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

/* Bumped whenever anything below changes shape. A plugin compiled against a
 * different value is refused rather than run against a struct it disagrees
 * about. */
#define TORIRS_PLUGIN_ABI 8

#define TORIRS_PLUGIN_NAME_MAX 48
/** Bytes of a plugin's human title, terminator included. Longer than the name
 *  because a title carries spaces and words the kebab-case id compresses. */
#define TORIRS_PLUGIN_TITLE_MAX 64
#define TORIRS_PLUGIN_MENU_ROWS_MAX 16
/** Controls one plugin may put on its tab. A budget, not a limit on what a
 *  window can hold: sixteen plugins sharing one window need the SHARE bounded,
 *  or the first one to ask takes the whole chrome. */
#define TORIRS_PLUGIN_WIDGETS_MAX 24
/** Bytes of a control id, terminator included. */
#define TORIRS_PLUGIN_WIDGET_ID_MAX 32
/** Longest asset name a plugin may ask for, including its extension. */
#define TORIRS_PLUGIN_ASSET_NAME_MAX 64
/** Recolour pairs one world object may carry, matching a spotanimtype's six. */
#define TORIRS_PLUGIN_OBJECT_RECOLORS_MAX 6

/*
 * Key codes for api->key_held, mirroring enum LibToriRS_KeyCode.
 *
 * Restated here rather than including the input header, so that anything built
 * on this contract -- a C plugin, the Lua adapter, whatever comes after it --
 * stays free of engine headers. The static asserts in
 * torirs_plugin_bridge.u.c are what keep the two in step: if the enum ever
 * moves, the client stops compiling rather than silently gating on the wrong
 * key.
 *
 * Only the modifiers are here. They are the ones a plugin gates on -- "hold
 * shift and right-click" is the shape half the client's own settings describe
 * -- and a full mirror of the enum would be a second copy of it to keep true.
 */
#define TORIRS_PLUGIN_KEY_ESCAPE 37
#define TORIRS_PLUGIN_KEY_SHIFT 42
#define TORIRS_PLUGIN_KEY_CTRL 43
#define TORIRS_PLUGIN_KEY_TAB 44
#define TORIRS_PLUGIN_KEY_SPACE 45

/* Opaque per-plugin instance handle. The host defines it. */
struct ToriRS_PluginCtx;

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

enum ToriRS_PluginEvent
{
    /** Enabled. Subscribe and allocate here. */
    TORIRS_PLUGIN_EV_START = 0,
    /** Disabled. Release everything; subscriptions are dropped by the host. */
    TORIRS_PLUGIN_EV_STOP,
    /** Top of App_RunOnce, after the command drain. Payload: EvFrame. */
    TORIRS_PLUGIN_EV_FRAME_START,
    /** Each 20ms client cycle (app_logic_tick). Payload: EvTick. */
    TORIRS_PLUGIN_EV_LOGIC_TICK,
    /** SERVER_TICK_END applied: every packet of this server tick is in world
     *  state. The 600ms cadence, and the only safe place to read a snapshot
     *  that must agree with the server. Payload: EvTick. */
    TORIRS_PLUGIN_EV_SERVER_TICK,
    /** Scene rebuild finished; absolute tile origin moved. Payload: EvWorld. */
    TORIRS_PLUGIN_EV_WORLD_LOADED,
    /** Payload: EvNpc. */
    TORIRS_PLUGIN_EV_NPC_SPAWN,
    /** multinpc rung / type change. base_npc_id is unchanged. Payload: EvNpc. */
    TORIRS_PLUGIN_EV_NPC_RETYPE,
    /** Payload: EvNpc. Snapshot is the last known state. */
    TORIRS_PLUGIN_EV_NPC_DESPAWN,
    /** Decoded, before it reaches world state. Payload: EvPacketIn. */
    TORIRS_PLUGIN_EV_PACKET_IN,
    /** Named, BEFORE the builder runs. Payload: EvPacketOut. */
    TORIRS_PLUGIN_EV_PACKET_OUT,
    /** A key event drained this frame. Payload: EvKey. */
    TORIRS_PLUGIN_EV_KEY,
    /** Minimenu rows assembled; rows may be appended. Payload: EvMenuBuild. */
    TORIRS_PLUGIN_EV_MENU_BUILD,
    /** A row was chosen, before dispatch. Payload: EvMenuSelect. */
    TORIRS_PLUGIN_EV_MENU_SELECT,
    /** The world overlay surface is open. The draw api is legal only here.
     *  Payload: EvDraw. */
    TORIRS_PLUGIN_EV_DRAW_WORLD,
    /** One of this plugin's config keys changed. Payload: EvConfig. */
    TORIRS_PLUGIN_EV_CONFIG_CHANGED,
    /** A ground-item stack appeared on a tile. Payload: EvObj. */
    TORIRS_PLUGIN_EV_OBJ_SPAWN,
    /** A ground-item stack's count changed in place. Payload: EvObj, carrying
     *  the NEW count. */
    TORIRS_PLUGIN_EV_OBJ_COUNT,
    /** A ground-item stack was taken or timed out. Payload: EvObj, holding the
     *  last known state. */
    TORIRS_PLUGIN_EV_OBJ_DESPAWN,
    /** An api->asset_load finished, either way. Payload: EvAsset. */
    TORIRS_PLUGIN_EV_ASSET,
    /** A line reached the chatbox. Payload: EvChat. */
    TORIRS_PLUGIN_EV_CHAT_MESSAGE,
    /** A notable moment the client recognised -- a level-up, a quest
     *  completion, a valuable drop, a boss kill. Payload: EvGameEvent. */
    TORIRS_PLUGIN_EV_GAME_EVENT,
    /**
     * Someone used a control on this plugin's window tab. Payload: EvUi.
     *
     * Raised for the plugin that owns the widget and no other, so a plugin
     * never sees another's controls -- the window is shared, the tabs are not.
     */
    TORIRS_PLUGIN_EV_UI,
    /**
     * This plugin's tab needs its contents. Payload: EvUi with `widget_id`
     * NULL.
     *
     * Raised after every win_clear the host itself performs -- on a reload, on
     * a re-enable, when the window is first opened -- so a plugin declares its
     * controls in ONE place and that place is re-run whenever the tab is
     * empty. A plugin that built its tab only in on_start would come back from
     * a reload with a blank tab.
     */
    TORIRS_PLUGIN_EV_UI_BUILD,
    /**
     * A row in the cache's own All Settings panel was used. Payload:
     * EvSetting.
     *
     * Every builtin reads its switch out of a varbit and needs no event for
     * it. This is for the rows that HAVE no varbit: a BUTTON row ("Clear your
     * highlighted tiles") is momentary, and the only trace the panel leaves of
     * it is `%varbit9657 = <setting id>`, which the four apply hubs all write
     * before they switch. Polling that varbit cannot see the same button
     * pressed twice; this can.
     */
    TORIRS_PLUGIN_EV_SETTING,

    TORIRS_PLUGIN_EV_COUNT
};

enum ToriRS_PluginVerdict
{
    /** Observed. The next subscriber runs and the engine proceeds. */
    TORIRS_PLUGIN_PASS = 0,
    /** Stop propagation AND suppress the engine's default behaviour, on the
     *  events that document themselves as interceptable. On the others it
     *  stops propagation only. */
    TORIRS_PLUGIN_CONSUME = 1
};

typedef enum ToriRS_PluginVerdict (*ToriRS_PluginHandler)(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata);

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                 */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginPlayerSnap
{
    /** The authoritative server tile: pathing.route[0] lifted to absolute.
     *  This is the "true tile" -- where the server believes the entity is --
     *  as distinct from the interpolated draw position below. */
    int true_x;
    int true_z;
    int level;
    /** Interpolated position, scene-relative, 128 units per tile. Feed
     *  straight to api->project. */
    int fine_x;
    int fine_z;
    /** Where the walk ends, absolute -- the map flag, which lives from the
     *  click to the arrival. Equals true_* when there is no destination, so a
     *  marker only has to ask whether the two differ. LOCAL PLAYER ONLY: every
     *  other player reports itself, because nothing tells this client where
     *  someone else is headed. */
    int dest_x;
    int dest_z;
    /** The same flag, unfolded: -1 when there is no destination at all, which
     *  dest_* cannot say (standing on your own destination reads the same as
     *  having none). Local player only. */
    int flag_x;
    int flag_z;
    /** Server pid, or -1 when unsynced. */
    int server_pid;
    /** ToriDraw scene element, for api->draw_hull. -1 when not drawn. */
    int element_id;
    int combat_level;
    char name[32];
};

struct ToriRS_PluginNpcSnap
{
    int server_slot;
    /** The resolved/drawn type. Changes when a multinpc rung changes. */
    int npc_id;
    /** The wire id -- the multinpc SHELL. Identity for tagging: it is what
     *  survives a varbit-driven appearance change. */
    int base_npc_id;
    char name[64];
    int combat_level;
    /** Footprint in tiles. true_x/true_z are the SW corner. */
    int size;
    int true_x;
    int true_z;
    int level;
    int fine_x;
    int fine_z;
    int element_id;
    /** Bit i set = op i is offered on this npc. */
    uint8_t visible_ops;
};

/**
 * One loc (scenery) standing in the scene.
 *
 * "Loc", not "object": `obj` is a ground ITEM everywhere in this tree, and the
 * two are the pair that get confused. This is the door, the tree, the rock,
 * the poll booth -- a thing the map placed.
 *
 * SCENE-SCOPED, unlike an npc or a ground item: a loc outside the loaded scene
 * simply is not in the list, and the list is rebuilt from nothing on every
 * scene load. A plugin that needs to remember one across a rebuild remembers
 * its absolute tile and finds it again, the way the client's own loc ops do.
 */
struct ToriRS_PluginLocSnap
{
    int loc_id;
    /** As the minimenu shows it, colour tags and all. */
    char name[64];
    /** ABSOLUTE tile of the SW corner, and the walked level. */
    int tile_x;
    int tile_z;
    int level;
    /** ROUTE footprint: the loc's config size, angle-swapped. Ground decor
     *  routes as its full config size but draws on one tile, so this is not
     *  what the model covers. */
    int size_x;
    int size_z;
    /** Placed shape (RSCACHE_LOC_SHAPE_*) and rotation, 0..3 = W/N/E/S. */
    int shape;
    int angle;
    int element_id;
    /** LocType.active: 0 for a wall, gravel or floor decor that nothing can
     *  click. A highlighter that ignores this lights up the pavement. */
    int interactive;
    /** Bit i set = op i is offered on this loc. */
    uint8_t visible_ops;
};

struct ToriRS_PluginObjSnap
{
    /** The obj id of the item on TOP of the stack -- the one the tile draws
     *  and the one the server's ops belong to. */
    int obj_id;
    int count;
    /**
     * ObjType.cost (cache opcode 12) for one of them: the same number CS2
     * reads through OC_COST, and the only value the client has. It is a base
     * price, not a live Grand Exchange quote -- nothing in the client knows
     * one -- so a plugin that wants market prices ships its own table as an
     * asset and looks `obj_id` up in it.
     *
     * 0 when the objtype is not resident. Not -1: 0 is also what a valueless
     * item costs, and a plugin thresholding on value treats both the same way.
     */
    int cost;
    /** Name as the right-click builder sees it, colour tags and all. */
    char name[64];
    /** ABSOLUTE tile, like every other snapshot here. */
    int tile_x;
    int tile_z;
    int level;
    /** ToriDraw scene element, for api->draw_hull. -1 when not drawn. */
    int element_id;
};

/* ------------------------------------------------------------------------ */
/* Event payloads                                                            */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginEvFrame
{
    uint64_t now_ms;
};

struct ToriRS_PluginEvTick
{
    /** logic_cycle for LOGIC_TICK, world cycle for SERVER_TICK. */
    int cycle;
};

struct ToriRS_PluginEvWorld
{
    int base_tile_x;
    int base_tile_z;
};

struct ToriRS_PluginEvNpc
{
    struct ToriRS_PluginNpcSnap npc;
};

struct ToriRS_PluginEvPacketIn
{
    /** enum GameProtoPktName. Compare against api->packet_name(). */
    int name;
    /** Wire size of the payload, or -1 when the revision does not report it. */
    int size;
    /** Set true to drop the packet: it is freed instead of executed, and no
     *  further subscriber sees it.
     *
     *  This is a live wire. PLAYER_INFO/NPC_INFO extended-info blocks are
     *  indexed by list position, so dropping one desyncs entity bookkeeping
     *  for the rest of the session, and the server-tick fence may never be
     *  dropped at all (the host asserts). */
    bool drop;
};

struct ToriRS_PluginEvPacketOut
{
    /**
     * The builder that is about to run, by name: "net_out_opnpc",
     * "net_out_move_gameclick", and so on.
     *
     * A string rather than the enum the inbound side uses, because outbound
     * genuinely has no id at the call site -- every send is a direct call to a
     * net_out_* builder that returns a byte count, and there is no packet-name
     * argument anywhere in the path. Deriving the name from the call itself is
     * what makes all sixty send sites observable without a hand-written
     * builder-to-enum table that could label any one of them wrong.
     */
    char const* builder;
    /** Set true to veto the send.
     *
     *  Fires BEFORE the builder, because every net_out_* builder encrypts its
     *  opcode by advancing the outbound ISAAC stream: a packet built and then
     *  discarded desyncs the cipher and the server misreads every opcode
     *  after it. There is deliberately no payload here for the same reason --
     *  it does not exist yet. */
    bool drop;
};

struct ToriRS_PluginEvKey
{
    /** enum LibToriRS_KeyCode. */
    int key;
    /** Typed character, or 0. */
    int ch;
    bool down;
};

/** Read-only view of one built minimenu row. */
struct ToriRS_PluginMenuRow
{
    /** Includes the reference colour tags (@yel@ and friends). */
    char const* text;
    /** RevConfig action id, already normalized. */
    int action;
    /** enum UIMinimenuPickKind. */
    int pick_kind;
    /** Server slot when the row targets an npc, else -1. */
    int npc_slot;
    /** Server pid when the row targets a player, else -1. */
    int player_pid;
    /** Loc/obj id when the row targets one, else -1. */
    int target_id;
};

struct ToriRS_PluginEvMenuBuild
{
    int row_count;
    struct ToriRS_PluginMenuRow rows[TORIRS_PLUGIN_MENU_ROWS_MAX];
    /** True for the hover-text rebuild, which runs EVERY FRAME. Handlers that
     *  only need rows in the right-click menu should return immediately. */
    bool hover_pass;
    /** Opaque; hand back to api->menu_add. */
    void* host_cursor;
};

struct ToriRS_PluginEvMenuSelect
{
    struct ToriRS_PluginMenuRow row;
    /** The tag passed to api->menu_add, or 0 for a native row. */
    uint32_t plugin_tag;
    /** True when this row belongs to the plugin being dispatched. */
    bool owned;
    int click_x;
    int click_y;
};

struct ToriRS_PluginEvDraw
{
    /** Opaque surface token; hand back to the draw api. */
    void* surface;
};

struct ToriRS_PluginEvConfig
{
    char const* key;
};

struct ToriRS_PluginEvObj
{
    struct ToriRS_PluginObjSnap obj;
};

struct ToriRS_PluginEvChat
{
    /** enum RS_ChatMessageType, as it arrived on the wire: 0 game, 2 public,
     *  3 private-from, and so on. */
    int type;
    /** Who said it, or "" for a system line. */
    char sender[64];
    /** The line, exactly as the chatbox holds it -- colour tags included. The
     *  recognised events below arrive with those stripped; this one does not,
     *  because a plugin reading raw chat may well care about them. */
    char text[200];
};

/*
 * One notable moment.
 *
 * `kind` is a STRING rather than an enum, and that is deliberate: the
 * recogniser (game/rs_game_events.c) already owns the list, a second copy in
 * this header would be a second thing to keep in step, and a plugin's config
 * lists these by name anyway ("screenshot on: level_up, boss_kill"). A kind
 * added to the recogniser reaches every plugin without touching the ABI.
 */
struct ToriRS_PluginEvGameEvent
{
    /** Stable lowercase name: "level_up", "quest_complete", "valuable_drop",
     *  "untradeable_drop", "boss_kill", "pet", "collection_log",
     *  "combat_achievement", "death", "treasure_trail", "duel_end". Never
     *  NULL, and valid for this dispatch only. */
    char const* kind;
    /** The skill, boss, quest or item this is about; "" when the moment names
     *  nothing (a pet drop never says which pet). */
    char subject[64];
    /** New level / kill count / coin value, or -1 when the kind carries none. */
    int value;
    /** The line it was recognised from, markup stripped. Empty for a level-up,
     *  which comes from UPDATE_STAT and has no sentence behind it. */
    char text[200];
};

/**
 * One use of an All Settings row.
 *
 * `setting_id` is the cache's `param_1077`, the number every settings hub
 * switches on -- 117 is "Clear your highlighted tiles", 112 is "Tile
 * highlighting". `value` is the row's new value where the hub carries one
 * (a dropdown, a slider) and -1 where it does not (a toggle, whose hub is
 * handed the id alone, and a button, which has no value at all).
 */
struct ToriRS_PluginEvSetting
{
    int setting_id;
    int value;
};

/** What the pointer is over. @see ToriRS_PluginApi::hover_entity. */
enum ToriRS_PluginHoverKind
{
    TORIRS_PLUGIN_HOVER_NONE = 0,
    TORIRS_PLUGIN_HOVER_SCENERY,
    TORIRS_PLUGIN_HOVER_NPC,
    TORIRS_PLUGIN_HOVER_PLAYER,
    TORIRS_PLUGIN_HOVER_OBJ
};

/**
 * The nearest entity under the pointer, as the last rendered frame picked it.
 *
 * Not the same question as hover_tile, which answers with the GROUND under the
 * pointer and is filled even when the cursor is over open grass. This one is
 * about a thing: the first scenery/npc/player/objstack in the frame's pickset,
 * which is the one the client's own left-click would act on.
 */
struct ToriRS_PluginHoverEntity
{
    /** enum ToriRS_PluginHoverKind. NONE when nothing is under the pointer. */
    int kind;
    /** ToriDraw scene element, for api->draw_hull. */
    int element_id;
    /** ABSOLUTE tile the pick landed on, and the level it was picked at. */
    int tile_x;
    int tile_z;
    int level;
};

/* ------------------------------------------------------------------------ */
/* Plugin windows                                                            */
/* ------------------------------------------------------------------------ */

/**
 * What a plugin can put on its tab.
 *
 * A deliberately small set, and the small set is the point: these are the
 * controls every executor can present natively. A checkbox is a checkbox in
 * the canvas, in the DOM, in comctl32 and in a cache interface; anything
 * richer would be a control one of them has to fake, and a faked control is
 * how a "native" window stops looking native.
 */
enum ToriRS_PluginWidgetKind
{
    TORIRS_PLUGIN_W_LABEL = 0,
    TORIRS_PLUGIN_W_CHECKBOX,
    TORIRS_PLUGIN_W_INPUT,
    TORIRS_PLUGIN_W_DROPDOWN,
    TORIRS_PLUGIN_W_BUTTON,
    TORIRS_PLUGIN_W_SEPARATOR,
};

/** What happened to a control. */
enum ToriRS_PluginUiAction
{
    /** A button was pressed. */
    TORIRS_PLUGIN_UI_ACTIVATE = 0,
    /** A checkbox changed: `value` is its new state. */
    TORIRS_PLUGIN_UI_TOGGLE,
    /** A field was edited: `text` is its whole new contents. */
    TORIRS_PLUGIN_UI_TEXT,
    /** A list choice was made: `value` is the index, `text` the chosen entry. */
    TORIRS_PLUGIN_UI_PICK,
};

struct ToriRS_PluginEvUi
{
    /** The id the plugin gave the control in api->win_widget. NULL on
     *  EV_UI_BUILD, which is about the tab rather than about a control. */
    char const* widget_id;
    /** enum ToriRS_PluginUiAction. */
    int action;
    /** Index or flag, per the action; -1 when the action carries none. */
    int value;
    /** The control's text after the change. Never NULL; "" when it has none.
     *  Valid for this dispatch only. */
    char const* text;
};

struct ToriRS_PluginEvAsset
{
    /** The name passed to api->asset_load. Valid for this dispatch only. */
    char const* name;
    /** Bytes now resident; 0 when the read failed. */
    int size;
    /** False when the asset does not exist or could not be read. The plugin is
     *  told either way: a load that simply never fires is indistinguishable
     *  from one still in flight, and a plugin waiting on a file that will
     *  never arrive would wait forever. */
    bool ok;
};

/* ------------------------------------------------------------------------ */
/* Config schema                                                             */
/* ------------------------------------------------------------------------ */

enum ToriRS_PluginConfigType
{
    TORIRS_PLUGIN_CFG_BOOL = 0,
    /** Uses min/max. */
    TORIRS_PLUGIN_CFG_INT,
    /** Stored as "#RRGGBB", read back as 0xRRGGBB. */
    TORIRS_PLUGIN_CFG_COLOR,
    /** Also the carrier for lists, as comma-separated text. */
    TORIRS_PLUGIN_CFG_STRING,
    /** `choices` is a '|'-separated set, rendered as a dropdown. */
    TORIRS_PLUGIN_CFG_ENUM
};

struct ToriRS_PluginConfigItem
{
    /** INI key: [a-z0-9_]. A NULL key terminates the array. */
    char const* key;
    enum ToriRS_PluginConfigType type;
    /** Panel text. NULL hides the key from the panel -- the idiom for state a
     *  plugin persists but the user does not edit by hand. */
    char const* label;
    /** Always textual, exactly as it would appear in the ini. */
    char const* default_value;
    /** CFG_INT only. */
    int min;
    int max;
    /** CFG_ENUM only: "a|b|c". */
    char const* choices;
};

/* ------------------------------------------------------------------------ */
/* World objects                                                             */
/* ------------------------------------------------------------------------ */

/**
 * Where a plugin object's geometry comes from.
 *
 * Both are cache ids rather than bytes a plugin supplies, because a model is
 * not a file the plugin can meaningfully own: it is decoded against the
 * revision's own model codec, lit with the client's light source, and its face
 * colours are indices into the palette the running cache defines. A plugin
 * ships DATA as an asset and names GEOMETRY by id.
 */
enum ToriRS_PluginModelSource
{
    /** A model id from the cache's model table, lit as an actor and drawn as
     *  it decodes. */
    TORIRS_PLUGIN_MODEL_CACHE = 0,
    /** A spotanimtype id: its model with that type's own recolours, retextures,
     *  resize, angle and lighting -- the graphic exactly as the server would
     *  draw it -- and its `seq` bound unless the plugin names another. */
    TORIRS_PLUGIN_MODEL_SPOTANIM
};

/**
 * What draw_hull wraps.
 *
 * The two differ in what they measure, not in how carefully they draw. BOUNDS
 * is the model's bounds cylinder, which has one radius for every horizontal
 * direction: a thing with anything sticking out -- a halberd, a cape, a wing
 * -- is wrapped at that reach on all four sides, so on screen it is a box
 * around the entity rather than an outline of it. That is the right shape when
 * what is being marked is the entity's PRESENCE (it never disappears, it never
 * cuts into the model, and it costs eight projections however dense the mesh),
 * and the wrong one when the outline is meant to read as the entity's own
 * edge.
 *
 * MESH hulls the posed vertices themselves, which is that edge, at one
 * projection per vertex per frame. Prefer it for a few marked entities;
 * BOUNDS stays the default and the sane choice for marking a crowd.
 */
enum ToriRS_PluginHullShape
{
    /** The bounds cylinder as an eight-corner box. Fixed cost. */
    TORIRS_PLUGIN_HULL_BOUNDS = 0,
    /** The model's own posed geometry: tight, and linear in the mesh. */
    TORIRS_PLUGIN_HULL_MESH = 1
};

/* ------------------------------------------------------------------------ */
/* The api table                                                             */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginApi
{
    uint32_t abi_version;

    /* -- bus + diagnostics -- */

    void (*subscribe)(
        struct ToriRS_PluginCtx* ctx,
        enum ToriRS_PluginEvent event,
        ToriRS_PluginHandler handler,
        void* userdata);
    /** Prefixed with the plugin name; goes wherever the client's log goes. */
    void (*log)(struct ToriRS_PluginCtx* ctx, char const* fmt, ...);

    /* -- clocks -- */

    /** World cycle (advances once per 20ms client tick). */
    int (*world_cycle)(struct ToriRS_PluginCtx* ctx);
    uint64_t (*frame_ms)(struct ToriRS_PluginCtx* ctx);

    /* -- world queries; 1 = filled, 0 = absent -- */

    int (*local_player)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPlayerSnap* out);
    /** Iterate: pass -1 to start; returns the next iterator, or -1 when done.
     *  `out` is filled for every returned iterator. */
    int (*npc_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginNpcSnap* out);
    int (*npc_by_slot)(
        struct ToriRS_PluginCtx* ctx,
        int server_slot,
        struct ToriRS_PluginNpcSnap* out);
    int (*player_next)(
        struct ToriRS_PluginCtx* ctx,
        int iter,
        struct ToriRS_PluginPlayerSnap* out);
    /** Ground-item stacks, iterated like npc_next. One entry per (tile, obj)
     *  pair -- a tile holding three different items yields three. */
    int (*obj_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginObjSnap* out);
    /** Locs in the loaded scene, iterated like npc_next. A busy city scene
     *  holds thousands of them, so a caller that wants one kind tests
     *  `loc_id` inside the walk rather than collecting the list first. */
    int (*loc_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginLocSnap* out);

    /* -- input -- */

    /** enum LibToriRS_KeyCode. */
    int (*key_held)(struct ToriRS_PluginCtx* ctx, int keycode);

    /** The tile under the mouse pointer, ABSOLUTE, as the last rendered frame
     *  picked it -- the same tile the client's own click paths would act on.
     *  Returns 0, leaving the outputs untouched, when the pointer is outside
     *  the world viewport or over no terrain at all.
     *
     *  `*out_level` is the WALKED level, the one every other level in this api
     *  is: on a bridge deck it is the plane the player standing there is on,
     *  not the plane the map authored the floor on. */
    int (*hover_tile)(
        struct ToriRS_PluginCtx* ctx,
        int* out_tile_x,
        int* out_tile_z,
        int* out_level);

    /** The nearest entity under the pointer. Returns 0, leaving `out`
     *  untouched, when the pointer is over no entity at all -- which is not
     *  the same as being outside the viewport, and a caller that needs to tell
     *  those apart asks hover_tile too. */
    int (*hover_entity)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginHoverEntity* out);

    /**
     * How high an OVERHEAD hangs above a scene element, in the projector's
     * units -- feed it to api->project as `height_above_ground`.
     *
     * This is the anchor the client's own health bars, hitsplats and chat
     * heads use, so a plugin drawing a name over an npc puts it where the game
     * would have. It is the model's bounds rather than a guess from the
     * footprint: a 1x1 imp and a 1x1 chicken are different heights, and a
     * marker npc with no model at all has to sit somewhere sensible rather
     * than on the floor.
     *
     * 200 for an element with no model yet -- the reference's own
     * `logicalHeight` default, and the reason a model-less npc reads as
     * floating slightly above its tile there too. 0 for an element that is not
     * in the scene at all.
     */
    int (*element_height)(struct ToriRS_PluginCtx* ctx, int element_id);

    /* -- projection: scene-relative fine position -> canvas x/y.
     *    Returns 0 when the point is behind the near plane or off-map. -- */

    int (*project)(
        struct ToriRS_PluginCtx* ctx,
        int fine_x,
        int fine_z,
        int height_above_ground,
        int* out_x,
        int* out_y);

    /* -- config -- */

    int (*cfg_bool)(struct ToriRS_PluginCtx* ctx, char const* key);
    int (*cfg_int)(struct ToriRS_PluginCtx* ctx, char const* key);
    uint32_t (*cfg_color)(struct ToriRS_PluginCtx* ctx, char const* key);
    char const* (*cfg_str)(struct ToriRS_PluginCtx* ctx, char const* key);
    /** Marks the store dirty and raises EV_CONFIG_CHANGED. */
    void (*cfg_set)(struct ToriRS_PluginCtx* ctx, char const* key, char const* value);

    /* -- the client's own variables --
     *
     * READ ONLY, and deliberately: a varp is the server's, and a plugin that
     * wrote one would be telling the client something the server never said.
     * What this is for is the other direction -- a BUILTIN reading the switch
     * the user already has, in the cache's All Settings panel, rather than
     * growing a second one of its own in the plugin roster.
     *
     * Ids are the cache's. 0 for an id this revision does not define, which is
     * the same answer an unset var gives: a plugin that needs to tell those
     * apart is reading a var it should not be reading.
     */

    int (*varbit)(struct ToriRS_PluginCtx* ctx, int varbit_id);
    int (*varp)(struct ToriRS_PluginCtx* ctx, int varp_id);
    /**
     * A colour-row setting, as 0xRRGGBB.
     *
     * The All Settings colour rows do not store a colour: they store
     * `colour + 1`, so that zero can mean "never chosen" for a palette whose
     * every entry -- black included -- is a legal answer. Reading one with
     * api->varp and forgetting the offset gives a colour one unit off, which
     * is invisible, so the arithmetic lives here rather than in each caller.
     *
     * `fallback` is returned for the unset case, and is where the row's own
     * default (the struct's `param_1230`) belongs.
     */
    uint32_t (*setting_color)(
        struct ToriRS_PluginCtx* ctx, int varp_id, uint32_t fallback);

    /* -- minimenu; legal only inside EV_MENU_BUILD (asserted) -- */

    /** Appends a row owned by this plugin. Returns 0 when the menu is full.
     *  The host allocates the client action id and routes the selection back
     *  to this plugin with `tag` in EV_MENU_SELECT. A plugin row can never
     *  become the left-click default: its action id sorts with the
     *  Cancel/Examine group. */
    int (*menu_add)(
        struct ToriRS_PluginCtx* ctx,
        struct ToriRS_PluginEvMenuBuild* menu,
        char const* text,
        uint32_t tag);

    /* -- drawing; legal only inside EV_DRAW_WORLD (asserted).
     *    Colours are 0xRRGGBB. `fill_alpha` is 0..255 where 0 means outline
     *    only; it is the wash's opacity, not the reference's inverted
     *    transparency. -- */

    /**
     * Absolute tile, outlined at per-corner terrain height so it stays
     * coplanar on a slope.
     *
     * The wash has a colour OF ITS OWN, unlike a hull's. A tile marker is read
     * against the ground it is drawn on rather than against a model, so the
     * two halves want to differ: a bright outline stays findable at a glance
     * while the fill is whatever tints the tile without hiding what is
     * standing on it. Pass `rgb` again to get the old single-colour marker.
     * `fill_alpha` still decides whether there is a fill at all.
     */
    void (*draw_tile)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int tile_x,
        int tile_z,
        int level,
        uint32_t rgb,
        uint32_t fill_rgb,
        int fill_alpha);
    /** Convex hull of a scene element: the silhouette that wraps the model in
     *  three dimensions. `shape` picks what is hulled -- see
     *  ToriRS_PluginHullShape. */
    void (*draw_hull)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int element_id,
        uint32_t rgb,
        int fill_alpha,
        int shape);
    void (*draw_line)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x0,
        int y0,
        int x1,
        int y1,
        uint32_t rgb);
    /** Centred on `x`, with `y` as the text BASELINE -- the overlay layer's
     *  text primitive has no other mode, so there is no alignment argument to
     *  pass rather than one that would be quietly ignored. */
    void (*draw_text)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x,
        int y,
        char const* text,
        uint32_t rgb);
    void (*draw_rect)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x,
        int y,
        int w,
        int h,
        uint32_t rgb,
        int fill_alpha);

    /* -- assets --
     *
     * A plugin's own files, in a namespace of its own: `name` is a bare
     * filename and never a path, so one plugin can neither read nor overwrite
     * another's, nor anything outside them. A name carrying a separator or a
     * `..` is refused and logged rather than asserted, because it arrives from
     * a script and a client that aborts on a typo in someone's Lua is worse
     * than one that says no.
     *
     * Reads resolve the SAVED copy first and the SHIPPED copy second, so a
     * plugin can ship a default table and later write over it without the two
     * being different names. Writes only ever land on the saved copy: the
     * shipped one sits under the script directory, which the browser lane
     * cannot write to at all.
     *
     * Everything crosses the IO queue, never fopen -- same reason the scripts
     * themselves do.
     */

    /** Begin loading `name`. Returns 1 when it is already resident (asset_data
     *  answers immediately and no event follows), 0 when a read was queued --
     *  EV_ASSET fires when it lands, whether it succeeded or not. */
    int (*asset_load)(struct ToriRS_PluginCtx* ctx, char const* name);
    /** Resident bytes, or NULL while pending / after a failure. `out_size` may
     *  be NULL. The buffer belongs to the host and lives until asset_release,
     *  another asset_save of the same name, or the plugin stopping. */
    void const* (*asset_data)(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size);
    /** Replace `name` with `size` bytes and queue the write. The resident copy
     *  is updated before returning, so asset_data answers with the new bytes at
     *  once. Returns 1 when accepted. */
    int (*asset_save)(
        struct ToriRS_PluginCtx* ctx,
        char const* name,
        void const* data,
        int size);
    /** Drop the resident copy. The file is untouched. */
    void (*asset_release)(struct ToriRS_PluginCtx* ctx, char const* name);

    /* -- screenshots --
     *
     * Capture the client's own frame and write it out as a PNG.
     *
     * DEFERRED, not immediate: the request is recorded and taken at the end of
     * the frame that asked for it, once the interface has been laid out again.
     * Every caller is inside a handler that runs BEFORE that -- a game event
     * is recognised while the packet is being executed -- so capturing on the
     * spot would photograph the frame before the level-up box, the quest
     * scroll or the drop existed. A plugin that wants the moment to settle
     * further counts ticks of its own and calls this later.
     *
     * `dir` is the one place the asset sandbox does not apply, and the reason
     * is that a screenshot is the user's, not the plugin's: it is write-only,
     * of pixels they are already looking at, and the destination comes from a
     * config key THEY typed. `..` is still refused, so a plugin cannot climb
     * out of a directory the user named.
     *
     * It is read two ways, and the split is what lets one plugin work on both
     * lanes without asking which it is on:
     *
     *   ABSOLUTE (leading '/') -- used as given. A desktop user naming a
     *      folder they can find in a file manager.
     *   RELATIVE, or absent -- under the plugin's own saved-asset directory.
     *      Subdirectories still work, so "Bob/Levels" sorts a browser run's
     *      captures exactly like a desktop one; the browser lane has no path
     *      to name and this is the only place it can write.
     *
     * `name` is a bare filename; the host appends ".png" when it has no
     * extension. Returns 1 when the capture was queued.
     */
    int (*screenshot)(struct ToriRS_PluginCtx* ctx, char const* dir, char const* name);

    /**
     * Local wall-clock time as "YYYY-MM-DD_HH-MM-SS", into `out`.
     *
     * Not frame_ms, which is a monotonic client clock with no relation to a
     * calendar, and not something a script can work out for itself: the
     * sandbox does not link `os`, so a plugin has no date at all without this.
     * The format is the one RuneLite names screenshots with (ImageCapture's
     * TIME_FORMAT), which is also, not by accident, filename-safe under the
     * character set `name` above is held to.
     *
     * Returns 1 on success; `out` is always NUL terminated.
     */
    int (*datestamp)(struct ToriRS_PluginCtx* ctx, char* out, int out_size);

    /* -- world objects --
     *
     * A model the plugin owns, drawn in the scene among the locs and the
     * entities rather than painted over them the way the draw api is. That
     * difference is the whole point: an overlay is always in front, so it
     * cannot stand behind a wall, sink into a slope or sort against an npc,
     * and a beam of light that ignores the building in front of it does not
     * read as being in the world.
     *
     * Handles are per-plugin and are destroyed for it when it stops. Position
     * is an ABSOLUTE tile, so an object survives a scene rebuild: the host
     * re-places it on the new origin rather than the plugin having to notice.
     *
     * Model and sequence loads are asynchronous, exactly as they are for the
     * client's own graphics. An object is simply not drawn until its assets
     * land; object_ready is how a plugin can tell the difference.
     */

    /** A new object, owned by this plugin, inactive and with no model yet.
     *  Returns a handle, or -1 when the plugin is at its object budget. */
    int (*object_create)(struct ToriRS_PluginCtx* ctx);
    void (*object_destroy)(struct ToriRS_PluginCtx* ctx, int object);
    /** Setting a model the object does not already have re-queues its load. */
    void (*object_set_model)(
        struct ToriRS_PluginCtx* ctx,
        int object,
        enum ToriRS_PluginModelSource source,
        int id);
    /** Append one recolour pair, in packed HSL -- the unit the model's face
     *  colours are actually in (see hsl_from_rgb). Applied when the model is
     *  built, so pairs added after it is already drawn take effect on the next
     *  rebuild; clear-then-set is what forces one. */
    void (*object_recolor)(struct ToriRS_PluginCtx* ctx, int object, int hsl_from, int hsl_to);
    /** Drop every recolour pair and rebuild the model from the cache copy. */
    void (*object_clear_recolors)(struct ToriRS_PluginCtx* ctx, int object);
    /** Bind a sequence. -1 leaves the model in its bind pose; a SPOTANIM-source
     *  object that is never given one plays the spotanimtype's own seq. */
    void (*object_set_anim)(struct ToriRS_PluginCtx* ctx, int object, int seq_id, int loop);
    /** Lighting OFFSETS against the actor profile, exactly as a spotanimtype's
     *  own ambient/contrast are applied: 0, 0 is the client's default and a
     *  positive ambient brightens. Not absolute values -- a light source that
     *  ignored the profile would not match the models beside it. */
    void (*object_set_light)(struct ToriRS_PluginCtx* ctx, int object, int ambient, int contrast);
    /** ABSOLUTE tile and level, `height` in the projector's 1/128-of-a-tile
     *  units above the ground (positive raises), `yaw` 0..2047. */
    void (*object_set_position)(
        struct ToriRS_PluginCtx* ctx,
        int object,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*object_set_active)(struct ToriRS_PluginCtx* ctx, int object, int active);
    /** 1 once the model is built and the object is in the scene. */
    int (*object_ready)(struct ToriRS_PluginCtx* ctx, int object);

    /* -- colour --
     *
     * Model faces are not RGB. They are 6-bit hue / 3-bit saturation / 7-bit
     * luminance indices into the revision's palette, which is why a recolour
     * takes packed HSL and why "make this beam #FF0000" needs a conversion
     * rather than a cast.
     */

    /** 0xRRGGBB -> packed HSL. */
    int (*hsl_from_rgb)(struct ToriRS_PluginCtx* ctx, uint32_t rgb);
    /** Packed HSL -> 0xRRGGBB, through the same palette the rasteriser uses. */
    uint32_t (*hsl_to_rgb)(struct ToriRS_PluginCtx* ctx, int hsl);

    /* -- the plugin window -------------------------------------------------
     *
     * ONE window, shared: win_request claims a TAB in it, not a window of its
     * own. That is the sandbox rule, not a simplification -- sixteen plugins
     * that could each open a window could bury the game under sixteen of them,
     * and a plugin cannot be trusted to be reasonable about a resource it does
     * not have to share.
     *
     * Where that tab actually appears is the host's business and the user's:
     * in the game canvas, in an OS window, in a browser tab, or as a panel
     * behind a sidebar button. A plugin declares controls and hears about
     * clicks; it never learns which of those it got.
     *
     * Controls are named by plugin-scoped string ids rather than handles,
     * because a reload rebuilds the tab from nothing: an id survives that and
     * a handle does not, so a saved setting can be routed back to the control
     * it came from on the other side of a reload.
     */

    /**
     * Claim this plugin's tab, titled `tab_title`. Idempotent -- calling it
     * again only renames the tab.
     *
     * @return false when the window is full or the host has none.
     */
    bool (*win_request)(struct ToriRS_PluginCtx* ctx, char const* tab_title);

    /**
     * Append a control. `id` is this plugin's own name for it; `label` is what
     * the user reads, and may be NULL for a control that needs none.
     *
     * @param kind enum ToriRS_PluginWidgetKind.
     * @return false when the per-plugin control budget is spent.
     */
    bool (*win_widget)(
        struct ToriRS_PluginCtx* ctx, int kind, char const* id, char const* label);

    bool (*win_set_text)(struct ToriRS_PluginCtx* ctx, char const* id, char const* text);
    bool (*win_set_checked)(struct ToriRS_PluginCtx* ctx, char const* id, bool on);
    /** `choices` is "a|b|c", as a config schema's enum is. */
    bool (*win_set_options)(
        struct ToriRS_PluginCtx* ctx, char const* id, char const* choices, int selected);

    /** Drop every control on this plugin's tab, to rebuild it from the top. */
    void (*win_clear)(struct ToriRS_PluginCtx* ctx);
};

/* ------------------------------------------------------------------------ */
/* Plugin definition                                                         */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginDef
{
    /** kebab-case id; also the ini section suffix. */
    char const* name;
    /**
     * What a PERSON is shown: "Entity Highlighter", not "entity-highlighter".
     *
     * Separate from `name` because the two answer to different readers. The
     * name is an identity -- it keys the ini section, PluginHost_IndexOf and
     * the manifest entry -- so it cannot be changed to read better without
     * orphaning everyone's saved settings. The title has no such duty and can
     * say whatever is clearest, including words the id has no room for.
     *
     * NULL is allowed and means "derive one from the name", which is what a
     * plugin that has not thought about it gets: separators become spaces and
     * words are capitalised. That fallback exists so no roster row can ever
     * show a raw slug -- not so titles can be skipped. Write one.
     */
    char const* title;
    char const* version;
    /** Higher runs earlier within an event. Ties break on registration order. */
    int priority;
    /** NULL-key-terminated array, or NULL for no config. */
    struct ToriRS_PluginConfigItem const* config;
    /** Start switched off until the user asks for it, or until a saved
     *  `enabled=1` turns it on. For anything that draws on a fresh install: a
     *  client that marks up the screen on first launch without being asked
     *  reads as broken, not as featureful. */
    bool disabled_by_default;
    /**
     * This plugin exists to HOST other plugins; it is machinery, not a
     * feature.
     *
     * Only the settings roster reads it, and only to decide whether to list a
     * row. The Lua adapter registers one plugin per script and is itself
     * registered beside them, which is what makes "a script" and "a C plugin"
     * the same kind of thing everywhere else in the system -- and that
     * uniformity is worth keeping. But it also put a row called "lua" in the
     * roster, sitting among the scripts it runs and looking like a peer of
     * them, and there is nothing a user does with it.
     *
     * So the roster hides an adapter that is working: its scripts are the
     * rows, and they speak for it. It reappears the moment it has something to
     * say -- a load fault to report, or its own switch turned off -- because
     * both are states you have to be able to see to get out of. Hiding it
     * unconditionally would make a broken Lua layer a client with no plugins
     * and no explanation, and a disabled one impossible to switch back on.
     */
    bool adapter;
    /**
     * Not listed in the Plugin settings roster.
     *
     * For a BUILTIN: a feature of the client that happens to be written as a
     * plugin, whose switch is somewhere the user already looks -- the cache's
     * own All Settings panel -- rather than in the roster.
     *
     * The roster row is not merely redundant for those, it is wrong. It would
     * be a second switch over one feature, and the two would disagree the
     * first time either was used: All Settings would say the tile markers are
     * on while the roster had stopped the plugin that draws them, with nothing
     * on either screen to explain the other. One feature, one switch, and for
     * these the switch is the cache's.
     *
     * Different from `adapter`, which hides machinery that has no feature at
     * all and REAPPEARS when it faults or is switched off. A hidden builtin
     * has no such escape hatch and needs none: it is always enabled, and what
     * it does is decided by the varbit it reads.
     */
    bool hidden;
    /** Both may be NULL. `init` is where subscriptions are made. */
    void (*init)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api);
    void (*shutdown)(struct ToriRS_PluginCtx* ctx);
    /**
     * Rebuild this plugin from its source, in place. NULL for a plugin that has
     * no source to reread -- a compiled-in C plugin, where stopping and
     * starting it IS a full reload.
     *
     * Exists because a scripted plugin's reload is a thing only its adapter can
     * do: the host can drop the subscriptions and call init again, but init for
     * a Lua script re-subscribes the SAME function references, so the file's
     * text is never re-executed and a reload changes nothing. This is the hook
     * where the VM tears its state down and builds a new one.
     *
     * Called between the teardown and the restart, with the plugin stopped. The
     * implementation may rewrite the fields of this very struct -- the host
     * holds it by pointer and rereads them afterwards -- which is how a script
     * that grew a config key or a handler comes back with it.
     */
    void (*reload)(struct ToriRS_PluginCtx* ctx);
};

#endif /* TORIRS_PLUGIN_H */
