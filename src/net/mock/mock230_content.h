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
 *                                       .dbtable / .dbrow  (see mock230_db.h)
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
    MOCK230_PACK_HEALTHBAR,
    /*
     * Sound effects, `pack/4_soundeffects.pack`, where the id *is* the name
     * (`1352=synth_1352`).
     *
     * A namespace whose names carry no information looks pointless, and is not:
     * without it a `param=attack_sound_stance1,synth_1352` row has no kind to
     * resolve against, so every weapon sound overlay fails the pack validator.
     * That is why no weapon in this tree states an attack sound, which is why
     * every weapon swings the `attack_sound_stanceN` default. See
     * docs/WEAPON_FX.md §6.6.
     */
    MOCK230_PACK_SYNTH,
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

/** The `default=` declared for a param in `configs/all.param`, or 0 when it
 *  declares none — the cache decoder's own zeroed-record value. This is what
 *  answers `oc_param`/`nc_param` for a record that carries no row; 365 of the
 *  469 declared defaults are -1, so 0-for-absent and -1 are both real answers
 *  and neither is a sentinel. */
int
mock230_content_param_default(int param_id);

/** Id for a symbol, or -1. `null` resolves to -1 without a diagnostic, which is
 *  what LostCity's `default=null` params mean. */
int
mock230_content_symbol(
    enum Mock230PackKind kind,
    const char* name);

/*
 * The same lookup, but able to say *why* it answered -1.
 *
 * `mock230_content_symbol` cannot: it maps the literal `null` to -1 by design —
 * that is what `param=death_drop,null` means — and it maps a name nothing knows
 * to -1 as well. A caller that only sees the number cannot tell "the author said
 * nothing" from "the author misspelled `bones`", so `param=death_drop,bones_TYPO`
 * used to load at 0 errors and drop nothing. It reads exactly like a correct
 * npc.
 *
 * That is triage §13 bar 1 — an unresolved name is a pack-time error, never a
 * default — so every caller that resolves an *authored* name goes through this
 * and reports on 0. Returns:
 *
 *   1, *out_id = -1   the literal `null`: a stated absence
 *   1, *out_id = id   a resolved name
 *   0, *out_id = -1   nothing in that namespace is spelled this way
 */
int
mock230_content_symbol_checked(
    enum Mock230PackKind kind,
    const char* name,
    int* out_id);

/**
 * Walk a namespace's symbols. Returns the entry count for the kind; when
 * `index` is inside it, fills `*out_id` and `*out_name` (the name is the pack's
 * own storage and lives as long as the tree stays loaded).
 *
 * The one thing here that iterates rather than looks up, and it exists because a
 * *derived* namespace has a bar the others do not: `pack/category.pack` is a set
 * of claims about what this cache's records carry, so validating it means asking
 * the question from the pack's side — "is anything actually in this category" —
 * which a name-to-id lookup cannot express.
 */
int
mock230_content_symbol_walk(
    enum Mock230PackKind kind,
    int index,
    int* out_id,
    const char** out_name);

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
    MOCK230_PARAM_BONUS_COUNT = 12,
    /*
     * How many authored `param=` rows one npc block can carry, for the by-id
     * copy `npc_param` reads. `apply_param` knows twenty names — the twelve
     * bonuses plus eight singles — so this is that with room to grow, and
     * overflow is an error rather than a silent drop.
     */
    MOCK230_NPCDEF_PARAM_MAX = 24
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

/** Waypoints one patrol route may carry. The reference's longest is ten. */
#define MOCK230_NPC_PATROL_MAX 16

/** `healthbar` unstated: the encoder substitutes the standard bar. Distinct
 *  from -1, which is `healthbar=null` — a record saying it has no bar. */
