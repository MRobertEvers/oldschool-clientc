#ifndef SRC_SCRIPTRUN_SCRIPTRUN_CORE_H
#define SRC_SCRIPTRUN_SCRIPTRUN_CORE_H

/*
 * The perceived world of one in-process script-run bot.
 *
 * WHAT IT IS. The state a client would hold after decoding every packet the
 * server sent its player, and nothing else: numbers, no scene, no models, no
 * interface tree. It is fed the payloads ToriRSServer_Send addresses to the
 * bot (struct ToriRSServer.packet_sink) and decodes them with the CLIENT's own
 * code: the revision's decode by wire opcode (net/proto/), its npc-info, player-info
 * and appearance decoders for the entity streams, and the client's world model
 * (world/world.c, game/rs_entity_sync.c) as the store, mutated through the same
 * World_* calls the client's apply makes.
 *
 * WHAT IT SHARES. Every packet the core decodes goes first through the packet
 * model (game/rs_server_state.h RS_ServerState_Apply), the one copy of the
 * rules the real client applies too, into `model` (scriptrun_model.h): varps,
 * stats, inventories, chat lines, the interface text/hide/colour/event stores
 * and the event ring are written there and read from there. What follows the
 * apply here is the bot's PRESENTATION, as the real client's src/app follows
 * it: re-resolving multinpcs on a varp, the mount table and the stores a
 * leaving group takes with it (RS_ServerState_SlotGroupLeaves), and the
 * stand-ins for what the real client's scripts would read (the last client
 * script call, the make-X and skill-multi values, the count prompt).
 *
 * The zone families follow the real client's rules too, onto its own records
 * (scriptrun_zone.h, ScriptrunCore_ZoneFamilies): ground rows as World obj
 * stacks, the effect log the projectiles and map graphics are read from
 * (game/rs_effect_log.h), the loc animations, and the root's loc change
 * records, which a rebuild re-places. That unit also holds the one list of
 * the real client's parallel task kinds and which this client runs, on its
 * own parallel queue (scriptrun_tasks.h): the loc change and the loc merge.
 *
 * WHAT IT SHARES WITH THE ENTITY STREAMS. PLAYER_INFO and NPC_INFO are
 * applied by the real client's rules, game/rs_entity_apply.h, the one copy
 * both clients call, against the same World and RS_EntitySync. This client is
 * that apply's host (scriptrun_core.c, "the entity streams"): its npc configs
 * are the server's own records of the pack's client configs
 * (ToriRSServer_NpcInfoRecord), its entities have no models, and of the
 * App-only side effects it keeps the chat store, the npc_seq / npc_face
 * stamps, its own sequence history and the wirelog. The interface mounts are
 * restated (the real client's record of what is mounted is its UI tree), and
 * the placed locs: the loc table below stands for the real client's scenery
 * pool, which this client builds no scene for.
 *
 * WHAT IT DOES NOT KNOW. Anything the server never sent: other players'
 * stats, npc hitpoints (only the health bar's fill), server-only varps. The
 * map's locs and collision come from the client's own map loader on each
 * REBUILD_NORMAL, and from the template zones of a REBUILD_REGION.
 */

#include "game/entity_info_scratch.h"
#include "game/rs_effect_rules.h"
#include "game/rs_event_ring.h"
#include "scriptrun/scriptrun_model.h"
#include "scriptrun/scriptrun_zone.h"

#include <stdint.h>

struct World;
struct GameProtoRevTable;
struct ScriptrunTasks;
struct ToriRS_Location;
struct ScriptrunShadow;
struct PktLocAddChange;
struct RevPacket;
struct RSCache_Dat2Disk;

