#ifndef SRC_SERVERSCRIPT_SSVM_SCRIPT_H
#define SRC_SERVERSCRIPT_SSVM_SCRIPT_H

/*
 * One compiled ServerScript, and the decoder for LostCity's bytecode format.
 *
 * The format is close to, but NOT the same as, the client's CS2 bytecode that
 * 3rd/rscache/src/datatypes/clientscript.c reads — see ss_is_large_operand
 * below for the one-byte-off difference that makes sharing code a trap.
 *
 * Layout, big-endian throughout:
 *
 *   HEADER (offset 0)
 *     cstr   name          "[opnpc1,hans]" — brackets included
 *     cstr   source_path   absolute path from the machine that compiled it
 *     i32    lookup_key    trigger | kind << 8 | subject << 10, or -1
 *     u8     param_count   non-zero only for debugproc
 *     u8[]   param_types   ScriptVarType chars
 *     u16    line_count
 *     {i32 pc, i32 line} * line_count
 *
 *   CODE (until pos == trailer_pos)
 *     u16 opcode, then one operand:
 *       opcode == PUSH_CONSTANT_STRING -> cstr
 *       ss_is_large_operand(opcode)    -> i32
 *       otherwise                      -> u8
 *
 *   TRAILER (at len - trailer_len - SS_SCRIPT_FOOTER_BYTES)
 *     i32 instruction_count
 *     u16 int_local_count, string_local_count, int_arg_count, string_arg_count
 *     u8  switch_table_count
 *     {u16 case_count; {i32 key, i32 delta} * case_count} * switch_table_count
 *     u16 trailer_len       (the last two bytes of the file)
 *
 * Sizes are not stored per-script anywhere inside the body — the container's
 * .idx supplies them — so a decode that stops in the wrong place cannot be
 * detected later. That is why the decoder asserts it lands EXACTLY on
 * trailer_pos and that it decoded exactly instruction_count opcodes.
 */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

enum
{
    /* Fixed trailer bytes after the switch block: instruction_count (4) +
     * four u16 counts (8) + switch_table_count (1) + trailer_len (2) - the
     * switch_table_count byte is inside trailer_len's own accounting, so this
     * is the constant the reference spells as `- trailerLen - 12 - 2`. */
    SS_SCRIPT_FOOTER_BYTES = 14,

    /* A script shorter than this cannot hold a trailer. */
    SS_SCRIPT_MIN_BYTES = 16,

    /* Caps sit far above the real corpus (measured maxima: 10 tables, 86 cases,
     * 3 param types) so no genuine script trips them, but a corrupt length
     * field hits a bounds error instead of a huge allocation. */
    SS_MAX_SWITCH_TABLES = 32,
    SS_MAX_SWITCH_CASES = 4096,
    SS_MAX_PARAM_TYPES = 16,

    /* Sanity bound on instruction_count, checked before allocating. The corpus
     * maximum is 1,515. */
    SS_MAX_INSTRUCTIONS = 65536,
};

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

struct SSVM_Error
{
    /** Script the failure belongs to, or -1 when it is container-level. */
    int script_id;
    /** Byte offset within that script's body, or -1. */
    long offset;
    char message[192];
};

void
SSVM_ErrorClear(struct SSVM_Error* err);

/** Formats into `err` when it is non-NULL; always returns 0 so callers can
 *  write `return SSVM_ErrorSet(...)`. */
int
SSVM_ErrorSet(
    struct SSVM_Error* err,
    int script_id,
    long offset,
    const char* fmt,
    ...);

/* ------------------------------------------------------------------ */
/* Script                                                              */
/* ------------------------------------------------------------------ */

struct SSVM_SwitchCase
{
    int32_t key;
    /** Instruction-index delta, NOT a byte offset. The VM does pc += delta and
     *  then ++pc at the top of the loop, so the target is pc + delta + 1. */
    int32_t delta;
};

struct SSVM_SwitchTable
{
    struct SSVM_SwitchCase* cases;
    uint16_t case_count;
};

