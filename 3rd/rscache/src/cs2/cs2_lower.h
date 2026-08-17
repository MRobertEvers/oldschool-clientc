/*
 * IR -> clientscript bytecode.
 *
 * The piece the pipeline never had. `cs2_interp.c` turns bytecode into IR by
 * simulating the operand stack; this turns it back, and the two are checked
 * against each other the only way that means anything: lower what was just
 * interpreted and compare the bytes with what the cache held (`cs2 lower`).
 *
 * It exists because the optimizer cannot go out through the source. The
 * generator needs `cs2_cfa.c` to structure the flow graph, and the cfa
 * recognises only the shapes Jagex's compiler emits — while every worthwhile
 * transformation wants a jump the language cannot spell: an early return out of
 * an inlined body, an exit from the middle of an unrolled loop copy, a
 * tail-call's back edge. Bytecode has `goto`; CS2 source does not.
 *
 * What it is NOT is a second compiler. It accepts one shape of IR — the one
 * `RSCache_CS2_TransformCore` leaves behind — and refuses anything else rather
 * than guessing. In particular it must run *before* `calc_types` /
 * `calc_identifiers` / `add_short_circuit`, the last of which synthesises
 * operators (RSCACHE_CS2_OP_SS_AND/_OR) that have no bytecode at all.
 */
#ifndef RSCACHE_CS2_LOWER_H
#define RSCACHE_CS2_LOWER_H

#include "../datatypes/cs2_script.h"
#include "cs2_ir.h"

#include <stdbool.h>

struct RSCache_CS2_LowerOptions
{
    /**
     * Re-emit the unreachable `goto` that `cs2_remove_dead_code` deleted after
     * a `return`.
     *
     * Only ever affects bytes, never behaviour — the instruction cannot be
     * reached. On for the identity check, which is comparing against a stream
     * that has it; off for optimizer output, which has no reason to carry it.
     */
    bool keep_dead_gotos;

    /**
     * Take the frame's local counts from what the original script declared
     * rather than from what this body actually references.
     *
     * The two agree on every script in cache.osrs239 — a corpus check found no
     * script that references a slot at or above its declared count — but they
     * stop agreeing the moment a pass deletes the last use of a local, and the
     * identity check wants the original's numbers.
     */
    bool preserve_frame_counts;

    /** Signature string for the output; copied. NULL emits an empty one. */
    const char* signature;
};

/**
 * Lower one function into `out`.
 *
 * `out` is filled with heap buffers it owns — release it with
 * `RSCache_CS2_ScriptFree`, exactly like a decoded script. Returns false with
 * `error` filled in and `out` untouched otherwise.
 *
 * Failure is a refusal, not an approximation: an expression kind with no
 * bytecode spelling, a jump to a label that is not in the chain, an operand
 * that does not fit. The caller's answer to a refusal is to ship the script
 * unoptimized, which is always available.
 */
bool
RSCache_CS2_Lower(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_LowerOptions* options,
    struct RSCache_CS2_Script* out,
    char* error,
    int error_capacity);

/** Deep field-by-field comparison, for the identity check. */
bool
RSCache_CS2_ScriptBytesEqual(
    const struct RSCache_CS2_Script* left,
    const struct RSCache_CS2_Script* right);

#endif
