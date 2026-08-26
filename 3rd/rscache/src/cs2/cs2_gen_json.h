/*
 * Structured IR -> JSON AST.
 *
 * The same tree `cs2_gen.c` prints as CS2 source, serialized instead as a
 * machine-readable syntax tree. It exists so a consumer outside this library
 * can lower a decompiled script into another language without reparsing the
 * source dialect — cs2dom compiles it to JavaScript.
 *
 * Deliberately *semantic*, not a token dump: an operation carries its opcode
 * and its command name, a local carries its inferred type, a constant carries
 * the typing that decided its spelling. Everything the source generator would
 * have folded into punctuation (calc parentheses, `def_` placement, string
 * interpolation) is left as structure, because a different back end makes
 * those choices differently.
 *
 * The two generators walk the identical `RSCache_CS2_Construct` tree, so a
 * disagreement between them is a bug in one of the two and not a difference of
 * opinion about the program.
 */
#ifndef RSCACHE_CS2_GEN_JSON_H
#define RSCACHE_CS2_GEN_JSON_H

#include "cs2_cfa.h"
#include "cs2_ir.h"
#include "cs2_names.h"

#include <stdbool.h>

/** The `schema` field every emitted document carries. */
#define RSCACHE_CS2_AST_SCHEMA "rscache-cs2-ast/1"

/**
 * Write one function as a JSON syntax tree into `out`.
 *
 * `script_name` is the `[trigger,name]` heading, exactly as the source
 * generator receives it. Returns false with `error` filled in when the IR
 * cannot be described — an unnamed opcode, an operand-stack slot that survived
 * to code generation, a switch whose subject is not a single value.
 */
bool
RSCache_CS2_GenerateJson(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    struct RSCache_CS2_Construct* root,
    const char* script_name,
    const struct RSCache_CS2_Names* names,
    struct RSCache_CS2_StrBuf* out,
    char* error,
    int error_capacity);

#endif
