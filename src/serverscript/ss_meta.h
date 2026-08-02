#ifndef SRC_SERVERSCRIPT_SS_META_H
#define SRC_SERVERSCRIPT_SS_META_H

/*
 * Per-opcode metadata and the trigger lookup key.
 *
 * The tables themselves are generated from the LostCity reference server by
 * gen_opcode_meta.py; this header is the hand-written interface to them. The
 * arity comes from content/scripts/engine.rs2, which is the authoritative
 * signature file, so the VM can pop exactly what a command declares instead of
 * every handler counting for itself. A wrong arity does not crash — it pops the
 * wrong number of values and corrupts the stack for every op after it — so
 * deriving it beats hand-typing it.
 */

#include "ss_opcode.h"
#include "ss_trigger.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Active-entity pointers                                              */
/* ------------------------------------------------------------------ */

/* Bit positions match ScriptPointer.ts. A command declaring `require:
 * ['active_npc']` cannot run unless the state has an active npc; the VM checks
 * this from the table before dispatching, which replaces the reference's
 * hand-maintained checkedHandler() wrapper at 239 call sites.
 *
 * `set` and `corrupt` are deliberately NOT table-driven: they are semantic
 * (npc_find sets active_npc only when it actually found one), so they stay
 * handler responsibilities. */
enum
{
    SSVM_PTR_ACTIVE_PLAYER = 1 << 0,
    SSVM_PTR_ACTIVE_PLAYER2 = 1 << 1,
    SSVM_PTR_PROTECTED_PLAYER = 1 << 2,
    SSVM_PTR_PROTECTED_PLAYER2 = 1 << 3,
    SSVM_PTR_ACTIVE_NPC = 1 << 4,
    SSVM_PTR_ACTIVE_NPC2 = 1 << 5,
    SSVM_PTR_ACTIVE_LOC = 1 << 6,
    SSVM_PTR_ACTIVE_LOC2 = 1 << 7,
    SSVM_PTR_ACTIVE_OBJ = 1 << 8,
    SSVM_PTR_ACTIVE_OBJ2 = 1 << 9,
};

/* ------------------------------------------------------------------ */
/* Opcode metadata                                                     */
/* ------------------------------------------------------------------ */

struct SSVM_OpcodeMeta
{
    /** Values popped. All ScriptVarTypes except `string` live on the int
     *  stack, so two counts describe every signature. */
    uint8_t int_in;
    uint8_t str_in;
    /** Values pushed. */
    uint8_t int_out;
    uint8_t str_out;

    /** The signature is real (declared in engine.rs2, or hand-filled from the
     *  reference's handler). 0 means the arity is genuinely unknown and the
     *  opcode must not be executed — see SSVM_OpcodeMetaKnown. */
    uint8_t known;

    /** The counts above are a lower bound only: the command pops a type string
     *  and decides from its characters what else to pop. The four queue*
     *  variants and the two structural ops (JOIN_STRING, GOSUB_WITH_PARAMS)
     *  are the whole set. Callers must not use int_in/str_in for these. */
    uint8_t variadic;

    /** Returns `any`: the data decides what comes back. enum, db_getfield and
     *  the *_param family all read a type out of the cache — a param declared
     *  as a string pushes onto the string stack where an int-typed one pushes
     *  an int, and db_getfield pushes one value per column, so even the count
     *  varies. int_out/str_out describe only the common int case; the handler
     *  is what actually decides. Static analysis cannot model these at all. */
    uint8_t runtime_typed;

    /** A `.name` secondary-pointer variant exists, so this opcode's operand is
     *  the dot flag. */
    uint8_t has_dot;

    /** Pointer bits required before dispatch, for the primary and secondary
     *  (dot) forms respectively. */
    uint16_t require;
    uint16_t require2;
};

/** Never NULL. Unknown ids report "OP_<n>" in a static per-call buffer. */
const char*
SSVM_OpcodeName(int opcode);

/** Never NULL: out-of-range ids return an all-zero entry with known = 0. */
const struct SSVM_OpcodeMeta*
SSVM_OpcodeMeta(int opcode);

/** Convenience for the hot path in the interpreter loop. */
int
SSVM_OpcodeMetaKnown(int opcode);

/** Opcode id for an engine.rs2 command name ("mes", "inv_add"), or -1.
 *  Case-insensitive, and a leading '.' selecting the secondary-pointer form is
 *  ignored — the dot lives in the operand, not in the opcode. */
int
SSVM_OpcodeFromName(const char* name);

/* ------------------------------------------------------------------ */
/* Triggers                                                            */
/* ------------------------------------------------------------------ */

/** Lower-case, matching the name inside a script's `[trigger,subject]` header.
 *  Never NULL; unknown ids return "". */
const char*
SSVM_TriggerName(int trigger);

/** SS_TRIGGER_* for a lower-case name, or -1. */
int
SSVM_TriggerFromName(const char* name);

/** How a script's subject is addressed. Matches the compiler's SubjectMode. */
enum
{
    SS_LOOKUP_GLOBAL = 0,   /**< `[login,_]` — no subject. */
    SS_LOOKUP_CATEGORY = 1, /**< subject is a category id. */
    SS_LOOKUP_TYPE = 2,     /**< subject is a type id (npc, obj, component). */
};

/**
 * Pack a trigger lookup key.
 *
 * The key is 64-bit where the reference's is a 32-bit int. That is a deliberate
 * divergence, but a narrower one than it looks:
 *
 * The compiled `lookup_key` field is an i32 and the subject sits at bit 10, so
 * a *stored* key can only ever describe a subject below 2^21. Widening here
 * therefore recovers nothing the format lost. What it buys is that a *lookup*
 * computed from an oversized subject finds no entry instead of wrapping onto
 * some unrelated script's key. That case is real: rev-230 addresses an
 * interface component as a packed (interface << 16) | child uid, so 231:5 is
 * 15,138,821 and cannot be a subject at all. Finding nothing is debuggable;
 * opening the wrong dialogue is not. Content targeting a component must use a
 * flat component id that fits, exactly as the reference's own does.
 *
 * Note the reference's mapzone/zone collisions are NOT this bug — its compiler
 * derives those subjects with parseInt("0_29_75"), which stops at the
 * underscore and yields 0, so every *three-part* one (379 of them) lands on the
 * same key. Nothing a key width can fix. Its five-part `[zone,…]` subjects
 * (427) are a second, different collapse: they pack into 28 bits and wrap when
 * shifted to bit 10 — 78 onto a negative key the index drops, and 349 onto a
 * subject field that is not the coord (10 of those colliding).
 * Neither shape is addressable by key, which is why this compiler writes -1 for
 * any coord subject and the engine reaches all four by *name* — see
 * ssc_compile.c's `subject_is_coord` branch and osrs230_mockserver.md §3.21.
 * test-ss-corpus reports the reference pack's own count.
 */
static inline uint64_t
SSVM_LookupKey(
    int trigger,
    int kind,
    int32_t subject)
{
    return (uint64_t)(uint32_t)trigger | ((uint64_t)(uint32_t)kind << 8) |
           ((uint64_t)(uint32_t)subject << 10);
}

/** Unpack the trigger byte from a key read out of a compiled script. */
static inline int
SSVM_LookupKeyTrigger(int32_t lookup_key)
{
    return lookup_key & 0xff;
}

#endif
