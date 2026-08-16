/*
 * Bytecode -> IR, by simulating the operand stack.
 *
 * Ported from RuneStar/cs2 `ir/Interpreter.kt` and the `translate` bodies of
 * `ir/Command.kt`. Each opcode is walked once, popping and pushing synthetic
 * stack variables; the resulting straight-line assignments are what the
 * data-flow passes later fold back into expressions.
 *
 * Every command's pop/push arity must be exactly right. A wrong count does not
 * fail here — it silently shifts every later operand by one and yields a
 * decompile of a different program. That is why an opcode with no recorded
 * signature is a hard error rather than a skip.
 */
#ifndef RSCACHE_CS2_INTERP_H
#define RSCACHE_CS2_INTERP_H

#include "cs2_decompile.h"
#include "cs2_ir.h"

#include <stdbool.h>

/**
 * Interpret `script_ids` into `fs`, loading callees through
 * `options->scripts` as their signatures are needed.
 *
 * Returns false with `error` filled in when a script cannot be interpreted;
 * `fs` is then partially populated and must still be freed.
 */
bool
RSCache_CS2_Interpret(
    struct RSCache_CS2_FunctionSet* fs,
    const int* script_ids,
    int script_id_count,
    const struct RSCache_CS2_DecompileOptions* options,
    char* error,
    int error_capacity);

#endif