enum
{
    SCRIPTRUN_TEXT = 256,
    /* The effect lists' sizes, the live ring's too (game/rs_effect_rules.h
     * says why each is what it is): what the zone's effect log and loc
     * animations (scriptrun_zone.h) hold alive at once; more is an assert. */
    SCRIPTRUN_PROJECTILES = RS_EFFECT_PROJECTILE_CAP,
    SCRIPTRUN_MAP_ANIMS = RS_EFFECT_MAP_ANIM_CAP,
    SCRIPTRUN_LOC_ANIMS = RS_EFFECT_LOC_ANIM_CAP,
    SCRIPTRUN_MOUNTS = 64,
    SCRIPTRUN_SCRIPT_ARGS = 32,
    /* Client logic cycles per server tick: the unit World and the drive's
     * tick() count in (game/rs_server_tick.h APP_SERVER_TICK_LOGIC_CYCLES). */
    SCRIPTRUN_CYCLES_PER_TICK = 30,
};

/* The drive events: game/rs_event_ring.h's one vocabulary, which the client
 * stamps as RS_EVENT_*, raised from the same packets with the same field meanings
 * (plugin/torirs_plugin_drive.h has the payload table). */
enum
{
    SCRIPTRUN_EV_NONE = RS_EVENT_NONE,
    SCRIPTRUN_EV_SUB_OPENED = RS_EVENT_SUB_OPENED,
    SCRIPTRUN_EV_SUB_MOUNTED = RS_EVENT_SUB_MOUNTED,
    SCRIPTRUN_EV_SUB_CLOSED = RS_EVENT_SUB_CLOSED,
    SCRIPTRUN_EV_CHAT_OPENED = RS_EVENT_CHAT_OPENED,
    SCRIPTRUN_EV_SLOT_MOUNTED = RS_EVENT_SLOT_MOUNTED,
    SCRIPTRUN_EV_RESUME_ANSWERED = RS_EVENT_RESUME_ANSWERED,
    SCRIPTRUN_EV_VARP_CHANGED = RS_EVENT_VARP_CHANGED,
    SCRIPTRUN_EV_INV_CHANGED = RS_EVENT_INV_CHANGED,
    SCRIPTRUN_EV_OBJ_ADDED = RS_EVENT_OBJ_ADDED,
    SCRIPTRUN_EV_OBJ_REMOVED = RS_EVENT_OBJ_REMOVED,
    SCRIPTRUN_EV_MAP_FLAG = RS_EVENT_MAP_FLAG,
    SCRIPTRUN_EV_CHAT_MESSAGE = RS_EVENT_CHAT_MESSAGE,
    SCRIPTRUN_EV_SERVER_TICK = RS_EVENT_SERVER_TICK,
    SCRIPTRUN_EV_NPC_SPAWN = RS_EVENT_NPC_SPAWN,
    SCRIPTRUN_EV_NPC_DESPAWN = RS_EVENT_NPC_DESPAWN,
    SCRIPTRUN_EV_NPC_RETYPE = RS_EVENT_NPC_RETYPE,
    SCRIPTRUN_EV_INV_PACKET = RS_EVENT_INV_PACKET,
    SCRIPTRUN_EV_NPC_SEQ = RS_EVENT_NPC_SEQ,
    SCRIPTRUN_EV_NPC_FACE = RS_EVENT_NPC_FACE,
    SCRIPTRUN_EV_KIND_COUNT = RS_EVENT_KIND_COUNT,
    /* The bot's ring: overwrites its oldest entry when full, uncounted
     * (RS_EVENT_RING_OVERWRITE); the reader clamps its cursor to it. */
    SCRIPTRUN_EVENTS = 1024,
};

/* A LOC_ANIM is kept for SCRIPTRUN_LOC_ANIM_TICKS, the live ring's
 * APP_DRIVE_LOC_ANIM_TICKS (both RS_EFFECT_LOC_ANIM_LIFE_TICKS); its row is
 * struct ScriptrunLocAnim (scriptrun_zone.h). (The Great Olm's head is a loc:
 * its facing and its attacks are told only by these.) */
enum { SCRIPTRUN_LOC_ANIM_TICKS = RS_EFFECT_LOC_ANIM_LIFE_TICKS };

