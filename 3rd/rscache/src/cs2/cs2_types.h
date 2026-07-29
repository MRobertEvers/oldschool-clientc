/*
 * The CS2 type system: script types, stack types, triggers, script names and
 * prototypes.
 *
 * A *type* is what the language writes (`int`, `component`, `namedobj`); a
 * *stack type* is what the VM actually pushes (int or string, nothing else). A
 * *prototype* pairs a type with an identifier hint — `int-mousex` and
 * `int-width` are both ints on the stack but decompile to differently named
 * locals, which is the whole reason the reference carries the pair around
 * rather than the type alone.
 *
 * Ported from RuneStar/cs2 `bin/Type.kt`, `bin/StackType.kt`, `bin/Trigger.kt`,
 * `bin/ScriptName.kt` and `ir/Prototype.kt`.
 */
#ifndef RSCACHE_CS2_TYPES_H
#define RSCACHE_CS2_TYPES_H

#include "cs2_support.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Stack types
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_StackType
{
    RSCACHE_CS2_STACK_INT = 0,
    RSCACHE_CS2_STACK_STRING = 1,
};

/* -------------------------------------------------------------------------
 * Types
 *
 * The ordering matches Type.kt so `RSCACHE_CS2_TYPE_COUNT` can size tables.
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_Type
{
    RSCACHE_CS2_TYPE_AREA = 0,
    RSCACHE_CS2_TYPE_BOOLEAN,
    RSCACHE_CS2_TYPE_CATEGORY,
    RSCACHE_CS2_TYPE_CHAR,
    RSCACHE_CS2_TYPE_COMPONENT,
    RSCACHE_CS2_TYPE_COORD,
    RSCACHE_CS2_TYPE_ENUM,
    RSCACHE_CS2_TYPE_FONTMETRICS,
    RSCACHE_CS2_TYPE_GRAPHIC,
    RSCACHE_CS2_TYPE_INT,
    RSCACHE_CS2_TYPE_INTERFACE,
    RSCACHE_CS2_TYPE_INV,
    RSCACHE_CS2_TYPE_LOC,
    RSCACHE_CS2_TYPE_MAPAREA,
    RSCACHE_CS2_TYPE_MAPELEMENT,
    RSCACHE_CS2_TYPE_MODEL,
    RSCACHE_CS2_TYPE_NAMEDOBJ,
    RSCACHE_CS2_TYPE_NPC,
    RSCACHE_CS2_TYPE_OBJ,
    RSCACHE_CS2_TYPE_PARAM,
    RSCACHE_CS2_TYPE_SEQ,
    RSCACHE_CS2_TYPE_SPOTANIM,
    RSCACHE_CS2_TYPE_STAT,
    RSCACHE_CS2_TYPE_STRING,
    RSCACHE_CS2_TYPE_STRUCT,
    RSCACHE_CS2_TYPE_SYNTH,
    RSCACHE_CS2_TYPE_NEWVAR,
    RSCACHE_CS2_TYPE_NPC_UID,
    RSCACHE_CS2_TYPE_PLAYER_UID,
    RSCACHE_CS2_TYPE_TYPE,
    /* Added after RuneStar's Type.kt was written: the client-side database
     * arrived with the DB_* opcodes. Descriptor 0xD0, per the ScriptVarType
     * table this repo already carries in src/serverscript/ssc_symbols.c. */
    RSCACHE_CS2_TYPE_DBROW,

    RSCACHE_CS2_TYPE_COUNT,
    RSCACHE_CS2_TYPE_NONE = -1,
};

/** Lowercase source spelling, e.g. "namedobj". Never NULL for a valid type. */
const char*
RSCache_CS2_TypeLiteral(enum RSCache_CS2_Type type);

/**
 * The wire descriptor byte, as it appears in a clientscript's argument
 * descriptor string and in enum/param config records. 0 for PARAM and TYPE,
 * which have no descriptor.
 */
uint8_t
RSCache_CS2_TypeDesc(enum RSCache_CS2_Type type);

/** Descriptor byte -> type. RSCACHE_CS2_TYPE_NONE when unrecognised. */
enum RSCache_CS2_Type
RSCache_CS2_TypeOfDesc(uint8_t desc);

/** As RSCache_CS2_TypeOfDesc, mapping descriptor 0 to `int` (config default). */
enum RSCache_CS2_Type
RSCache_CS2_TypeOfDescAuto(uint8_t desc);

/** Type name (case-insensitive literal) -> type, for the compiler's parser. */
enum RSCache_CS2_Type
RSCache_CS2_TypeOfLiteral(const char* literal);

enum RSCache_CS2_StackType
RSCache_CS2_TypeStackType(enum RSCache_CS2_Type type);

