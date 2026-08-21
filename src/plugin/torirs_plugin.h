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
#define TORIRS_PLUGIN_ABI 1

#define TORIRS_PLUGIN_NAME_MAX 48
#define TORIRS_PLUGIN_MENU_ROWS_MAX 16

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
    /** Far end of the route queue, absolute. Equals true_* when idle. */
    int dest_x;
    int dest_z;
    /** Minimap flag latch, absolute; -1 when unset. Covers the window between
     *  a click and the server's echo, when the route queue is still empty. */
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

    /* -- input -- */

    /** enum LibToriRS_KeyCode. */
    int (*key_held)(struct ToriRS_PluginCtx* ctx, int keycode);

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

    /** Absolute tile, outlined at per-corner terrain height so it stays
     *  coplanar on a slope. */
    void (*draw_tile)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int tile_x,
        int tile_z,
        int level,
        uint32_t rgb,
        int fill_alpha);
    /** Convex hull of a scene element's bounds cylinder: the silhouette that
     *  wraps the model in three dimensions. */
    void (*draw_hull)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int element_id,
        uint32_t rgb,
        int fill_alpha);
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
};

/* ------------------------------------------------------------------------ */
/* Plugin definition                                                         */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginDef
{
    /** kebab-case id; also the ini section suffix. */
    char const* name;
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
    /** Both may be NULL. `init` is where subscriptions are made. */
    void (*init)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api);
    void (*shutdown)(struct ToriRS_PluginCtx* ctx);
};

#endif /* TORIRS_PLUGIN_H */
