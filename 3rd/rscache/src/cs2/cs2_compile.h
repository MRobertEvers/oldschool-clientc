/*
 * The CS2 compiler: CS2 source text in, clientscript bytecode out.
 *
 * This half has no upstream to port from — RuneStar/cs2 only decompiles — so it
 * is written against the language as the decompiler emits it, and checked the
 * only way that means anything: decompile a real script, compile the result,
 * and compare the bytes against what the cache held. `cs2 roundtrip` is that
 * check, and the figures it reports are recorded in EXCEPTIONS.md.
 *
 * Consequences of that design worth knowing before using it:
 *
 *  - The grammar accepted is the grammar generated. Sources written by hand in
 *    a friendlier style (whitespace and comments are fine; reordered arguments,
 *    implicit conversions and operators outside `calc` are not) will be
 *    rejected rather than guessed at.
 *  - Compiling needs the same side information decompiling did: the callee
 *    signatures a `~proc` call implies, and the param types an `*_param`
 *    command implies. Both come through the same interfaces.
 *  - A name table is needed to resolve a symbolic constant (`coins_995`,
 *    `^iftype_rectangle`) back to its id. Sources that only use numeric forms
 *    compile without one.
 */
#ifndef RSCACHE_CS2_COMPILE_H
#define RSCACHE_CS2_COMPILE_H

#include "../datatypes/clientscript.h"
#include "cs2_decompile.h"
#include "cs2_names.h"

#include <stdbool.h>

struct RSCache_CS2_CompileOptions
{
    /** Callee signatures for `~proc` calls and hook registrations. */
    struct RSCache_CS2_ScriptSource scripts;
    struct RSCache_CS2_ParamTypes param_types;
    /**
     * What a dbtable column holds, for the same reason the decompiler needs it:
     * `db_getfield` on a four-field column pushes four values and on a
     * one-field column pushes one, and which it is lives in the cache's config
     * rather than in the opcode (EXCEPTIONS G8).
     *
     * Optional. Without it a db call in statement position gets no
     * `pop_*_discard`, which is a stack imbalance the compiler cannot see —
     * so supply it whenever the cache is at hand. `tools/common/cs2_db_columns.c`
     * is the provider both `cs2` and `cachepack` use.
     */
    struct RSCache_CS2_DbColumnTypes db_columns;
    /** Symbolic constant and script-name resolution; optional. */
    const struct RSCache_CS2_Names* names;
};

/**
 * Compile one script.
 *
 * `out` is filled in on success and must be released with
 * RSCache_ClientScriptFree (it owns its buffers, like a decoded script).
 * Returns false with `error` filled in otherwise, leaving `out` untouched.
 */
bool
RSCache_CS2_Compile(
    const char* source,
    const struct RSCache_CS2_CompileOptions* options,
    struct RSCache_ClientScript* out,
    char* error,
    int error_capacity);

#endif
