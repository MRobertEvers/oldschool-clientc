#ifndef SRC_GAME_RS_ENTITY_APPLY_H
#define SRC_GAME_RS_ENTITY_APPLY_H

/*
 * PLAYER_INFO and NPC_INFO applied to World and the entity sync state: ONE
 * copy of the rules, for both clients.
 *
 * WHAT IT IS. Plain synchronous functions over a decoded op list
 * (streams/rsprot_info.h): the positional tracked lists, adds and removals
 * (an npc removed and added in one packet is MOVED, not respawned), moves
 * (walk, run, crawl, the coordinate snaps, the deltas, exact moves), facing,
 * sequences, attached graphics, overhead text and chat, hit splats and bars,
 * tints and fades, type changes, appearances and the npc customisation
 * blocks. Nothing here names struct App, a scene, a UI tree or a CS2 host.
 *
 * WHAT THE CLIENT SUPPLIES (struct RS_EntityApplyHost). Three kinds of call,
 * each answered synchronously:
 *   - LOOKUPS into the client's own caches: which multinpc rung its varps
 *     choose, an npc type's facts, a hitsplat or headbar type's timing, which
 *     view an absolute tile belongs to, the npc update origin;
 *   - MAKERS for what only the client can build: a new player or npc entity
 *     (with whatever it draws), and a type change applied to one;
 *   - one PRESENT call per thing the client shows or records (a chatbox
 *     line, a drive-ring stamp, an asset want, a script trigger), made at the
 *     point in the op list where it happened, after the World write it
 *     follows.
 * The real client's host is in game/task_exec_entity_info.c, the headless
 * client's in scriptrun/scriptrun_core.c.
 *
 * WHAT IT DOES NOT DO. Await anything. A packet task that needs an npc
 * config before the rules can run awaits it first (RS_EntityApply_NpcTypes
 * lists the types an op list names) and answers `npc_resolve` from what it
 * awaited.
 */

#include "world/entity_facets.h"

#include <stdbool.h>
#include <stdint.h>

struct GameProtoRevTable;
struct PktPlayerAppearance;
struct RS_EntitySync;
struct RsprotInfoOp;
struct World;

/* An npc type's facts as a spawn or a type change gives them to the entity:
 * the shell's size, turn speed and stance set, the drawn rung's name, ops
 * and level (engine/torirs_types.h ToriRS_NpcEntityFacts has the rule). The
 * strings are the host's, valid for the call; "" where the type has none. */
struct RS_EntityNpcType
{
    int size;
    int turn_speed;
    struct WorldEntityFacet_IdleAnimations idle;
    char const* name;
    char const* actions[5];
    int combat_level;
    bool alwaysontop;
    bool minimap_visible;
    bool interactable;
    bool no_rotation_flag;
};

enum RS_EntityNoteKind
{
    /* `slot`'s npc is about to be removed: what the client keeps for it
     * (pending tasks) goes first. */
    RS_ENTITY_NOTE_NPC_GONE = 1,
    /* A new npc entity `idx` is registered under `slot` (its body is the
     * client's to want). */
    RS_ENTITY_NOTE_NPC_SPAWNED,
    /* The add record of npc `idx` has ended: it is placed (announce it, run
     * the cache's npc-add script). */
    RS_ENTITY_NOTE_NPC_ADDED,
    /* npc `idx` (`slot`) was sent sequence `seq` (any id, -1 clears). */
    RS_ENTITY_NOTE_NPC_SEQ,
    /* npc `idx` (`slot`) was sent a face square: `x`, `z` in half-tiles. */
    RS_ENTITY_NOTE_NPC_FACE,
    /* player `idx` (`pid`) took the appearance `appearance`. */
    RS_ENTITY_NOTE_PLAYER_APPEARANCE,
    /* player `idx` (`pid`) was sent sequence `seq` after `delay`. */
    RS_ENTITY_NOTE_PLAYER_SEQ,
    /* An attached graphic `seq` (a spotanim id >= 0) was put on an entity. */
    RS_ENTITY_NOTE_SPOTANIM,
    /* A public line: `chat_type`, `sender` (as printed), `sender_key` (the
     * bare name friend and ignore key on, NULL for none), `text`; said by
     * player `idx`. Before the overhead is set. */
    RS_ENTITY_NOTE_CHAT,
    /* player `idx` (`pid`) was placed by a coordinate op in view `view`. */
    RS_ENTITY_NOTE_PLAYER_PLACED,
    /* player `idx` (`pid`) applied `op`: a facing, an exact move (its tiles
     * resolved, scene-local, in x0 z0 x1 z1) or a fade. For a client that logs
     * what its own player was told. */
    RS_ENTITY_NOTE_PLAYER_OP,
};

