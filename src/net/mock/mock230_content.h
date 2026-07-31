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
 *                                       .constant  `^name = value`
 *                                       .prayer  one block per prayer
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
/* Config text                                                         */
/* ------------------------------------------------------------------ */

/*
 * The three-line grammar every LostCity config shares: `[section]` headers,
 * `key=value` lines, `//` comments.
 *
 * Exported because this file is no longer the only reader — mock230_db.c reads
 * `.dbtable`/`.dbrow` with the same grammar, and a second copy of "trim a line"
 * is how two readers of one format start disagreeing about whether a trailing
 * space is significant.
 *
 * All three edit in place and return a pointer into the caller's buffer.
 */

/** Strip a `//` comment and surrounding whitespace, in place. */
char*
mock230_content_clean_line(char* line);

/** Split `key=value` in place. Returns the value, or NULL when there is no `=`. */
char*
mock230_content_split_key_value(char* line);

/** `[name]` section header; returns the name in place, or NULL. */
char*
mock230_content_section_header(char* line);

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
    MOCK230_PACK_VARBIT,
    MOCK230_PACK_INTERFACE,
    MOCK230_PACK_COMPONENT,
    MOCK230_PACK_STAT,
    MOCK230_PACK_PARAM,
    MOCK230_PACK_HITSPLAT,
    /*
     * The server's own namespaces.
     *
     * Nothing in the cache names an enum, struct or dbtable the *server* defines,
     * so their ids are allocated one past the largest id the cache's own group
     * holds — `tools/ss_allocate.py`, LostCity's `pack.max++` rule. They share
     * `pack/<ns>.pack` with the cache's own names where the cache has any
     * (dbtable, dbrow) and own the file outright where it does not (enum, struct).
     * See docs/CONTENT_PACK_PLAN.md §4.
     *
     * `category` is the odd one and is here for the opposite reason: the id is the
     * cache's (an obj record's config opcode 94) and only the *name* is ours, so
     * it has a pack file and no allocator.
     */
    MOCK230_PACK_ENUM,
    MOCK230_PACK_STRUCT,
    MOCK230_PACK_DBTABLE,
    MOCK230_PACK_DBROW,
    MOCK230_PACK_CATEGORY,
    MOCK230_PACK_COUNT
};

/**
 * The namespace a kind is spelled with: `pack/<name>.pack`, and the row
 * `ContentRegister_Find` answers to. Never NULL.
 *
 * Exported because the register is keyed by namespace name and the runtime is keyed
 * by kind, and something has to bridge the two. One table does it (see the
 * implementation); a second spelling of the same list is how a kind ends up
 * loadable and unnameable.
 */
const char*
mock230_content_pack_name(enum Mock230PackKind kind);

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

/*
 * Resolving a table of symbols in one go.
 *
 * The engine addresses a few dozen ids directly — the interfaces it opens, the
 * components it arms, the varbits it writes. LostCity's engine never sees those
 * numbers because its content names all of them; this is the same arrangement
 * for a server whose interface logic is in C: one table per subsystem, resolved
 * once at boot, and nothing downstream holds a literal.
 *
 * A name that does not resolve leaves its `out` at -1, prints, and counts
 * towards mock230_content_error_count — the same treatment a bad config line
 * gets, because it is the same mistake.
 */
struct Mock230SymbolRef
{
    enum Mock230PackKind kind;
    const char* name;
    int* out;
};

/** Resolve `count` refs; returns how many failed. `what` names the caller in
 *  the diagnostic ("bank", "prayer"). */
int
mock230_content_resolve(
    const char* what,
    const struct Mock230SymbolRef* refs,
    int count);

/* ------------------------------------------------------------------ */
/* Constants (`.constant`)                                             */
/* ------------------------------------------------------------------ */

