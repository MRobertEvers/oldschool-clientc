/*
 * The command table: what each opcode is called, and what it does to the stack.
 *
 * Ported from RuneStar/cs2 `bin/Opcodes.kt` and `ir/Command.kt`. The bulk is
 * generated (cs2_command.gen.h) so it can be re-derived from the vendored
 * sources rather than drifting by hand; this header is the interface over it.
 *
 * `kind` selects a translate() body in the interpreter. Most opcodes are
 * RSCACHE_CS2_CMD_BASIC — pop N typed arguments, push M typed results — and the
 * rest are the handful with their own shape: switch tables, branches, procedure
 * calls, array access, string joins and the hook-registering `*_seton*` family.
 */
#ifndef RSCACHE_CS2_COMMAND_H
#define RSCACHE_CS2_COMMAND_H

#include "cs2_types.h"

#include <stdbool.h>

/* Named opcodes with no id of their own. The reference gives the two
 * short-circuit operators negative ids because they are synthesised by a DFA
 * pass rather than decoded, and never appear in a script's bytecode. */
#define RSCACHE_CS2_OP_SS_AND (-2)
#define RSCACHE_CS2_OP_SS_OR (-1)

#define RSCACHE_CS2_OP_PUSH_CONSTANT_INT 0
#define RSCACHE_CS2_OP_PUSH_VAR 1
#define RSCACHE_CS2_OP_POP_VAR 2
#define RSCACHE_CS2_OP_PUSH_CONSTANT_STRING 3
#define RSCACHE_CS2_OP_BRANCH 6
#define RSCACHE_CS2_OP_BRANCH_NOT 7
#define RSCACHE_CS2_OP_BRANCH_EQUALS 8
#define RSCACHE_CS2_OP_BRANCH_LESS_THAN 9
#define RSCACHE_CS2_OP_BRANCH_GREATER_THAN 10
#define RSCACHE_CS2_OP_RETURN 21
#define RSCACHE_CS2_OP_PUSH_VARBIT 25
#define RSCACHE_CS2_OP_POP_VARBIT 27
#define RSCACHE_CS2_OP_BRANCH_LESS_THAN_OR_EQUALS 31
#define RSCACHE_CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS 32
#define RSCACHE_CS2_OP_PUSH_INT_LOCAL 33
#define RSCACHE_CS2_OP_POP_INT_LOCAL 34
#define RSCACHE_CS2_OP_PUSH_STRING_LOCAL 35
#define RSCACHE_CS2_OP_POP_STRING_LOCAL 36
#define RSCACHE_CS2_OP_JOIN_STRING 37
#define RSCACHE_CS2_OP_POP_INT_DISCARD 38
#define RSCACHE_CS2_OP_POP_STRING_DISCARD 39
#define RSCACHE_CS2_OP_GOSUB_WITH_PARAMS 40
#define RSCACHE_CS2_OP_PUSH_VARC_INT 42
#define RSCACHE_CS2_OP_POP_VARC_INT 43
#define RSCACHE_CS2_OP_DEFINE_ARRAY 44
#define RSCACHE_CS2_OP_PUSH_ARRAY_INT 45
#define RSCACHE_CS2_OP_POP_ARRAY_INT 46
#define RSCACHE_CS2_OP_PUSH_VARC_STRING 49
#define RSCACHE_CS2_OP_POP_VARC_STRING 50
#define RSCACHE_CS2_OP_SWITCH 60
#define RSCACHE_CS2_OP_PUSH_VARCLANSETTING 74
#define RSCACHE_CS2_OP_PUSH_VARCLAN 76
#define RSCACHE_CS2_OP_ENUM 3408
/* The three hook families whose trigger list is typed rather than opaque. */
#define RSCACHE_CS2_OP_CC_SETONVARTRANSMIT 1407
#define RSCACHE_CS2_OP_CC_SETONINVTRANSMIT 1414
#define RSCACHE_CS2_OP_CC_SETONSTATTRANSMIT 1415
#define RSCACHE_CS2_OP_IF_SETONVARTRANSMIT 2407
#define RSCACHE_CS2_OP_IF_SETONINVTRANSMIT 2414
#define RSCACHE_CS2_OP_IF_SETONSTATTRANSMIT 2415
/** Hook opcodes at or above this id name their target component explicitly. */
#define RSCACHE_CS2_OP_IF_HOOK_BASE 2000
/*
 * The client-database family, whose stack shape is a property of the *data*.
 *
 * A `dbcolumn` literal packs (table, column, field+1); field 0 means "the whole
 * tuple". So `db_getfield` pushes one value of one type, or every field of the
 * column in order — and `db_find` pops a value whose int-vs-string stack is the
 * indexed field's. Neither is answerable from a fixed signature, which is why
 * these have their own kinds rather than an entry in the generated table.
 */
#define RSCACHE_CS2_OP_DB_FIND_WITH_COUNT 7500
#define RSCACHE_CS2_OP_DB_GETFIELD 7502
#define RSCACHE_CS2_OP_DB_FIND_FILTER_WITH_COUNT 7507
#define RSCACHE_CS2_OP_DB_FIND 7508
#define RSCACHE_CS2_OP_DB_FIND_FILTER 7510
#define RSCACHE_CS2_OP_ADD 4000
#define RSCACHE_CS2_OP_SUB 4001
#define RSCACHE_CS2_OP_MULTIPLY 4002
#define RSCACHE_CS2_OP_DIV 4003
#define RSCACHE_CS2_OP_MOD 4011
#define RSCACHE_CS2_OP_AND 4014
#define RSCACHE_CS2_OP_OR 4015