#define MOCK230_NPC_HEALTHBAR_UNSET (-2)

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
    /* Ticks the corpse lies there once the death *animation* has started — the
     * reference's `npc_delay(1)` in `[proc,npc_death]`, which is two. The blow
     * itself is one tick earlier than that, plus `npc_arrivedelay`. */
    int death_delay;
    int wanderrange; /* 0 = stays put */
    int blockwalk;   /* 0 none, 1 npc, 2 all, 3 player; default 1 (npc) */
    int blocksight;  /* default 0 */
    /* 0 normal, 1 blocked, 2 blocked_normal/los, 3 indoors, 4 outdoors,
     * 5 nomove, 6 passthru — keep nomove working as today. */
    int moverestrict;
    int nomove; /* derived/compat: set when moverestrict==5 */
    /**
     * NpcType.turnspeed, restated on the server. -1 = not stated, use the
     * cache record's own value.
     *
     * The cache carries this field and the client honours it, but the *server*
     * is what sets the FACE_ENTITY latch an npc turns toward, and the server
     * boots from whichever cache MOCK230_CACHE_DIR names — normally the
     * pristine one, not a bake. A fixture that must not turn therefore has to
     * say so somewhere the server reads unconditionally, or it turns on the
     * wire and only a baked client stops it on screen.
     */
    int turnspeed;

    enum Mock230HuntMode huntmode;
    int huntrange;
    /**
     * How far from its spawn tile this npc will follow a target — LostCity's
     * `maxrange`, and its leash.
     *
     * The reference checks it in `Npc.targetWithinMaxRange` and drops the target
     * the moment the check fails, which is what stops a monster following a
     * player out of its area. Same default as the reference's NpcType: 7.
     */
    int maxrange;
    /** LostCity `givechase=no`: this npc drops its target rather than following
     *  it a single tile. Default yes, as the reference's NpcType has it. */
    int givechase;
    /**
     * `retaliate=no`: being hit does not give this npc a target.
     *
     * Ours, not the reference's. `mock230_combat_hit_npc` fights back for any
     * npc that is hit "whatever its hunt mode says", which is right for
     * everything that fights and wrong for the handful of npcs that are scenery
     * with hitpoints. `givechase=no` is not the same statement — it drops the
     * target after one step, so the npc has still stopped, turned and taken
     * that step — and `defaultmode=none` is not either, because the latch is
     * `combat_target` and the npc phase hands movement to combat while one is
     * live, ahead of any mode.
     *
     * The Inferno's Ancestral Glyph is the case: the adds chew on it for the
     * whole Zuk phase and it must walk its row through all of it. Default yes,
     * so every other npc keeps fighting back.
     */
    int retaliate;
    /**
     * Which healthbar config this npc's hits raise, `-1` for none.
     *
     * The reference's headbar update mask carries a healthbar id, and a bar
     * exists on an actor only while the server has sent that mask for it — the
     * hitsplat mask is unrelated and carries no health at all. So "this npc has
     * no bar" is not a flag there, it is simply an id the server never sends,
     * and the id it does send selects the sprites, the fade and the health
     * scale the fill is expressed in.
     *
     * `healthbar=null` is that absence, spelled. Ours defaults to
     * `MOCK230_NPC_HEALTHBAR_UNSET` rather than to the standard bar's id
     * because the id is a symbol resolved after this default is seeded; the
     * encoder substitutes `healthbar_0` for the sentinel, so an npc that says
     * nothing keeps exactly the bar it has today.
     */
    int healthbar;

    /* Seeded from the cache's params, overridable with `param=`. */
    int bonus[MOCK230_PARAM_BONUS_COUNT];
    int attackrate;
    int attackrange;
    int damagetype;

    int attack_anim;
    int defend_anim;
    int death_anim;
    int death_drop; /* obj id, -1 for `param=death_drop,null` */

    /*
     * The sounds that go with the three animations above: swinging, being hit,
     * and dying. Synth ids, -1 for silent.
     *
     * -1 and not 0, because sound effect 0 is a real clip and "states no sound"
     * has to be a value rather than an absence — a 0 default is exactly what
     * made every weapon in the game play the same sound on every swing
     * (docs/WEAPON_FX.md 6.6). Most npcs are legitimately -1: no public source
     * describes npc combat sound for a modern cache, so silence is the honest
     * answer for the ~96% nothing covers (docs/NPC_SOUNDS_ANIMS.md).
     */
    int attack_sound;
    int defend_sound;
    int death_sound;

    /*
     * The same authored params again, filed under their param *id*.
     *
     * The fields above are what the engine reads; this is what `npc_param`
     * reads, because a script names a param by id and has no way back to a C
     * field name. Rank 1 in CONTENT_ARCHITECTURE.md §3.1's sense — an authored
     * overlay value that overrides the cache record's own param table — so the
     * opcode reads this first and `mock230_npc_param` second.
     *
     * Only params whose name the pack knows land here. `death_drop` (2634) is
     * the one that has always been script-visible, and it was visible by having
     * its name spelled in C.
     */
    struct
    {
        int32_t key;
        int32_t value;
    } params[MOCK230_NPCDEF_PARAM_MAX];
    int param_count;

    /** The block stated `hitpoints=`. Everything else has a defensible
     *  default; hitpoints is the one that says "this is meant to be fought",
     *  and the difference matters to the validator — a speaking npc inherits
     *  the engine's 10 hitpoints and is not a combat block. */
    int authored_combat;

    /*
     * Patrol route — LostCity's `defaultmode=patrol` plus `patrol1..patrolN`.
     *
     * A patrol is not a wander with a bigger radius. Hans walks a fixed ring
     * round the castle grounds and pauses at one corner of it, which is what
     * makes him findable: a wanderer of the same range is somewhere random in a
     * hundred tiles. The reference spells each waypoint
     * `<level>_<mapx>_<mapz>_<localx>_<localz>,<pause>`, so the absolute tile is
     * `mapx * 64 + localx`.
     *
     * Sized at 16 because the reference's longest route is ten and a route long
     * enough to need more is one an author should be splitting. Overflow is a
     * content error, not a truncation.
     */
    struct
    {
        int x;
        int z;
        int level;
        /** Ticks to stand still on arrival before moving to the next one. */
        int pause;
    } patrol[MOCK230_NPC_PATROL_MAX];
    int patrol_count;
    /** The mode an npc starts in and returns to. `MOCK230_NPCMODE_NONE` unless
     *  the block says otherwise; a `wanderrange` still implies wander, which is
     *  what every npc without a `defaultmode=` line gets. */
    int defaultmode;
};