/*
 * LostCity's `^name = value`, scattered through the tree in `.constant` files
 * and referenced with a leading caret. The compiler substitutes them into
 * RuneScript; here they are the tuning numbers the C engine reads — quantity
 * modes, prayer indices, headicon slots. Anything that is a *number the client
 * and server have to agree on* belongs in one, so both ends can be read off the
 * same line.
 *
 * A constant may also be used on either side of a `.enum` val= line, exactly as
 * the reference does (`val=^prayer_thickskin,3`).
 */

/** The literal text of `^name` (with or without the caret), or NULL. */
const char*
mock230_content_constant(const char* name);

/** `^name` as a number. Missing or non-numeric yields `fallback` and a
 *  diagnostic — a constant the engine reads is content it needs. */
int
mock230_content_constant_int(
    const char* name,
    int fallback);

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
/* Varp definitions                                                    */
/* ------------------------------------------------------------------ */

/*
 * A player variable's declaration, from a `.varp` config.
 *
 * The only field the engine acts on is `transmit`, and it is the one that
 * matters: a varp the *client's own CS2* reads has to reach the client, and one
 * that is purely server bookkeeping must not — sending it costs a packet per
 * change and, worse, invites the client to react to something it should not see.
 * LostCity spells that decision `transmit=yes` in a config file, so this does
 * too.
 *
 * `protect` and `scope` are carried but unread: this server has no protected
 * scripts and no persistence. They exist so a config shared with a LostCity
 * tree keeps its meaning rather than being quietly dropped.
 */
struct Mock230VarpDef
{
    int varp_id;
    const char* symbol;
    int transmit;
    int protect;
    /** 0 temp, 1 perm. */
    int scope_perm;
    int clientcode;
};

/** Declaration for a varp, or NULL when nothing declared it. An undeclared
 *  varp is server-only — the safe default, and the one that keeps the mock's
 *  own bookkeeping variables off the wire. */
const struct Mock230VarpDef*
mock230_content_varp(int varp_id);

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */

/*
 * A LostCity `.enum`: a keyed table of `val=key,value` lines with declared
 * input and output namespaces.
 *
 * The mock uses one, for the gameframe layout — 24 slot/interface pairs that
 * were a literal table in C. Keeping the reference's shape rather than
 * inventing a `.gameframe` file means the next keyed table has an obvious home,
 * and that a LostCity `.enum` can be read here unchanged.
 *
 * Order is preserved. The gameframe is mounted in the order it is listed, and
 * the chatbox has to exist before anything writes to it.
 */
enum
{
    MOCK230_ENUM_VALUE_MAX = 256,
};

struct Mock230EnumValue
{
    int key;
    int value;
    /** The output when the enum declares `outputtype=string`, else NULL. Owned
     *  by the def. `value` is meaningless for those entries — read the flag on
     *  the def, never the pointer, or an enum of empty strings looks like an
     *  enum of ints. */
    const char* text;
};

struct Mock230EnumDef
{
    const char* symbol;
    enum Mock230PackKind input_kind;
    enum Mock230PackKind output_kind;
    /*
     * Whether each side is *text* rather than a number.
     *
     * `input_kind`/`output_kind` cannot carry this: they name the pack a side
     * resolves against, and both `int` and `string` resolve against no pack at
     * all, so both arrive as MOCK230_PACK_COUNT. The RuneScript `enum` opcode
     * has to know the difference because the output type decides which *stack*
     * it pushes onto — get it wrong and every later value in the script is read
     * off the wrong stack.
     */
    int input_is_string;
    int output_is_string;
    /** `default=`. What a key with no entry yields — 0 / "null" in the
     *  reference, and the reason a missing key is not an error. */
    int default_int;
    const char* default_text;
    struct Mock230EnumValue values[MOCK230_ENUM_VALUE_MAX];
    int count;
};

/** An enum by name, or NULL. */
const struct Mock230EnumDef*
mock230_content_enum(const char* symbol);

/**
 * An enum by the id the `enum` namespace gives its name, or NULL.
 *
 * This is the lookup RuneScript needs and the C consumers do not: a compiled
 * script carries the *number* tools/ss_allocate.py assigned, never the name. The
 * two lookups are the same table read two ways rather than two tables.
 */
