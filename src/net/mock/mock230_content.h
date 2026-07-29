#ifndef SRC_NET_MOCK_MOCK230_CONTENT_H
#define SRC_NET_MOCK_MOCK230_CONTENT_H

/*
 * The mock's content tree: LostCity's pack files, config syntax and map format,
 * read straight off disk.
 *
 *   content/pack/<namespace>.pack       `id=name`, one file per namespace
 *   content/scripts/<area>/configs/     .npc  `[symbol]` + `key=value` + `param=`
 *                                       .param  parameter declarations
 *                                       .loc  loc overlays (doors, stairs)
 *   content/maps/m<x>_<z>.jm2           `==== NPC ====` / `==== OBJ ====`
 *   content/scripts/<area>/             .rs2  behaviour (see mock230_scripts.c)
 *
 * Why a reader rather than a packer: LostCity's own tooling compiles this tree
 * into the binary configs its cache serves, because its *client* has to see the
 * npcs and locs the content invents. Nothing here invents one — every npc, obj
 * and loc already exists in cache.osrs230 exactly as OldSchool ships it, so a
 * config block is an **overlay** carrying only what a cache cannot state:
 * hitpoints, combat levels, aggression and what a door opens into. Reading the
 * text at boot costs about a millisecond and removes a build step from the edit
 * loop. `mock230_pack` is the validator, not a required build stage.
 *
 * The one thing that *is* packed is a derived cache — see mock230_pack.c
 * --cache-out — for the case where an overlay should be visible to the client
 * too.
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Symbols                                                             */
/* ------------------------------------------------------------------ */

enum Mock230PackKind
{
    MOCK230_PACK_NPC = 0,
    MOCK230_PACK_OBJ,
    MOCK230_PACK_LOC,
    MOCK230_PACK_SEQ,
    MOCK230_PACK_SPOTANIM,
    MOCK230_PACK_INV,
    MOCK230_PACK_VARP,
    MOCK230_PACK_INTERFACE,
    MOCK230_PACK_COMPONENT,
    MOCK230_PACK_STAT,
    MOCK230_PACK_PARAM,
    MOCK230_PACK_COUNT
};

/** Id for a symbol, or -1. `null` resolves to -1 without a diagnostic, which is
 *  what LostCity's `default=null` params mean. */
int
mock230_content_symbol(
    enum Mock230PackKind kind,
    const char* name);

/** Symbol for an id, or NULL. Only for diagnostics — the reverse map is a
 *  linear scan. */
const char*
mock230_content_symbol_name(
    enum Mock230PackKind kind,
    int symbol_id);

/* ------------------------------------------------------------------ */
/* Combat parameters                                                   */
/* ------------------------------------------------------------------ */

/*
 * LostCity's combat.param names, in the order the OldSchool cache's own param
 * table numbers them — which is the whole point of this enum. Param ids 0..11
 * in an OldSchool obj or npc record ARE these bonuses (OpenRune's
 * cache/src/main/kotlin/org/alter/ParamMapper.kt documents the mapping and
 * cache.osrs230 was checked against it: bronze scimitar reads +7 slashattack,
 * +6 strengthbonus, attackrate 4). So the loader can seed every bonus from the
 * cache and a `param=` line in a config is a genuine override.
 *
 * Ordering is load-bearing twice over: `MOCK230_PARAM_STABATTACK + style` picks
 * the attack bonus for a damage type and `+ MOCK230_PARAM_STABDEFENCE` the
 * defence one, exactly as OpenRune's MeleeCombatFormula indexes BonusSlot.
 */
enum Mock230CombatParam
{
    MOCK230_PARAM_STABATTACK = 0,
    MOCK230_PARAM_SLASHATTACK = 1,
    MOCK230_PARAM_CRUSHATTACK = 2,
    MOCK230_PARAM_MAGICATTACK = 3,
    MOCK230_PARAM_RANGEATTACK = 4,
    MOCK230_PARAM_STABDEFENCE = 5,
    MOCK230_PARAM_SLASHDEFENCE = 6,
    MOCK230_PARAM_CRUSHDEFENCE = 7,
    MOCK230_PARAM_MAGICDEFENCE = 8,
    MOCK230_PARAM_RANGEDEFENCE = 9,
    MOCK230_PARAM_STRENGTHBONUS = 10,
    MOCK230_PARAM_PRAYERBONUS = 11,
    MOCK230_PARAM_BONUS_COUNT = 12
};

/* Cache param ids that are not bonuses. `ATTACKRATE` is ticks between swings;
 * `RANGEBONUS` is the ranged strength bonus, which OldSchool keeps far away
 * from the contiguous block for reasons of its own. */
enum
{
    MOCK230_CACHE_PARAM_ATTACKRATE = 14,
    MOCK230_CACHE_PARAM_RANGEBONUS = 189,
};

/* Damage types, matching LostCity's `damagetype` param: which of the three
 * melee bonuses a swing rolls with, and which defence it rolls against. */