/** Definition for an npc type, or NULL when the content tree has no block for
 *  it. Every caller must cope with NULL: a spawn is allowed to name an npc that
 *  no config describes, and gets engine defaults. */
const struct Mock230NpcDef*
mock230_content_npc(int npc_id);

/** Engine defaults, which are OpenRune's NpcCombatDef.DEFAULT: 10 hitpoints,
 *  4-tick attacks, a 3-tick corpse, 25-tick respawn, human unarmed animations.
 *  Never NULL. */
const struct Mock230NpcDef*
mock230_content_npc_default(void);

/**
 * An authored `param=` row off an npc block, by param id.
 *
 * Returns 1 and writes `*out` when the block stated the param, 0 when it did
 * not — which is a different answer from "the value is 0", and the caller needs
 * both: `npc_param` reads this first (rank 1) and falls back to the cache
 * record's own param table (rank 0), then to the param's declared `default=`.
 *
 * A NULL def reports 0, so the caller does not branch twice.
 */
int
mock230_content_npc_param(
    const struct Mock230NpcDef* def,
    int param_id,
    int32_t* out);

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
    /**
     * `wholewrite=allow` — this varp may be written whole even though the cache
     * packs varbits into it.
     *
     * The escape hatch for docs/LOSTCITY_PORT_TRIAGE.md §7.5's class, and it is
     * declared in content because two readers need the same answer: sscompile
     * refuses the write without it, and `mock230_world.c` counts one at runtime
     * for the writers a compiler cannot see (a `::` cheat, C, a packet). See
     * `fields/varp.ini`.
     */
    int wholewrite_allowed;
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
    /** Heap-owned. Growable: the cache's `configs/all.enum` has enums with
     *  more than a thousand values (max measured 1658), and a fixed 256-slot
     *  array was the reason ServerScript could not read those tables. */
    struct Mock230EnumValue* values;
    int count;
    int capacity;
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

struct Mock230LocDef
{
    int loc_id;
    const char* symbol;
    /**
     * A `category` id, resolved through `pack/category.pack`, or -1.
     *
     * This was a private two-valued door enum (`MOCK230_LOC_CATEGORY_DOOR_CLOSED`
     * / `_OPENED`) until 2026-08-02, and the enum was wrong in the way that is
     * worst: it *aliased* onto the real namespace. Its values were 1 and 2, which
     * are `weapon_staff` and — nothing yet, but the next crawl could name it — so
     * handing it to `SSVM_ProviderGetByTrigger` as a category would have bound
     * every door in the game to a weapon's scripts. `interaction_category` in
     * `mock230_world.c` answered -1 for locs rather than risk it, and that -1 was
     * the whole reason `[oploc1,_door_closed]` could not bind.
     *
     * It is one id space now: the cache's own opcode 61 for the 8,407 records
     * that state one, and ids from `content.ini`'s `server_base` for concepts the
     * cache does not group (`door_closed` is 8192). Read it through
     * `mock230_loc_category`, which applies the overlay-then-cache order; this
     * field is only the overlay's half.
     */
    int category;
    /** `param=next_loc_stage,<symbol>`: what this loc becomes when used. -1
     *  when it has none, which makes the op a no-op rather than a crash. */
    int next_loc_stage;
};