struct ScriptrunLoc
{
    int x, z, level; /* absolute */
    int shape;
    int angle;
    int loc_id; /* -1: deleted */
    /* The map's placement this row stands for (its loc id), or -1 for a row a
     * zone change appended. A map row a change deleted keeps it
     * (ScriptrunCore_MapLocAt reads it for a change's record). */
    int map_loc_id;
    /* The world view the row belongs to: 0 the root scene, 1..15 a boat's
     * deck (its staging-rectangle tiles). A root rebuild keeps deck rows, as
     * the client keeps a view's world across a root re-centre. */
    int view;
    /* This placement's own menu (LOC_ADD_CHANGE_V2, a content loc_add_op):
     * `op_flags` the five shown slots (bit 0 = op1), `ops` a label that
     * replaces the loctype's for a slot when non-empty. A map loc and a plain
     * loc_add show the loctype's menu: all five bits, no labels. */
    int op_flags;
    char ops[5][32];
};

/*
 * A world entity (a boat) as WORLDENTITY_INFO and REBUILD_WORLDENTITY told
 * it: the client's Wev and its Worldview, less everything a renderer needs.
 * The hull's transform is the last target the server sent (the client lerps
 * toward it over one tick; a tick-granular reader needs only the target).
 * base_x/base_z stay 0 until the view's REBUILD_WORLDENTITY names its staging
 * square, as WorldviewRegistry's base does.
 */
enum
{
    SCRIPTRUN_VIEWS = 64, /* WORLDVIEW_MAX: local slots, slot 0 is the root */
};

struct ScriptrunView
{
    int live;
    /* The server's world-entity id; the array index is a local slot, as the
     * client's WorldviewRegistry keeps it (worldview.h). */
    int wire_id;
    int config_id;
    int size_x, size_z; /* deck extent, tiles */
    int priority_group;
    int fine_x, fine_z; /* hull centre, 128 per tile */
    int angle;          /* 0..2047 */
    int op_mask;
    int base_x, base_z; /* staging rectangle SW corner, absolute tiles */
    int level;          /* the plane SET_ACTIVE_WORLD named for it */
};

struct ScriptrunMount
{
    int target_uid;
    int interface_id;
    int type;
};

struct ScriptrunScriptCall
{
    int script_id;
    int argc;
    int int_args[SCRIPTRUN_SCRIPT_ARGS];
    char str_args[4][SCRIPTRUN_TEXT];
    int serial;
};

struct ScriptrunCore
{
    struct GameProtoRevTable const* rev;
    struct World* world;
    struct RS_EntitySync* esync;

    /* The scene: the rebuild's centre zone; absolute = scene + base. */
    int have_scene;
    int rebuild_zone_x, rebuild_zone_z;
    /* The last REBUILD_REGION's template grid, KEPT as the client keeps it
     * (App rebuild_region): the only thing a client is told about an
     * instance's layout. [level*169 + zone_x*13 + zone_z], packed as struct
     * PktMapRebuild documents, centred on rebuild_zone_x/z. A REBUILD_NORMAL
     * clears it. */
    int32_t instance_zones[4 * 13 * 13];
    int instance_active;
    /* The zone header cursor for zone sub-packets (scene-local). */
    int zone_base_x, zone_base_z, zone_level;
    /* SET_NPC_UPDATE_ORIGIN (scene-local). */
    int npc_origin_valid, npc_origin_x, npc_origin_z;

    /* World entities. `active_world` is SET_ACTIVE_WORLD's cursor (0 the
     * root; SERVER_TICK_END resets it); `view_list` is the root view's
     * entity list in the server's order, which WORLDENTITY_INFO's records
     * address positionally (rs_gameproto_exec.c exec_worldentity_info).
     * `local_view` is the view the local player is homed in (0 ashore). */
    struct ScriptrunView views[SCRIPTRUN_VIEWS];
    int view_list[SCRIPTRUN_VIEWS];
    int view_list_count;
    int active_world;
    int active_world_level;
    int local_view;
    int wev_refused;

