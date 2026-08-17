/*
 * The optimizer: IR in, better IR out.
 *
 * Sits between `RSCache_CS2_TransformCore` and `RSCache_CS2_Lower`, and every
 * pass in it is written against two rules.
 *
 * The first is that a refusal is free and a miscompile is not. Nothing here
 * guesses: a pass that cannot prove a transformation safe declines it, the
 * script comes out as it went in, and the worst case is a script that runs
 * exactly as fast as it did before. `cs2_effects.h` is where "safe" is defined
 * and it defaults to "no".
 *
 * The second is that the check is the identity gate. `cs2 lower` proves the IR
 * round-trips the whole cache byte for byte; `cs2 optimize` runs the same
 * pipeline with the passes switched on and re-interprets the result, so a pass
 * that breaks the operand stack is caught on every script it touches rather
 * than on whichever one someone runs.
 */
#ifndef RSCACHE_CS2_OPT_H
#define RSCACHE_CS2_OPT_H

#include "cs2_decompile.h"
#include "cs2_ir.h"

#include <stdbool.h>

enum RSCache_CS2_OptLevel
{
    /** Lower only. The identity baseline. */
    RSCACHE_CS2_OPT_NONE = 0,
    /** Local: constant/copy propagation, folding, branch folding, dead code. */
    RSCACHE_CS2_OPT_LOCAL = 1,
    /** Adds inlining, tail-recursion elimination and loop unrolling. */
    RSCACHE_CS2_OPT_FULL = 2,
};

struct RSCache_CS2_OptOptions
{
    int level;

    /** Largest callee, in instructions, that is inlined on size alone. */
    int inline_max_callee_insns;
    /** A caller may not grow past this many instructions by inlining. */
    int inline_max_insns;
    /** How many times to re-scan a caller for newly exposed calls. */
    int inline_rounds;
    /** Copies of a self-call to unroll before leaving the residual call. */
    int recursion_unroll_depth;
    /** Trip count at or below which a counted loop is fully unrolled. */
    int loop_unroll_max_trips;
    /** Body instructions × trips ceiling for a full unroll. */
    int loop_unroll_max_insns;

    /**
     * Where callee bytecode comes from, and what typing it needs.
     *
     * Inlining interprets the callee afresh rather than sharing the caller's
     * IR, so that the driver can hand over the callee's *optimized* bytes and
     * the caller inlines what will actually ship.
     */
    struct RSCache_CS2_DecompileOptions callees;
};

struct RSCache_CS2_OptStats
{
    int constants_folded;
    int constants_propagated;
    int branches_folded;
    int instructions_removed;
    int calls_inlined;
    int tail_calls_looped;
    int loops_unrolled;
    int slots_coalesced;
};

/** Fill in the defaults for a level; the caller may then override any of them. */
void
RSCache_CS2_OptDefaults(struct RSCache_CS2_OptOptions* options, int level);

/**
 * Optimize one function in place.
 *
 * Returns false with `error` filled in only when the IR was found inconsistent
 * — which means a pass has a bug, not that this script is unusual. A script
 * nothing can be done with returns true, unchanged, with zeroed stats.
 */
bool
RSCache_CS2_Optimize(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_OptOptions* options,
    struct RSCache_CS2_OptStats* stats,
    char* error,
    int error_capacity);

#endif