enum RSCache_CS2_CommandKind
{
    RSCACHE_CS2_CMD_UNKNOWN = 0,
    RSCACHE_CS2_CMD_SWITCH,
    RSCACHE_CS2_CMD_BRANCH,
    RSCACHE_CS2_CMD_PROC,
    RSCACHE_CS2_CMD_RETURN,
    RSCACHE_CS2_CMD_ENUM,
    RSCACHE_CS2_CMD_JOIN_STRING,
    RSCACHE_CS2_CMD_DEFINE_ARRAY,
    RSCACHE_CS2_CMD_PUSH_ARRAY_INT,
    RSCACHE_CS2_CMD_POP_ARRAY_INT,
    RSCACHE_CS2_CMD_BRANCH_COMPARE,
    RSCACHE_CS2_CMD_DISCARD,
    RSCACHE_CS2_CMD_ASSIGN,
    RSCACHE_CS2_CMD_BASIC,
    /** One argument's stack is selected by the final literal base-type code. */
    RSCACHE_CS2_CMD_TYPED_POP,
    /** Three fixed ints, descriptor-selected values, then the descriptor string. */
    RSCACHE_CS2_CMD_DESCRIPTOR_ARGS,
    RSCACHE_CS2_CMD_CLIENTSCRIPT,
    RSCACHE_CS2_CMD_PARAM,
    /** Active-component param lookup; result stack comes from the param config. */
    RSCACHE_CS2_CMD_ACTIVE_PARAM,
    /** Explicit (param, interface, subcomponent) lookup with dynamic result stack. */
    RSCACHE_CS2_CMD_COMPONENT_PARAM,
    /** db_getfield: pops (dbrow, dbcolumn, index), pushes the column's fields. */
    RSCACHE_CS2_CMD_DB_GETFIELD,
    /** db_find and friends: the search value's stack comes from the column. */
    RSCACHE_CS2_CMD_DB_FIND,
};

struct RSCache_CS2_CommandInfo
{
    /** Lowercase source spelling, or NULL when the opcode has no name. */
    const char* name;
    enum RSCache_CS2_CommandKind kind;
    /* Offsets into the shared prototype pool; see RSCache_CS2_CommandArg. */
    int arg_offset;
    int arg_count;
    int def_offset;
    int def_count;
    /**
     * True for the commands whose operand byte selects the "." form — the
     * active-component variant that takes its target implicitly rather than off
     * the stack. Only CC_* commands set it.
     */
    bool dot_capable;
    /**
     * Kind-specific: the discarded stack type for DISCARD, and the receiver
     * prototype for PARAM. Zero elsewhere.
     */
    int extra;
};

/** NULL when the opcode is outside the table or carries neither name nor signature. */
const struct RSCache_CS2_CommandInfo*
RSCache_CS2_CommandGet(int opcode);

/**
 * Install a signature for one opcode at run time, ahead of the generated table.
 *
 * The generated table holds what two published sources happen to know, and a
 * cache can contain opcodes neither describes. Rather than force a regeneration
 * to try an arity, a caller can supply one — which is what `cs2 infer-arity`
 * does while solving an unknown opcode against a corpus, and what a caller with
 * era-specific knowledge can do without touching the library.
 *
 * `args` and `defs` are copied. `arg_count < 0` removes any override for that
 * opcode. Returns false only when the override table is full.
 */
bool
RSCache_CS2_CommandOverride(
    int opcode,
    const char* name,
    const enum RSCache_CS2_ProtoId* args,
    int arg_count,
    const enum RSCache_CS2_ProtoId* defs,
    int def_count,
    bool dot_capable);

/** Drop every override installed so far. */
void
RSCache_CS2_CommandClearOverrides(void);

/**
 * Lowercase source spelling. NULL when unnamed — the caller decides whether
 * that is fatal (the compiler) or printable as a bare id (a raw disassembly).
 */
const char*
RSCache_CS2_CommandName(int opcode);

/**
 * Opcode for a source spelling, or -1. Semantic names are case-insensitive;
 * the `_1234` compatibility spelling is also accepted for an existing row.
 */
int
RSCache_CS2_CommandOfName(const char* name);

/** i-th argument prototype of a BASIC command. */
enum RSCache_CS2_ProtoId
RSCache_CS2_CommandArg(const struct RSCache_CS2_CommandInfo* info, int index);

/** i-th result prototype of a BASIC command. */
enum RSCache_CS2_ProtoId
RSCache_CS2_CommandDef(const struct RSCache_CS2_CommandInfo* info, int index);

/**
 * Infix spelling for the operators the generator prints as `a + b` rather than
 * `add(a, b)`. NULL when the opcode has no infix form.
 *
 * Two families, kept apart because they are legal in different places: `calc`
 * arithmetic, and the comparisons that may only appear in an `if`/`while` test.
 */
const char*
RSCache_CS2_CommandCalcInfix(int opcode);

const char*
RSCache_CS2_CommandBranchInfix(int opcode);

/**
 * Binding tightness, 1 (tightest) upward, for deciding parentheses. 0 when the
 * opcode is not an infix operator.
 */
int
RSCache_CS2_CommandPrecedence(int opcode);

#endif