/**
 * Merge rules for the type solver.
 *
 * A value flowing *into* a typing widens (union): obj and namedobj meet at obj,
 * fontmetrics and graphic at fontmetrics. A value flowing *out* narrows
 * (intersection) the other way. Any other pair of distinct types is a
 * contradiction and returns RSCACHE_CS2_TYPE_NONE — the reference throws there,
 * and a decompile that reaches it is genuinely inconsistent.
 */
enum RSCache_CS2_Type
RSCache_CS2_TypeUnion(const enum RSCache_CS2_Type* types, int count);

enum RSCache_CS2_Type
RSCache_CS2_TypeIntersection(const enum RSCache_CS2_Type* types, int count);

/* -------------------------------------------------------------------------
 * Triggers
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_Trigger
{
    RSCACHE_CS2_TRIGGER_OPWORLDMAPELEMENT1 = 10,
    RSCACHE_CS2_TRIGGER_OPWORLDMAPELEMENT2 = 11,
    RSCACHE_CS2_TRIGGER_OPWORLDMAPELEMENT3 = 12,
    RSCACHE_CS2_TRIGGER_OPWORLDMAPELEMENT4 = 13,
    RSCACHE_CS2_TRIGGER_OPWORLDMAPELEMENT5 = 14,
    RSCACHE_CS2_TRIGGER_WORLDMAPELEMENTMOUSEOVER = 15,
    RSCACHE_CS2_TRIGGER_WORLDMAPELEMENTMOUSELEAVE = 16,
    RSCACHE_CS2_TRIGGER_WORLDMAPELEMENTMOUSEREPEAT = 17,
    RSCACHE_CS2_TRIGGER_47 = 47,
    RSCACHE_CS2_TRIGGER_48 = 48,
    RSCACHE_CS2_TRIGGER_49 = 49,
    RSCACHE_CS2_TRIGGER_PROC = 73,
    RSCACHE_CS2_TRIGGER_CLIENTSCRIPT = 76,

    RSCACHE_CS2_TRIGGER_NONE = -1,
};

const char*
RSCache_CS2_TriggerName(enum RSCache_CS2_Trigger trigger);

enum RSCache_CS2_Trigger
RSCache_CS2_TriggerOfName(const char* name);

bool
RSCache_CS2_TriggerIsValid(int id);

/* -------------------------------------------------------------------------
 * Script names
 *
 * A cache's script-name table holds either a bracketed name
 * (`[proc,min]`) or a bare integer that encodes a trigger and a subject. The
 * integer forms are the three the client itself synthesises, and the arithmetic
 * below is transcribed from ScriptName.kt rather than re-derived.
 * ---------------------------------------------------------------------- */

struct RSCache_CS2_ScriptName
{
    enum RSCache_CS2_Trigger trigger;
    /* Owned by the caller-supplied arena. */
    const char* name;
};

/**
 * Parse a cache name string. Returns false when the string is neither a valid
 * bracketed name nor an integer that resolves to a known trigger.
 */
bool
RSCache_CS2_ScriptNameParse(
    const char* cache_name,
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_ScriptName* out);

/** Formats as `[trigger,name]` into the arena. */
const char*
RSCache_CS2_ScriptNameFormat(
    const struct RSCache_CS2_ScriptName* script_name,
    struct RSCache_CS2_Arena* arena);

/* -------------------------------------------------------------------------
 * Prototypes
 *
 * A closed set, so a prototype is an index rather than an interned pair. The
 * generated command table and the identifier solver both index by this enum.
 * ---------------------------------------------------------------------- */

enum RSCache_CS2_ProtoId
{
    /* Plain prototypes: identifier == the type's own literal. */
    RSCACHE_CS2_PROTO_INT = 0,
    RSCACHE_CS2_PROTO_STRING,
    RSCACHE_CS2_PROTO_COMPONENT,
    RSCACHE_CS2_PROTO_BOOLEAN,
    RSCACHE_CS2_PROTO_OBJ,
    RSCACHE_CS2_PROTO_ENUM,
    RSCACHE_CS2_PROTO_STAT,
    RSCACHE_CS2_PROTO_GRAPHIC,
    RSCACHE_CS2_PROTO_INV,
    RSCACHE_CS2_PROTO_MODEL,
    RSCACHE_CS2_PROTO_COORD,
    RSCACHE_CS2_PROTO_CATEGORY,
    RSCACHE_CS2_PROTO_LOC,
    RSCACHE_CS2_PROTO_AREA,
    RSCACHE_CS2_PROTO_MAPAREA,
    RSCACHE_CS2_PROTO_NAMEDOBJ,
    RSCACHE_CS2_PROTO_FONTMETRICS,
    RSCACHE_CS2_PROTO_CHAR,
    RSCACHE_CS2_PROTO_STRUCT,
    RSCACHE_CS2_PROTO_SYNTH,
    RSCACHE_CS2_PROTO_MAPELEMENT,
    RSCACHE_CS2_PROTO_NPC,
    RSCACHE_CS2_PROTO_SEQ,
    RSCACHE_CS2_PROTO_INTERFACE,
    RSCACHE_CS2_PROTO_TYPE,
    RSCACHE_CS2_PROTO_PARAM,
    RSCACHE_CS2_PROTO_NEWVAR,
    RSCACHE_CS2_PROTO_NPC_UID,
    RSCACHE_CS2_PROTO_PLAYER_UID,
    RSCACHE_CS2_PROTO_SPOTANIM,
    RSCACHE_CS2_PROTO_DBROW,