    int next_element_id;
    int tick;

    /* Every placed loc the bot knows: the map's (cache level, base id) and
     * every zone change since. loc_id -1 is a removed one. */
    struct ScriptrunLoc* locs;
    int loc_count;
    int loc_capacity;
    /* The zone families (scriptrun_zone.h), applied by the real client's
     * rules (ScriptrunCore_ZoneFamilies): the ground rows as World obj stacks
     * (read with ScriptrunCore_Objs), the effect log the projectiles and map
     * graphics are read from (game/rs_effect_log.h), the loc animations. */
    struct ScriptrunZone zone;

    int toplevel;
    /* The component a resume was sent on and not yet answered (the client's
     * pause_pending): -1 none. Any interface mount or close answers it. */
    int resume_pending;
    /* The group in each slot, as the bot's mounts stand: what an IF_OPENSUB
     * or IF_CLOSESUB names as the group leaving (RS_ServerState_SlotGroupLeaves
     * then drops its IF_SETEVENTS arming, and its texts, hides and colours
     * when a different group takes the slot). IF_OPENTOP clears the table;
     * IF_MOVESUB is not followed. */
    struct ScriptrunMount mounts[SCRIPTRUN_MOUNTS];
    int mount_count;
    /* The last RUNCLIENTSCRIPT of `menu_script` (chatbox_multi_init, 58: the
     * chatmenu's title and rows), kept whatever runs after it -- a stat-boost
     * timer (4517, 4518) sent on the menu's tick used to replace it and empty
     * the rows. menu_script -1: every script is kept. */
    struct ScriptrunScriptCall last_script;
    int script_serial;
    int menu_script;
    /* The last RUNCLIENTSCRIPT of `skillmulti_script` (the make-menu's
     * skillmulti_setup, 2046), kept whatever runs after it: a cell press is
     * answered from its mode, max and default quantity (args 0, 1, 20), as the
     * client's own script answers it. skillmulti_script -1: not tracked. */
    int skillmulti_script;
    int skillmulti_mode;
    int skillmulti_max;
    int skillmulti_default;
    int skillmulti_valid;
    /* A p_countdialog prompt the server opened and nobody has answered: the
     * meslayer mode the client would be in (7, "Enter amount:" -- rev 239+
     * sends it as RUNCLIENTSCRIPT `count_script`, 108), 0 when none is up.
     * Answered by the runner's count_answer verb. count_script -1: not tracked. */
    int count_script;
    int count_prompt;

    int logged_out;

    /* Collision, built from the map files on each REBUILD_NORMAL
     * (scriptrun_collision.c). The cache is the client's own, opened by the
     * runner; NULL leaves the bot without collision. */
    struct RSCache_Dat2Disk* cache_disk;
    void* builder; /* struct WorldBuilder: the scene's flag map, kept for loc changes */
    int have_collision;
    int collision_squares;
    int collision_locs;

    /* The drive events, SCRIPTRUN_EVENTS of them (RS_EVENT_RING_OVERWRITE):
     * what the packet model stamps (its sink is this ring) and what the bot's
     * presentation raises (ScriptrunCore_Event). */
    struct RS_Event event_entries[SCRIPTRUN_EVENTS];
    struct RS_EventRing events;

    /* The packet model's records (scriptrun_model.h), bound to this core's
     * World, entity sync and event ring. */
    struct ScriptrunModel model;

    /* The local player's own action seqs as its screen saw them start
     * (the drive's players() `me` row: seq_history, raid seam48). */
    int own_seq[16];
    int own_seq_tick[16];
    int own_seq_n[16];
    int own_seq_count;
    int own_seq_starts;

    /* This bot's copy of the revision's player-info table (the decoders
     * keep one file-static table; see ScriptrunCore_Packet). */
    void* player_decoder_state;

