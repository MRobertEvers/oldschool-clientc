#ifndef SRC_SCRIPTRUN_SCRIPTRUN_CORE_H
#define SRC_SCRIPTRUN_SCRIPTRUN_CORE_H

/*
 * The perceived world of one in-process script-run bot.
 *
 * WHAT IT IS. The state a client would hold after decoding every packet the
 * server sent its player, and nothing else: numbers, no scene, no models, no
 * interface tree. It is fed the payloads ToriRSServer_Send addresses to the
 * bot (struct ToriRSServer.packet_sink) and decodes them with the CLIENT's own
 * code: gameproto_parse for every packet, the revision's npc-info, player-info
 * and appearance decoders for the entity streams, and the client's world model
 * (world/world.c, game/rs_entity_sync.c) as the store, mutated through the same
 * World_* calls the client's apply makes.
 *
 * WHAT IT MIRRORS. The client applies the entity streams in an async task
 * bound to the whole App (game/task_exec_entity_info.c): it awaits npc configs,
 * models and stances between ops. That cannot run here, so the target/apply
 * rules are restated in scriptrun_core.c, op for op, against the same World
 * and RS_EntitySync, with the App-only side effects (models, scene elements,
 * sounds, chat UI, drive events) left out. Keep the two in step: a change to
 * player_target_op / player_apply_op / npc_target_op / npc_apply_op there is a
 * change here.
 *
 * WHAT IT DOES NOT KNOW. Anything the server never sent: other players'
 * stats, npc hitpoints (only the health bar's fill), server-only varps. The
 * map's locs and collision come from the client's own map loader on each
 * REBUILD_NORMAL, and from the template zones of a REBUILD_REGION.
 */

#include <stdint.h>

struct World;
struct GameProtoRevTable;
struct RSCache_Dat2Disk;

enum
{
    SCRIPTRUN_VARPS = 8192,
    SCRIPTRUN_STATS = 25,
    SCRIPTRUN_INVS = 32,
    SCRIPTRUN_INV_SLOTS = 512,
    SCRIPTRUN_MESSAGES = 256,
    SCRIPTRUN_TEXT = 256,
    SCRIPTRUN_PROJECTILES = 256,
    /* Bloat's hand volleys below 40% put ~320 floor graphics in the
     * 40-tick window (16 shadows + 16 splats every 4 ticks); a full ring
     * is an assert, never a dropped graphic (the dropped one is the newest,
     * the one a bot must dodge). */
    SCRIPTRUN_MAP_ANIMS = 1024,
    SCRIPTRUN_LOCS = 1024,
    SCRIPTRUN_OBJS = 512,
    SCRIPTRUN_MOUNTS = 64,
    SCRIPTRUN_SETTEXTS = 256,
    SCRIPTRUN_SCRIPT_ARGS = 32,
    /* Client logic cycles per server tick: the unit World and the drive's
     * tick() count in (src/app.h APP_SERVER_TICK_LOGIC_CYCLES). */
    SCRIPTRUN_CYCLES_PER_TICK = 30,
};

/* The client's drive events (app/app_plugin_drive_events.c), same order and
 * field meanings, raised from the same packets. */
enum ScriptrunEventKind
{
    SCRIPTRUN_EV_NONE = 0,
    SCRIPTRUN_EV_SUB_OPENED,
    SCRIPTRUN_EV_SUB_MOUNTED,
    SCRIPTRUN_EV_SUB_CLOSED,
    SCRIPTRUN_EV_CHAT_OPENED,
    SCRIPTRUN_EV_SLOT_MOUNTED,
    SCRIPTRUN_EV_RESUME_ANSWERED,
    SCRIPTRUN_EV_VARP_CHANGED,
    SCRIPTRUN_EV_INV_CHANGED,
    SCRIPTRUN_EV_OBJ_ADDED,
    SCRIPTRUN_EV_OBJ_REMOVED,
    SCRIPTRUN_EV_MAP_FLAG,
    SCRIPTRUN_EV_CHAT_MESSAGE,
    SCRIPTRUN_EV_SERVER_TICK,
    SCRIPTRUN_EV_NPC_SPAWN,
    SCRIPTRUN_EV_NPC_DESPAWN,
    SCRIPTRUN_EV_NPC_RETYPE,
    SCRIPTRUN_EV_INV_PACKET,
    SCRIPTRUN_EV_NPC_SEQ,
    SCRIPTRUN_EV_NPC_FACE,
    SCRIPTRUN_EV_KIND_COUNT,
    SCRIPTRUN_EVENTS = 1024,
};

