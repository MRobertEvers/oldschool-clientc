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

#endif