struct SSVM_Script
{
    int32_t id;

    /** "[opnpc1,hans]", brackets included. Owned. */
    char* name;
    /** Absolute path on whichever machine compiled this. Owned. Never open it —
     *  it exists so a backtrace can name a file, nothing more. */
    char* source_path;

    /** trigger | kind << 8 | subject << 10, or -1 for a name-addressed script
     *  (proc, label, queue, timer, softtimer, walktrigger, debugproc). Signed:
     *  the -1 must survive as -1. */
    int32_t lookup_key;

    /** ScriptVarType chars. Read as unsigned: NPC_STAT is 254 and AUTOINT is
     *  255, both of which go negative in a signed char. */
    uint8_t param_types[SS_MAX_PARAM_TYPES];
    uint8_t param_type_count;

    uint16_t int_local_count;
    uint16_t string_local_count;
    uint16_t int_arg_count;
    uint16_t string_arg_count;

    int32_t op_count;
    /** [op_count] each. `string_operands[i]` is non-NULL only where
     *  `opcodes[i] == SS_OP_PUSH_CONSTANT_STRING`. */
    uint16_t* opcodes;
    int32_t* int_operands;
    char** string_operands;

    struct SSVM_SwitchTable* switch_tables;
    uint8_t switch_table_count;

    /** pc -> source line, for backtraces. */
    int32_t* line_pcs;
    int32_t* line_numbers;
    uint16_t line_count;
};

/**
 * Decode one script body.
 *
 * `out` is zeroed first, so a failed decode leaves it safe to pass to
 * SSVM_ScriptFree. Returns 1 on success, 0 with `err` filled on failure.
 */
int
SSVM_ScriptDecode(
    int32_t id,
    const uint8_t* data,
    size_t length,
    struct SSVM_Script* out,
    struct SSVM_Error* err);

/**
 * Re-encode a script to the wire format.
 *
 * Exists so the reader can be checked the only way that admits no doubt: decode
 * the real corpus, encode it back, and memcmp. A decoder that skipped a field,
 * mis-sized one, or quietly normalised a value cannot survive that.
 *
 * Writes into `out` (capacity `capacity`) and reports the byte count through
 * `out_length`. Returns 1 on success; 0 with `err` filled when the buffer is
 * too small or a field cannot be represented. Call with `out == NULL` to size
 * the buffer first — `out_length` is still filled.
 */
int
SSVM_ScriptEncode(
    const struct SSVM_Script* script,
    uint8_t* out,
    size_t capacity,
    size_t* out_length,
    struct SSVM_Error* err);

void
SSVM_ScriptFree(struct SSVM_Script* script);

/** Source line for a pc, or 0 when there is no line table. */
int32_t
SSVM_ScriptLineNumber(
    const struct SSVM_Script* script,
    int32_t pc);

/** Trailing component of `source_path`, for backtraces. Never NULL. */
const char*
SSVM_ScriptFileName(const struct SSVM_Script* script);

/** Trigger byte of `lookup_key`, or -1 when the script is name-addressed. */
int
SSVM_ScriptTrigger(const struct SSVM_Script* script);

/**
 * True when `opcode` carries a 4-byte operand.
 *
 * NOT the CS2 rule. ServerScript is `opcode <= 100` minus {21,22,23,38,39};
 * CS2 (3rd/rscache/src/datatypes/clientscript.c) is `opcode >= 100` plus
 * {21,38,39,62,63}. The boundary differs by one AND the exception sets differ,
 * so the two predicates must never be shared or "helpfully" merged later.
 */
static inline int
ss_is_large_operand(int opcode)
{
    if( opcode > 100 )
        return 0;
    switch( opcode )
    {
    case 21: /* RETURN */
    case 22: /* GOSUB */
    case 23: /* JUMP */
    case 38: /* POP_INT_DISCARD */
    case 39: /* POP_STRING_DISCARD */
        return 0;
    default:
        return 1;
    }
}

#endif