struct ScriptrunEvent
{
    int serial;
    int kind;
    int cycle;
    int a, b, c, d;
};

struct ScriptrunInv
{
    int inv_id; /* -1: unused */
    int size;
    int obj[SCRIPTRUN_INV_SLOTS];   /* -1: empty */
    int count[SCRIPTRUN_INV_SLOTS];
};

struct ScriptrunMessage
{
    int serial;
    int type;
    char name[64];
    char text[SCRIPTRUN_TEXT];
};

struct ScriptrunProjectile
{
    int spotanim;
    int src_x, src_z, dst_x, dst_z, level; /* absolute tiles */
    int target;                            /* the wire's target field */
    int launch_cycle;                      /* world cycle it was received */
    int start_delay, end_delay;            /* cycles after receipt */
};

struct ScriptrunMapAnim
{
    int spotanim;
    int x, z, level; /* absolute */
    int height;
    int delay;
    int cycle; /* world cycle it was received */
};

struct ScriptrunLoc
{
    int x, z, level; /* absolute */
    int shape;
    int angle;
    int loc_id; /* -1: deleted */
    /* The map's placement this row stands for (its loc id), or -1 for a row a
     * zone change appended. A map row a change deleted keeps it, so a zone's
     * full reset (ScriptrunCore_ZoneResetLocs) can put the map's loc back. */
    int map_loc_id;
};

struct ScriptrunObj
{
    int x, z, level; /* absolute */
    int obj_id;
    int count;
};

struct ScriptrunMount
{
    int target_uid;
    int interface_id;
    int type;
};

struct ScriptrunSetText
{
    int component;
    char text[SCRIPTRUN_TEXT];
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
    /* The zone header cursor for zone sub-packets (scene-local). */
    int zone_base_x, zone_base_z, zone_level;
    /* SET_NPC_UPDATE_ORIGIN (scene-local). */
    int npc_origin_valid, npc_origin_x, npc_origin_z;

    int next_element_id;
    int tick;

    int32_t varps[SCRIPTRUN_VARPS];
    int stat_level[SCRIPTRUN_STATS];
    int stat_xp[SCRIPTRUN_STATS];

    struct ScriptrunInv invs[SCRIPTRUN_INVS];

    struct ScriptrunMessage messages[SCRIPTRUN_MESSAGES]; /* ring */
    int message_serial;

    struct ScriptrunProjectile projectiles[SCRIPTRUN_PROJECTILES];
    int projectile_count;
    struct ScriptrunMapAnim map_anims[SCRIPTRUN_MAP_ANIMS];
    int map_anim_count;
    /* Every placed loc the bot knows: the map's (cache level, base id) and
     * every zone change since. loc_id -1 is a removed one. */
    struct ScriptrunLoc* locs;
    int loc_count;
    int loc_capacity;
    struct ScriptrunObj objs[SCRIPTRUN_OBJS];
    int obj_count;

    int toplevel;
    /* The component a resume was sent on and not yet answered (the client's
     * pause_pending): -1 none. Any interface mount or close answers it. */
    int resume_pending;
    struct ScriptrunMount mounts[SCRIPTRUN_MOUNTS];
    int mount_count;
    struct ScriptrunSetText settexts[SCRIPTRUN_SETTEXTS];
    int settext_count;
    struct ScriptrunScriptCall last_script;
    int script_serial;

    int run_energy;
    int logged_out;