    /* The decoders' op lists, the client's own (game/entity_info_scratch.h):
     * each sized to its packet by EntityInfoScratch_Read. */
    struct EntityInfoScratch info_scratch;
    int packets;
    int unhandled;

    /* The parallel queue (scriptrun_tasks.h): the loc tasks both clients
     * run, pumped after every packet. */
    struct ScriptrunTasks* tasks;

    /* The packet model's shadow (scriptrun_shadow.h): a second model fed the
     * same packets and nothing else, compared with `model` after the bot's
     * presentation. NULL unless TORIRS_SCRIPTRUN_SHADOW names a file; nothing
     * above reads it. */
    struct ScriptrunShadow* shadow;
};

/** A fresh core for one bot. `rev` is the world's wire revision. */
struct ScriptrunCore*
ScriptrunCore_New(struct GameProtoRevTable const* rev);

/* The server's IF_SETEVENTS for (`com_id`, `sub_id`): 1 with the mask in
 * `out_events` when an entry exists, 0 when the server never armed it. */
int
ScriptrunCore_IfEvents(
    struct ScriptrunCore const* core,
    int com_id,
    int sub_id,
    unsigned* out_events);

void
ScriptrunCore_Free(struct ScriptrunCore* core);

/**
 * The login response's player index: what the client learns before the login
 * REBUILD (net/net.c: the table's player_info_set_local + a synthetic UPDATE_PID).
 * Call before the bot's first packet.
 */
void
ScriptrunCore_SetLocalIndex(
    struct ScriptrunCore* core,
    int index);

/** Before the server's tick: advance the clock the entity model reads. */
void
ScriptrunCore_BeginTick(
    struct ScriptrunCore* core,
    int server_tick);

/** One payload the server addressed to this bot, in send order, with the
 *  wire opcode it was sent under. */
void
ScriptrunCore_Packet(
    struct ScriptrunCore* core,
    int wire,
    const uint8_t* payload,
    int len);

/** A control frame the server addressed to this bot (net/torirs_control.h),
 *  in its place among the packets. */
void
ScriptrunCore_Control(
    struct ScriptrunCore* core,
    int type,
    const uint8_t* payload,
    int len);

/** The instance template zone holding absolute tile (x, z) on `level`, from
 *  the kept REBUILD_REGION grid: the template's tile x/z (a zone's south-west
 *  corner) and level, and the zone's clockwise turn. 0 when the scene is not
 *  an instance, the tile is outside it, or the zone is empty. */
int
ScriptrunCore_InstanceZone(
    struct ScriptrunCore const* core,
    int x,
    int z,
    int level,
    int* template_x,
    int* template_z,
    int* template_level,
    int* rotation);

/** The local player's absolute tile; 0 when not yet placed. */
int
ScriptrunCore_LocalTile(
    struct ScriptrunCore const* core,
    int* x,
    int* z,
    int* level);

/** Raise a drive event (the runner raises RESUME_ANSWERED on its own send). */
void
ScriptrunCore_Event(
    struct ScriptrunCore* core,
    int kind,
    int a,
    int b,
    int c,
    int d);

/** Build the scene's collision as the client does (scriptrun_collision.c). */
void
ScriptrunCore_BuildCollision(struct ScriptrunCore* core);

/**
 * An instance's collision from its REBUILD_REGION descriptor grid
 * (`[level * 13 * 13 + zone_x * 13 + zone_z]`, 0 = void), as the client's
 * WorldBuilder_RebuildInstance builds it: template zones copied and turned.
 */
void
ScriptrunCore_BuildInstanceCollision(
    struct ScriptrunCore* core,
    const int32_t* zones);

/**
 * A boat deck's template locs from its REBUILD_WORLDENTITY grid (same
 * descriptor stride as REBUILD_REGION, `zones_x` x `zones_z` used), placed at
 * the view's staging tiles and tagged with `view`. Locs only: a deck's
 * collision is the deck world's, which the root collision map cannot hold.
 */