/*
 * A `.loc` block's `op1=`..`op5=` are deliberately NOT a field here.
 *
 * They go straight to `mock230_scene_loc_op_overlay` as they are parsed, because
 * the only reader is `mock230_scene_loc_op` and `mock230_scene.c` is the half of
 * the server that knows nothing about a content tree — `collision_doors_test`
 * links it against the cache alone. Storing them here and having the scene pull
 * them back out would reverse that, for no gain: nothing else asks a loc def
 * what its ops are.
 */

const struct Mock230LocDef*
mock230_content_loc(int loc_id);

/** The authored loc defs by position, NULL past the end. For the callers that
 *  have to walk every one of them — `mock230_loc_category_members` counts the
 *  overlay's rows the way it counts the cache's. */
const struct Mock230LocDef*
mock230_content_loc_at(int index);

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

/* ------------------------------------------------------------------ */
/* The server band (server/pack)                                       */
/* ------------------------------------------------------------------ */

/** What `mock230_content_load_server_band` found, for the boot log. */
struct Mock230BandReport
{
    /** Band archives that opened and decoded, across every registered type. */
    int archives;
    /** Of those, how many overlay a def the text pass loaded. */
    int overlaid;
    /** Archives refusing to open — bad magic, version, kind or CRC. Any one of
     *  these means the pack on disk is stale or truncated. */
    int invalid;
    /** Field values where the band and the text parse disagree. Any one of
     *  these means the *tree* moved since the pack was written. */
    int mismatched;
    /** Field values only the text carries because the band has no wire for
     *  them (`huntmode=aggressive` is an enum name and the band is integers).
     *  Expected during migration; reported per field, never fatal. */
    int text_only;
    /** Band archives over records the runtime never loads: no def, and the
     *  seed is blind to the cache record (`mock230_npcinfo_known` — the
     *  nameless multinpc instances). Nothing exists to compare them against
     *  and nothing is applied from them. */
    int unseeded;
};

/** `mock230_content_load_server_band` results. */
enum Mock230BandStatus
{
    /** Verified identical to the text parse and applied over it. */
    MOCK230_BAND_LOADED = 1,
    /** No `server/pack` on disk — run `cachepack pack` (make mock230-servpack). */
    MOCK230_BAND_MISSING = 0,
    /** Present but stale: a CRC/header refusal or a value disagreeing with the
     *  text parse. Nothing was applied; the text-loaded records stand. */
    MOCK230_BAND_STALE = -1,
};

/**
 * The band load path: read `<dir>/server/pack`, prove it equivalent, prefer it.
 *
 * **Call after `mock230_content_load`** — the proof is against what that pass
 * loaded, and the seeds come from the cache tables plus the text `[default]`
 * block it applied.
 *
 * Every band archive is held to the text parse three ways, per registered
 * field: a band value must equal the text-loaded value; a field the band lacks
 * must be one the text left at its seed **or** one the band has no wire for
 * (counted per field as `text_only`); and a record with no text def must decode
 * to exactly its seed. Only when every archive passes is the band decoded over
 * the live records — at which point the band, not the text, is what the engine
 * is running on. Any refusal leaves the text parse standing, which is the
 * migration fallback PORTING_GUIDE §3.6 describes.
 */
enum Mock230BandStatus
mock230_content_load_server_band(
    const char* dir,
    struct Mock230BandReport* report);

void
mock230_content_free(void);

/**
 * Is this tile in a multi-combat zone? — `maps/multiway.csv`, the reference's
 * `GameMap.isMulti`.
 *
 * Answers 0 for every tile when the file is absent, which is the same answer the
 * reference gives (`if (fs.existsSync(...))` — no file, empty set). A tree
 * without the data therefore behaves as single-way everywhere rather than
 * failing to boot, and that is the safe direction: single-way is the more
 * restrictive rule.
 */
int
mock230_content_multiway(
    int x,
    int z,
    int level);

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