const struct Mock230EnumDef*
mock230_content_enum_by_id(int enum_id);

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
/* Prayers (`.prayer`)                                                 */
/* ------------------------------------------------------------------ */

/*
 * One block per prayer, in the order the prayer book lists them.
 *
 * LostCity keeps the same table as a `.dbrow` per prayer over a `prayers`
 * `.dbtable`, with the drain rates and headicons in `.enum`s beside it. This is
 * that table flattened into the `[symbol]` + `key=value` shape the rest of this
 * tree already uses — a `.dbtable` reader would be a second config grammar for
 * one table of 29 rows. The field names are the reference's.
 *
 * `button` is the *component* the prayer sits on, named rather than derived:
 * the book has 30 buttons and this revision fills 29 of them, so "first button
 * plus index" is a coincidence that holds today.
 */
enum
{
    /** Storage ceiling, not a count: `prayer_active` is a 32-bit mask, so a
     *  30th prayer is free and a 33rd needs a wider field. The number the
     *  engine loops over is however many blocks the tree declares. */
    MOCK230_PRAYER_MAX = 32,
};

/** Which stat bonus a prayer claims. Two prayers sharing any group cannot be up
 *  together, which is why this is a mask and not a group id — Piety claims
 *  three at once. */
enum Mock230PrayerGroup
{
    MOCK230_PRAYER_GROUP_DEFENCE = 1 << 0,
    MOCK230_PRAYER_GROUP_STRENGTH = 1 << 1,
    MOCK230_PRAYER_GROUP_ATTACK = 1 << 2,
    MOCK230_PRAYER_GROUP_RANGED = 1 << 3,
    MOCK230_PRAYER_GROUP_MAGIC = 1 << 4,
    MOCK230_PRAYER_GROUP_OVERHEAD = 1 << 5,
};

struct Mock230PrayerDef
{
    const char* symbol;
    /** Display name, as the "You need a Prayer level of N to use X." message
     *  spells it. */
    char* name;
    int level;
    /** Drain units per tick, weighed against 60 + 2 * prayer bonus. */
    int drain;
    /** A mask of Mock230PrayerGroup. */
    int groups;
    /** Overhead sprite index, or -1 for the prayers that draw none. */
    int headicon;
    /** Packed component uid of the button that toggles it. */
    int button;
    /** The cache varbit that says this prayer is currently on, or -1.
     *
     *  Not decoration: the prayer book's own clientscript reads these to draw
     *  the lit border, and the overhead-icon and quick-prayer scripts read
     *  them too. `player->prayer_active` is the server's copy; without the
     *  varbit the client never learns a prayer went on, so the book stays dark
     *  no matter what the server believes. */
    int varbit;
};

/** The prayer at `index`, or NULL. Index is the bit position in
 *  `player->prayer_active`, which is the order the blocks were read in. */
const struct Mock230PrayerDef*
mock230_content_prayer(int index);

/** How many prayers the tree declares. */
int
mock230_content_prayer_count(void);

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

/**
 * What `configs/all.param` declares param `param_id`'s value to be, as the
 * cache's own one-character type code, or 0 when nothing declares it.
 *
 * Only `'s'` is a string. Every other letter is an integer of some flavour —
 * `i` plain, `d` a coord, `o` an obj, `1` a boolean — and the VM has one int
 * stack for all of them. The distinction is the whole reason the `*_param`
 * opcodes are marked `runtime_typed`.
 */
char
mock230_content_param_type(int param_id);

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

/**
 * Report a rejected config line from *another* reader, counting it here.
 *
 * `mock230_db.c` reads `.dbtable`/`.dbrow` and has to fail the same way this file
 * does, or a tree with a broken dbrow starts anyway and the fight is quietly
 * wrong. One counter, one prefix, one exit status.
 */
void
mock230_content_report_error(
    const char* fmt,
    ...);

#endif