    /* Collision, built from the map files on each REBUILD_NORMAL
     * (scriptrun_collision.c). The cache is the client's own, opened by the
     * runner; NULL leaves the bot without collision. */
    struct RSCache_Dat2Disk* cache_disk;
    void* builder; /* struct WorldBuilder: the scene's flag map, kept for loc changes */
    int have_collision;
    int collision_squares;
    int collision_locs;

    struct ScriptrunEvent events[SCRIPTRUN_EVENTS]; /* ring, by serial */
    int event_serial;

    /* The local player's own action seqs as its screen saw them start
     * (the drive's players() `me` row: seq_history, raid seam48). */
    int own_seq[16];
    int own_seq_tick[16];
    int own_seq_n[16];
    int own_seq_count;
    int own_seq_starts;

    /* This bot's copy of the revision's player-info table (the osrs239
     * decoder keeps one file-static table; see ScriptrunCore_Packet). */
    void* player_decoder_state;

    /* Decoder scratch (the client's ops arrays). */
    void* npc_ops;
    void* player_ops;
    int npc_old_list[2048];
    int player_old_list[2048];
    int packets;
    int unhandled;
};

/** A fresh core for one bot. `rev` is the world's wire revision. */
struct ScriptrunCore*
ScriptrunCore_New(struct GameProtoRevTable const* rev);

void
ScriptrunCore_Free(struct ScriptrunCore* core);

/**
 * The login response's player index: what the client learns before the login
 * REBUILD (net/net.c: osrs239_playerinfo_set_local + a synthetic UPDATE_PID).
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

/** One payload the server addressed to this bot, in send order. */
void
ScriptrunCore_Packet(
    struct ScriptrunCore* core,
    int pkt_name,
    const uint8_t* payload,
    int len);

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
 * A zone LOC_ADD_CHANGE (loc_id >= 0) or LOC_DEL (loc_id -1) at an absolute
 * tile on the zone's walked level: the loc in that shape's layer is replaced,
 * collision undone and restamped (WorldBuilder_ApplyLocChange).
 */
void
ScriptrunCore_LocChange(
    struct ScriptrunCore* core,
    int abs_x,
    int abs_z,
    int zone_level,
    int shape,
    int angle,
    int loc_id);

/**
 * UPDATE_ZONE_FULL_FOLLOWS for the zone whose south-west tile is (abs_x0,
 * abs_z0) on the walked level `zone_level`: every loc a zone change put in it
 * goes, and every map loc a change deleted comes back, collision undone and
 * restamped -- the client's zone_full_reset_locs (rs_gameproto_exec.c), which
 * reverts each changed (level, x, z, layer) to the map's loc before the
 * zone's state re-applies what still differs.
 */
void
ScriptrunCore_ZoneResetLocs(
    struct ScriptrunCore* core,
    int abs_x0,
    int abs_z0,
    int zone_level);

/** Release the collision scene (the builder and the loc table). */
void
ScriptrunCore_FreeCollision(struct ScriptrunCore* core);

/** A tile's collision word (COLL_FLAG_*), or -1 outside the scene / unbuilt. */
int
ScriptrunCore_CollisionFlags(
    struct ScriptrunCore const* core,
    int level,
    int abs_x,
    int abs_z);

/**
 * The menu text of a loc's op (1-based) on the rung this bot's varps select,
 * or NULL when that rung offers no such op (scriptrun_collision.c).
 */
char const*
ScriptrunCore_LocOpName(
    struct ScriptrunCore const* core,
    int loc_id,
    int op);

/**
 * A multinpc's live rung over this bot's varps (App_NpctypeResolveMultiId);
 * -1 when the selected slot hides it, the id itself when it is no shell.
 */
int
ScriptrunCore_NpcResolve(
    struct ScriptrunCore const* core,
    int npc_id);

/** A varbit out of the varps this bot was sent. 0 when undefined. */
int
ScriptrunCore_Varbit(
    struct ScriptrunCore const* core,
    int varbit_id,
    int* value);

/** The inventory record for `inv_id`, or NULL when never sent. */
struct ScriptrunInv const*
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

/** The text last set on `component`, or NULL. */
char const*
ScriptrunCore_Text(
    struct ScriptrunCore const* core,
    int component);

#endif