enum Mock230DamageType
{
    MOCK230_DAMAGE_STAB = 0,
    MOCK230_DAMAGE_SLASH = 1,
    MOCK230_DAMAGE_CRUSH = 2,
};

/* ------------------------------------------------------------------ */
/* NPC definitions                                                     */
/* ------------------------------------------------------------------ */

/** LostCity `huntmode`: whether an npc starts fights on its own. */
enum Mock230HuntMode
{
    MOCK230_HUNT_NONE = 0,
    MOCK230_HUNT_AGGRESSIVE,
};

struct Mock230NpcDef
{
    int npc_id;
    /** Points into the pack's own storage; never freed separately. */
    const char* symbol;

    /* Authored — a cache states none of these. 0 hitpoints means the npc has no
     * combat block at all, which is how "not a fighter" is spelled. */
    int hitpoints;
    int attack;
    int strength;
    int defence;
    int magic;
    int ranged;

    int respawnrate; /* ticks from despawn to respawn */
    int wanderrange; /* 0 = stays put */
    int nomove;      /* moverestrict=nomove */

    enum Mock230HuntMode huntmode;
    int huntrange;

    /* Seeded from the cache's params, overridable with `param=`. */
    int bonus[MOCK230_PARAM_BONUS_COUNT];
    int attackrate;
    int attackrange;
    int damagetype;

    int attack_anim;
    int defend_anim;
    int death_anim;
    int death_drop; /* obj id, -1 for `param=death_drop,null` */

    /** The block stated `hitpoints=`. Everything else has a defensible
     *  default; hitpoints is the one that says "this is meant to be fought",
     *  and the difference matters to the validator — a speaking npc inherits
     *  the engine's 10 hitpoints and is not a combat block. */
    int authored_combat;
};

/** Definition for an npc type, or NULL when the content tree has no block for
 *  it. Every caller must cope with NULL: a spawn is allowed to name an npc that
 *  no config describes, and gets engine defaults. */
const struct Mock230NpcDef*
mock230_content_npc(int npc_id);

/** Engine defaults, which are OpenRune's NpcCombatDef.DEFAULT: 10 hitpoints,
 *  4-tick attacks, 25-tick respawn, human unarmed animations. Never NULL. */
const struct Mock230NpcDef*
mock230_content_npc_default(void);

/* Obj bonuses are not repeated here: they live on `struct Mock230ObjInfo` in
 * mock230.h, beside the wearpos fields, because both come out of the same
 * one-pass decode of the obj config group. A second decode to build a parallel
 * table would cost a tenth of a second at boot and buy nothing. */

/* ------------------------------------------------------------------ */
/* Loc definitions (doors, gates, stairs)                              */
/* ------------------------------------------------------------------ */

/*
 * Only doors need a category. Stairs and ladders do not appear here at all:
 * their direction is already in the cache, as the loc's own menu text
 * ("Climb-up", "Climb-down"), so the engine reads it there rather than making
 * content restate it. A config that repeats what the cache says is a config
 * that can disagree with it.
 */
enum Mock230LocCategory
{
    MOCK230_LOC_CATEGORY_NONE = 0,
    MOCK230_LOC_CATEGORY_DOOR_CLOSED,
    MOCK230_LOC_CATEGORY_DOOR_OPENED,
};

struct Mock230LocDef
{
    int loc_id;
    const char* symbol;
    enum Mock230LocCategory category;
    /** `param=next_loc_stage,<symbol>`: what this loc becomes when used. -1
     *  when it has none, which makes the op a no-op rather than a crash. */
    int next_loc_stage;
};

const struct Mock230LocDef*
mock230_content_loc(int loc_id);

/* ------------------------------------------------------------------ */
/* Map spawns (content/maps/m<x>_<z>.jm2)                              */
/* ------------------------------------------------------------------ */

struct Mock230MapNpcSpawn
{
    int npc_id;
    int x, z, level; /* absolute tile */
};

struct Mock230MapObjSpawn
{
    int obj_id;
    int count;
    int x, z, level;
};

/** Every npc spawn in the tree, in file order. */
const struct Mock230MapNpcSpawn*
mock230_content_npc_spawns(int* count);

const struct Mock230MapObjSpawn*
mock230_content_obj_spawns(int* count);

/* ------------------------------------------------------------------ */
/* Loading                                                             */
/* ------------------------------------------------------------------ */

/**
 * Read the whole tree.
 *
 * **Call after `mock230_objinfo_load` and `mock230_npcinfo_load`.** Bonuses,
 * attack rate and damage type are seeded from the cache params those two
 * already decoded, and the config blocks overlay that. Loading in the other
 * order silently produces npcs with no bonuses, which reads as a combat-formula
 * bug rather than a load-order one.
 *
 * Returns the number of npc definitions read, or 0 when `dir` does not exist —
 * which is not an error. Every consumer falls back to engine defaults, the way
 * an absent script pack falls back to the hardcoded C behaviour.
 */
int
mock230_content_load(const char* dir);

void
mock230_content_free(void);

/** Diagnostics: what the last load rejected. Zero means the tree is clean. */
int
mock230_content_error_count(void);

#endif
