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
#define TORIRS_PLUGIN_ABI 3

#define TORIRS_PLUGIN_NAME_MAX 48
#define TORIRS_PLUGIN_MENU_ROWS_MAX 16
/** Longest asset name a plugin may ask for, including its extension. */
#define TORIRS_PLUGIN_ASSET_NAME_MAX 64
/** Recolour pairs one world object may carry, matching a spotanimtype's six. */
#define TORIRS_PLUGIN_OBJECT_RECOLORS_MAX 6

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

    /* -- input -- */

    /** enum LibToriRS_KeyCode. */
    int (*key_held)(struct ToriRS_PluginCtx* ctx, int keycode);

    /** The tile under the mouse pointer, ABSOLUTE, as the last rendered frame
     *  picked it -- the same tile the client's own click paths would act on.
     *  Returns 0, leaving the outputs untouched, when the pointer is outside
     *  the world viewport or over no terrain at all. */
    int (*hover_tile)(
        struct ToriRS_PluginCtx* ctx,
        int* out_tile_x,
        int* out_tile_z,
        int* out_level);

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