    /* Named prototypes: a type plus an identifier hint. */
    RSCACHE_CS2_PROTO_OPBASE,
    RSCACHE_CS2_PROTO_MOUSEX,
    RSCACHE_CS2_PROTO_MOUSEY,
    RSCACHE_CS2_PROTO_OPINDEX,
    RSCACHE_CS2_PROTO_COMSUBID,
    RSCACHE_CS2_PROTO_DROP,
    RSCACHE_CS2_PROTO_DROPSUBID,
    RSCACHE_CS2_PROTO_KEY,
    RSCACHE_CS2_PROTO_KEYCHAR,
    RSCACHE_CS2_PROTO_INDEX,
    RSCACHE_CS2_PROTO_LENGTH,
    RSCACHE_CS2_PROTO_COUNT,
    RSCACHE_CS2_PROTO_TOTAL,
    RSCACHE_CS2_PROTO_SIZE,
    RSCACHE_CS2_PROTO_NUM,
    RSCACHE_CS2_PROTO_X,
    RSCACHE_CS2_PROTO_Y,
    RSCACHE_CS2_PROTO_Z,
    RSCACHE_CS2_PROTO_WIDTH,
    RSCACHE_CS2_PROTO_HEIGHT,
    RSCACHE_CS2_PROTO_WORLD,
    RSCACHE_CS2_PROTO_RANK,
    RSCACHE_CS2_PROTO_XP,
    RSCACHE_CS2_PROTO_LVL,
    RSCACHE_CS2_PROTO_SLOT,
    RSCACHE_CS2_PROTO_CLANSLOT,
    RSCACHE_CS2_PROTO_CLOCK,
    RSCACHE_CS2_PROTO_TRANS,
    RSCACHE_CS2_PROTO_ANGLE,
    RSCACHE_CS2_PROTO_CHATFILTER,
    RSCACHE_CS2_PROTO_MESUID,
    RSCACHE_CS2_PROTO_FLAGS,
    RSCACHE_CS2_PROTO_LAYER,
    RSCACHE_CS2_PROTO_OP,
    RSCACHE_CS2_PROTO_URL,
    RSCACHE_CS2_PROTO_TEXT,
    RSCACHE_CS2_PROTO_MES,
    RSCACHE_CS2_PROTO_USERNAME,
    RSCACHE_CS2_PROTO_COLOUR,
    RSCACHE_CS2_PROTO_IFTYPE,
    RSCACHE_CS2_PROTO_SETSIZE,
    RSCACHE_CS2_PROTO_SETPOSH,
    RSCACHE_CS2_PROTO_SETPOSV,
    RSCACHE_CS2_PROTO_SETTEXTALIGNH,
    RSCACHE_CS2_PROTO_SETTEXTALIGNV,
    RSCACHE_CS2_PROTO_CHATTYPE,
    RSCACHE_CS2_PROTO_BOOL,
    RSCACHE_CS2_PROTO_WINDOWMODE,
    RSCACHE_CS2_PROTO_CLIENTTYPE,

    /**
     * An int-stack argument whose *type* is not known.
     *
     * The client's stack table records how many ints and strings an opcode moves
     * and nothing about what they mean, so a signature taken from it can assert
     * arity and no more. Writing `int` there is a claim the table cannot support,
     * and a false one often enough to matter: 996 of osrs239's decompile failures
     * were a value the signature called `int` meeting a use that called it
     * `component`, which the type lattice — correctly — refuses to reconcile.
     *
     * Its type is NONE, which the solver reads as "contributes no constraint", so
     * the value takes whatever type its other uses give it. A value with no other
     * use falls out as `int` anyway, at the solver's last step.
     */
    RSCACHE_CS2_PROTO_UNKNOWNINT,

    RSCACHE_CS2_PROTO_COUNT_,
    RSCACHE_CS2_PROTO_NONE = -1,
};

struct RSCache_CS2_Prototype
{
    enum RSCache_CS2_Type type;
    const char* identifier;
};

const struct RSCache_CS2_Prototype*
RSCache_CS2_ProtoGet(enum RSCache_CS2_ProtoId id);

/** The plain prototype for a type — identifier equal to the type literal. */
enum RSCache_CS2_ProtoId
RSCache_CS2_ProtoForType(enum RSCache_CS2_Type type);

/** True when the prototype adds no identifier beyond its type's own literal. */
bool
RSCache_CS2_ProtoIsDefault(enum RSCache_CS2_ProtoId id);

#endif
