/*
 * The CS2 decompiler: clientscript bytecode in, CS2 source text out.
 *
 * Ported from RuneStar/cs2. The pipeline is the reference's, in the same order:
 *
 *   interpret   bytecode -> IR, by simulating the operand stack (cs2_interp.c)
 *   transform   nine data-flow passes: dead code, inlining, type and
 *               identifier inference, short-circuit operators (cs2_dfa.c)
 *   reconstruct IR -> structured control flow, via a dominator tree (cs2_cfa.c)
 *   write       structured IR -> source text (cs2_gen.c)
 *
 * Decompiling needs more than the one script's bytes: a `gosub` reveals only a
 * script id, so the callee has to be loaded to learn how many arguments it
 * takes and what it returns. That is what RSCache_CS2_ScriptSource is for. A
 * caller decompiling a whole cache should serve it from the same table 12 it is
 * iterating, and cache the decodes.
 */
#ifndef RSCACHE_CS2_DECOMPILE_H
#define RSCACHE_CS2_DECOMPILE_H

#include "../datatypes/cs2_script.h"
#include "cs2_ir.h"
#include "cs2_names.h"
#include "cs2_types.h"

#include <stdbool.h>

/** Where the decompiler gets scripts it did not start with. */
struct RSCache_CS2_ScriptSource
{
    void* user;
    /** NULL when the id is not in this cache. */
    const struct RSCache_CS2_Script* (*load)(void* user, int script_id);
};

/**
 * Param id -> value type, for the `*_param` commands.
 *
 * Their result type is not in the bytecode: it is a property of the param
 * config the id names. Without this the decompiler cannot know whether
 * `oc_param` pushed an int or a string, which is a stack-shape question, not a
 * cosmetic one — so an unknown param id is a hard failure, exactly as upstream.
 */
struct RSCache_CS2_ParamTypes
{
    void* user;
    /** RSCACHE_CS2_TYPE_NONE when unknown. */
    enum RSCache_CS2_Type (*load)(void* user, int param_id);
};

/**
 * dbtable column -> the types of its fields, for the `db_*` commands.
 *
 * Same shape of problem as `param_types`, one level deeper. A `dbcolumn`
 * literal packs `(table << 12) | (column << 4) | (field + 1)`; field 0 means
 * "the whole tuple", so `db_getfield` pushes one value or all of them, and
 * `db_find` pops a search value whose int-vs-string stack is the indexed
 * field's. None of that is in the bytecode — it is in the dbtable config the
 * column names — so without this hook a script reading a multi-field column
 * desynchronises the operand stack and is refused.
 */
struct RSCache_CS2_DbColumnTypes
{
    void* user;
    /**
     * Writes the column's field types in order and returns the count, or -1
     * when the table or column is not in this cache. At most `capacity`.
     */
    int (*load)(
        void* user,
        int table_id,
        int column_id,
        enum RSCache_CS2_Type* out,
        int capacity);
};

struct RSCache_CS2_DecompileOptions
{
    struct RSCache_CS2_ScriptSource scripts;
    struct RSCache_CS2_ParamTypes param_types;
    struct RSCache_CS2_DbColumnTypes db_columns;
    /** Optional; without it every id decompiles under a synthesised name. */
    const struct RSCache_CS2_Names* names;
};

/**
 * The stack types a script leaves behind, read off the trailing constant pushes
 * before its final `return`. Returns the count, writing at most `capacity`.
 */
int
RSCache_CS2_ScriptReturnTypes(
    const struct RSCache_CS2_Script* script,
    enum RSCache_CS2_StackType* out,
    int capacity);

/**
 * Decompile one script.
 *
 * Returns a NUL-terminated source listing the caller must free, or NULL on
 * failure with `error` (when given) filled in. Failure is the normal outcome
 * for a script whose callee is missing from the cache, or that uses an opcode
 * with no recorded signature — the reference throws in both cases, and
 * guessing would produce a confident decompile of a different program.
 *
 * `out_name` receives the script's name (`[proc,min]`), also caller-freed.
 */
char*
RSCache_CS2_Decompile(
    int script_id,
    const struct RSCache_CS2_DecompileOptions* options,
    char** out_name,
    char* error,
    int error_capacity);

#endif