void
ScriptrunCore_LoadViewLocs(
    struct ScriptrunCore* core,
    int view,
    const int32_t* zones,
    int zones_x,
    int zones_z);

/** Drop every loc row of `view` (a despawned or rebuilt deck). */
void
ScriptrunCore_DropViewLocs(
    struct ScriptrunCore* core,
    int view);

/**
 * A loc change of `view` placed (the loc tasks' place, scriptrun_tasks.c) at
 * an absolute tile and CACHE level, with the placement's own menu
 * (`op_flags`, `ops`); `loc_id` -1 deletes. The loc in that shape's layer is
 * replaced, its collision undone and the new one's stamped
 * (WorldBuilder_ApplyLocChange) -- in the root's collision map only.
 */
void
ScriptrunCore_LocPlace(
    struct ScriptrunCore* core,
    int view,
    int abs_x,
    int abs_z,
    int level,
    int shape,
    int angle,
    int loc_id,
    int op_flags,
    const char ops[5][32]);

/** The cache level a loc on scene tile (`sx`, `sz`) of the root sits at when
 *  a zone names walked plane `zone_level`: one above it on a bridge column
 *  (World_LocCacheLevel; zone_loc_tile_at). */
int
ScriptrunCore_LocCacheLevel(
    struct ScriptrunCore const* core,
    int sx,
    int sz,
    int zone_level);

/** The map's loc in `layer` at an absolute root tile and cache level (the
 *  loc the scene was built with there, whatever has changed it since), its
 *  shape and angle; -1 when the map put none there. */
int
ScriptrunCore_MapLocAt(
    struct ScriptrunCore const* core,
    int abs_x,
    int abs_z,
    int level,
    int layer,
    int* out_shape,
    int* out_angle);

/** Every ground row the bot knows (scriptrun_zone.h ScriptrunZone_Objs):
 *  the root's, then each deck's, in arrival order, absolute tiles. At most
 *  `cap`; returns how many. ScriptrunCore_ObjCount says how many there are. */
int
ScriptrunCore_Objs(
    struct ScriptrunCore const* core,
    struct ScriptrunObjRow* out,
    int cap);

int
ScriptrunCore_ObjCount(struct ScriptrunCore const* core);

/** The zone cursor as it stands (the last header's base and plane, the
 *  SET_ACTIVE_WORLD view), as a ScriptrunZoneAt. */
void
ScriptrunCore_ZoneAt(
    struct ScriptrunCore const* core,
    struct ScriptrunZoneAt* out);

/**
 * Apply `packet`'s zone families (scriptrun_zone.h: ground rows, the effect
 * log, loc animations) to `zone`, AFTER the core has applied the packet (so
 * its cursor, its views and its scene base are the packet's): the zone
 * sub-packets at the cursor, an enclosed zone's entries, UPDATE_ZONE_FULL_FOLLOWS's
 * reset, the *_SPECIFIC forms at their own tiles, a rebuild's shift, and a
 * deck that went. The core's own zone and its shadow's both go through here.
 * `loc_records`: write the zone LOC_ADD_CHANGE / LOC_DEL records here, for a
 * zone that runs no loc tasks (the shadow's); the core's are written by its
 * loc tasks' place (scriptrun_tasks.c).
 */
void
ScriptrunCore_ZoneFamilies(
    struct ScriptrunCore const* core,
    struct ScriptrunZone* zone,
    struct RevPacket const* packet,
    int loc_records);

/** Release the collision scene (the builder and the loc table). */
void
ScriptrunCore_FreeCollision(struct ScriptrunCore* core);

/** Release the decoded loc configs every bot shares. After the last core is
 *  freed: the run's teardown. */
void
ScriptrunCore_FreeLocConfigs(void);

/** A tile's collision word (COLL_FLAG_*), or -1 outside the scene / unbuilt. */
int
ScriptrunCore_CollisionFlags(
    struct ScriptrunCore const* core,
    int level,
    int abs_x,
    int abs_z);