struct RS_EntityNote
{
    enum RS_EntityNoteKind kind;
    int idx;
    int slot; /* the npc's server slot, -1 none */
    int pid;  /* the player's server pid, -1 none */
    int seq;
    int delay;
    int x;
    int z;
    int view;
    int x0, z0, x1, z1;
    int chat_type;
    char const* sender;
    char const* sender_key;
    char const* text;
    struct PktPlayerAppearance const* appearance;
    struct RsprotInfoOp const* op;
};

struct RS_EntityApplyHost
{
    void* ctx;

    /* ---- lookups ---- */

    /** The rung this client's varps choose for wire type `base_type` (any
     *  value the op list named, -1 included), or -1 when the rung hides it. */
    int (*npc_resolve)(void* ctx, int base_type);
    /** Type `npc_type` drawn under shell `base_type` (-1: none): 1 and `out`
     *  filled, or 0 when this client has no config for it. */
    int (*npc_type)(void* ctx, int npc_type, int base_type, struct RS_EntityNpcType* out);
    /** A hit of wire hitsplat type `type`: how long it shows and what a full
     *  slot does (WORLD_HITMARK_POLICY_*). */
    void (*hitsplat)(void* ctx, int type, int* out_duration, int* out_policy);
    /** How long a headbar of `type` lingers after its duration, in cycles. */
    int (*headbar_persist)(void* ctx, int type);
    /** The live view whose staging rectangle holds absolute tile x, z, with
     *  the tile in that view's own space; 0 (the root) when none does. */
    int (*home_view)(void* ctx, int abs_x, int abs_z, int* out_x, int* out_z);
    /** The origin NPC_INFO's deltas for view `view` are measured from, in
     *  that view's tiles, and its level. */
    void (*npc_origin)(void* ctx, int view, int* out_x, int* out_z, int* out_level);

    /* ---- makers ---- */

    /** A new player entity on a scene tile, drawing nothing yet: its pool
     *  index, or -1 when the client has no room for it. */
    int (*player_spawn)(void* ctx, int tile_x, int tile_z, int level);
    /** A new npc entity of rung `npc_type` (shell `base_type`) with the
     *  type's facts (RS_EntityApply_NpcSpawned): its pool index, or -1. */
    int (*npc_spawn)(void* ctx, int npc_type, int base_type, int tile_x, int tile_z, int level);
    /** npc `idx` takes rung `npc_type` (shell `base_type`): its type and
     *  facts (RS_EntityApply_NpcRetyped) and whatever it draws. */
    void (*npc_retype)(void* ctx, int idx, int element_id, int npc_type, int base_type);

    /* ---- presentation ---- */

    void (*present)(void* ctx, struct RS_EntityNote const* note);
};

/* What one call needs. `scene_base_x/z` is the root scene's south-west
 * corner, absolute (the rebuild's centre zone less its six zones of margin). */
struct RS_EntityApplyIn
{
    struct World* world;
    struct RS_EntitySync* esync;
    struct GameProtoRevTable const* rev;
    struct RS_EntityApplyHost const* host;
    int scene_base_x;
    int scene_base_z;
};

/* ---- the streams ---- */

void
RS_EntityApply_PlayerInfo(
    struct RS_EntityApplyIn const* in,
    struct RsprotInfoOp const* ops,
    int count);

/** NPC_INFO for view `view` (0 the root; a deck view's own npc list). */
void
RS_EntityApply_NpcInfo(
    struct RS_EntityApplyIn const* in,
    int view,
    struct RsprotInfoOp const* ops,
    int count);

/**
 * The npc wire types `ops` names (NPCTYPE and CHANGE_TYPE, -1 included), each
 * once, into `out` (room for `cap`); returns how many. What a packet task
 * awaits the configs of before RS_EntityApply_NpcInfo, so that `npc_resolve`
 * can answer at once.
 */
int
RS_EntityApply_NpcTypes(
    struct RsprotInfoOp const* ops,
    int count,
    int* out,
    int cap);

/* ---- the type rules, for the makers ---- */

/** A new npc's type facts (after World_NpcSpawn gave it the type's size and
 *  stance set): name, ops, level, the draw flags, the turn speed. */
void
RS_EntityApply_NpcSpawned(
    struct World* world,
    int idx,
    struct RS_EntityNpcType const* type);

/**
 * npc `idx` takes type `npc_type` with facts `type`: World_NpcSetType, then
 * the facts. What the server set on the npc stays (WORLD_NPC_OVERRIDE_*: a
 * name, a level, a stance set).
 */
void
RS_EntityApply_NpcRetyped(
    struct World* world,
    int idx,
    int npc_type,
    struct RS_EntityNpcType const* type);

/** A new player's stance set before any appearance names one: the default
 *  kit's (808 ready, 819-822 walks, 823 turn, 824 run). */
struct WorldEntityFacet_IdleAnimations
RS_EntityApply_PlayerSpawnIdle(void);

/** The appearance block's World half: slots, kit, colours, stance set, name,
 *  level, gender, headicons and the transmog npc. */
void
RS_EntityApply_PlayerAppearance(
    struct World* world,
    int idx,
    struct PktPlayerAppearance const* appearance);

#endif
