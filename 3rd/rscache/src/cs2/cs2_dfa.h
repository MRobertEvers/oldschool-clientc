/*
 * The data-flow passes that turn the interpreter's straight-line stack
 * assignments back into readable expressions.
 *
 * Ported from RuneStar/cs2 `dfa/`. The order in Phase.DEFAULT is load-bearing
 * and is preserved exactly: inlining before type inference, type inference
 * before identifier inference, short-circuit operators last (they rewrite the
 * branch structure the earlier passes assume).
 */
#ifndef RSCACHE_CS2_DFA_H
#define RSCACHE_CS2_DFA_H

#include "cs2_ir.h"

#include <stdbool.h>

/**
 * Run every pass over the function set.
 *
 * Returns false with `error` filled in when a pass finds the IR inconsistent —
 * a type contradiction, or a shape the reference asserts on. Those are real
 * signals that something upstream (usually a command signature) is wrong, not
 * conditions to paper over.
 */
bool
RSCache_CS2_Transform(
    struct RSCache_CS2_FunctionSet* fs,
    char* error,
    int error_capacity);

/**
 * The prefix of `RSCache_CS2_Transform` that rebuilds expressions, stopping
 * before the three passes that exist only to print.
 *
 * Seven passes run: dead code, nops, argument reordering, array arguments,
 * same-line combining, stack-definition inlining, nops again. What is left out
 * is `calc_types`, `calc_identifiers` (which name things for a human reader and
 * can fail on a script whose types are contradictory but whose *shape* is
 * perfectly well formed) and `add_short_circuit` (which synthesises the two
 * operators with no bytecode of their own, RSCACHE_CS2_OP_SS_AND/_OR).
 *
 * This is where the optimizer works: the IR is expression trees over labels
 * and branches, every stack type is still recoverable from the variables
 * themselves, and nothing in it is un-lowerable. Running the last three passes
 * would add nodes `cs2_lower.c` cannot emit and refuse scripts that lower fine.
 */
bool
RSCache_CS2_TransformCore(
    struct RSCache_CS2_FunctionSet* fs,
    char* error,
    int error_capacity);

#endif