/** The loc a placement shows now: a multiloc's child as the bot's own
 *  varps select it (the client's DriveLocRow.resolved_loc_id), the id
 *  itself for a plain loc, -1 when the rung selects none. */
int
ScriptrunCore_LocResolvedId(
    struct ScriptrunCore const* core,
    int loc_id);

/**
 * The menu text of a loc's op (1-based) on the rung this bot's varps select,
 * or NULL when that rung offers no such op (scriptrun_collision.c).
 */
/* The display name of the rung a loc resolves to for this bot, or NULL. */
char const*
ScriptrunCore_LocName(
    struct ScriptrunCore const* core,
    int loc_id);

char const*
ScriptrunCore_LocOpName(
    struct ScriptrunCore const* core,
    int loc_id,
    int op);

/**
 * Forget every decoded loc config (scriptrun_collision.c), shared by every bot:
 * the next lookup decodes the server's current client record. The bot's half of
 * a CACHE_INVALIDATE control frame for the loc config group (index 2, archive 6), what the
 * client's CacheProvider_InvalidateArchive does with its own copies. Returns
 * how many were dropped.
 */
int
ScriptrunCore_LocConfigsInvalidate(void);

/** Loc config `loc_id` decoded from the server's client record, shared by
 *  every bot; NULL when there is none. */
struct ToriRS_Location*
ScriptrunCore_LocConfig(int loc_id);

/* The label a placed loc's menu shows on slot `op` (1..5), or NULL when the
 * slot is empty or hidden: the placement's mask and labels over the
 * resolved loctype's, in the reference's order (mask, loctype, override). */
char const*
ScriptrunCore_LocRowOpName(
    struct ScriptrunCore const* core,
    struct ScriptrunLoc const* row,
    int op);

/**
 * A multinpc's live rung over this bot's varps (App_NpctypeResolveMultiId);
 * -1 when the selected slot hides it, the id itself when it is no shell.
 */
int
ScriptrunCore_NpcResolve(
    struct ScriptrunCore const* core,
    int npc_id);

/** A varbit out of the varps this bot was sent (the model's VarPManager).
 *  0 when undefined or when its base varp is one the client cannot address
 *  (ScriptrunCore_VarpAddressable). */
int
ScriptrunCore_Varbit(
    struct ScriptrunCore const* core,
    int varbit_id,
    int* value);

/** 1 when `varp_id` is a varp the client's array holds: below the count the
 *  server transmits to (ToriRSServer_VarpClientCount, the cache's varps), or
 *  one the model has been sent. The real client's drive answers not_found
 *  past its VarPManager's count (DriveState_Varp). */
int
ScriptrunCore_VarpAddressable(
    struct ScriptrunCore const* core,
    int varp_id);

/** The container the model holds for `inv_id`, or NULL when never sent. */
struct InvContainer const*
ScriptrunCore_Inv(
    struct ScriptrunCore const* core,
    int inv_id);

/** The interface mounted under `target_uid`, -1 when none. */
int
ScriptrunCore_MountedAt(
    struct ScriptrunCore const* core,
    int target_uid);

/** 1 when interface `interface_id` is the top level or mounted anywhere. */
int
ScriptrunCore_InterfaceOpen(
    struct ScriptrunCore const* core,
    int interface_id);

/** The text the server set on `component` (the model's store: dropped when
 *  its group leaves its slot for another group), or NULL. */
char const*
ScriptrunCore_Text(
    struct ScriptrunCore const* core,
    int component);

/** 1 when the server's last IF_SETHIDE for `component` hid it, 0 when it
 *  showed it, -1 when it sent none since the group was last unloaded (closed,
 *  or replaced in its slot by another group; a group reopened in its own slot
 *  keeps them, RS_ServerState_SlotGroupLeaves). The cache's own
 *  `hidden` flag and a hidden parent layer are not modelled: -1 is "no
 *  packet said", not "shown". */
int
ScriptrunCore_SetHide(
    struct ScriptrunCore const* core,
    int component);

#endif
