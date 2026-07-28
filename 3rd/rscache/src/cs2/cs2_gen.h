/*
 * Structured IR -> CS2 source text.
 *
 * Ported from RuneStar/cs2 `cg/StrictGenerator.kt`. "Strict" in the sense that
 * the output is meant to compile back: no comments beyond the leading script
 * id, no reformatting, and a refusal rather than an approximation wherever a
 * value has no legal spelling.
 */
#ifndef RSCACHE_CS2_GEN_H
#define RSCACHE_CS2_GEN_H

#include "cs2_cfa.h"
#include "cs2_ir.h"
#include "cs2_names.h"

#include <stdbool.h>

/**
 * Write one function as source into `out`.
 *
 * `script_name` is the `[trigger,name]` heading. Returns false with `error`
 * filled in when the IR cannot be spelled — a constant with no literal form, or
 * a call whose target's recorded name has the wrong trigger for the call site.
 */
bool
RSCache_CS2_Generate(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    struct RSCache_CS2_Construct* root,
    const char* script_name,
    const struct RSCache_CS2_Names* names,
    struct RSCache_CS2_StrBuf* out,
    char* error,
    int error_capacity);

/**
 * The name a script decompiles under.
 *
 * The cache's own table is preferred; failing that the name is synthesised as
 * `script<id>`, with the trigger taken from how the script was called — or, for
 * one never called in this run, from whether it returns anything.
 */
const char*
RSCache_CS2_FunctionName(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_Names* names,
    char* out,
    int out_capacity);

#endif
