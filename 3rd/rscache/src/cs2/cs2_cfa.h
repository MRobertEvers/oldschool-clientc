/*
 * Control-flow reconstruction: labels and gotos back into if/while/switch.
 *
 * Ported from RuneStar/cs2 `cfa/`. The method is the standard one: partition
 * the instruction list into basic blocks, build the flow graph, compute a
 * dominator tree (Cooper-Harvey-Kennedy), then walk the graph emitting a
 * structured construct wherever a block dominates the region it branches into.
 *
 * There is no goto in the CS2 language, so this is not a convenience — a
 * decompile that cannot be structured cannot be printed at all.
 */
#ifndef RSCACHE_CS2_CFA_H
#define RSCACHE_CS2_CFA_H

#include "cs2_ir.h"

#include <stdbool.h>

enum RSCache_CS2_ConstructKind
{
    RSCACHE_CS2_CONSTRUCT_SEQ = 0,
    RSCACHE_CS2_CONSTRUCT_IF,
    RSCACHE_CS2_CONSTRUCT_WHILE,
    RSCACHE_CS2_CONSTRUCT_SWITCH,
};

struct RSCache_CS2_Construct
{
    enum RSCache_CS2_ConstructKind kind;
    /** What follows this construct at the same nesting level. */
    struct RSCache_CS2_Construct* next;

    /* SEQ: Insn*, in order. */
    struct RSCache_CS2_Vec instructions;

    /* IF: parallel arrays — condition i guards body i. The first is the `if`,
     * the rest are `else if`. */
    struct RSCache_CS2_Vec branch_conditions;
    struct RSCache_CS2_Vec branch_bodies;
    struct RSCache_CS2_Construct* otherwise;

    /* WHILE */
    struct RSCache_CS2_Expr* condition;
    struct RSCache_CS2_Construct* body;

    /* SWITCH */
    struct RSCache_CS2_Expr* expression;
    /** Per case group: an RSCache_CS2_IntVec* of the keys that share a body. */
    struct RSCache_CS2_Vec case_keys;
    /** Per case group: an RSCache_CS2_Construct*. */
    struct RSCache_CS2_Vec case_bodies;
    struct RSCache_CS2_Construct* default_case;
};

/**
 * Reconstruct one function's control flow.
 *
 * Returns the root construct, or NULL with `error` filled in when the flow
 * graph is not reducible to the language's constructs. Everything allocated
 * belongs to `arena`.
 */
struct RSCache_CS2_Construct*
RSCache_CS2_Reconstruct(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Function* function,
    char* error,
    int error_capacity);

#endif
