/*
 * Symbolic names for int constants.
 *
 * Ported from RuneStar/cs2 `Resources.kt` and `cg/Ints.kt`. A CS2 int is only
 * a number in the bytecode; whether it prints as `995`, `coins_995`,
 * `^iftype_rectangle` or `0_50_50_1_2` depends on the prototype the type solver
 * settled on, and — for the id-keyed types — on a name table.
 *
 * The tables are *not* in the cache. Jagex ships numeric ids; the names are
 * community-recovered and live in the tab-separated files RuneStar publishes
 * (`obj-names.tsv`, `script-names.tsv`, …). They are therefore optional and
 * loaded from a directory at run time rather than vendored: without them a
 * decompile is still correct, just less readable (`obj_995` instead of
 * `coins_995`).
 *
 * Two tables are load-bearing rather than cosmetic:
 *
 *  - `param-types.tsv` decides whether an `*_param` command pushes an int or a
 *    string. A cache's own param config answers the same question, which is
 *    what the tool prefers; the file is the fallback for a cache without one.
 *  - Four types (`boolean`, `stat`, `maparea`, `fontmetrics`) have no numeric
 *    fallback spelling at all in the language, so an id missing from those
 *    tables makes the script unprintable. That is upstream's behaviour and is
 *    reported rather than papered over.
 */
#ifndef RSCACHE_CS2_NAMES_H
#define RSCACHE_CS2_NAMES_H

#include "cs2_support.h"
#include "cs2_types.h"

#include <stdbool.h>

enum RSCache_CS2_NameTable
{
    RSCACHE_CS2_NAMES_BOOLEAN = 0,
    RSCACHE_CS2_NAMES_FONTMETRICS,
    RSCACHE_CS2_NAMES_GRAPHIC,
    RSCACHE_CS2_NAMES_INTERFACE,
    RSCACHE_CS2_NAMES_INV,
    RSCACHE_CS2_NAMES_LOC,
    RSCACHE_CS2_NAMES_MAPAREA,
    RSCACHE_CS2_NAMES_MODEL,
    RSCACHE_CS2_NAMES_NPC,
    RSCACHE_CS2_NAMES_OBJ,
    RSCACHE_CS2_NAMES_PARAM,
    RSCACHE_CS2_NAMES_SEQ,
    RSCACHE_CS2_NAMES_STAT,
    RSCACHE_CS2_NAMES_STRUCT,
    RSCACHE_CS2_NAMES_SYNTH,
    RSCACHE_CS2_NAMES_CHATFILTER,
    RSCACHE_CS2_NAMES_CHATTYPE,
    RSCACHE_CS2_NAMES_CLIENTTYPE,
    RSCACHE_CS2_NAMES_IFTYPE,
    RSCACHE_CS2_NAMES_KEY,
    RSCACHE_CS2_NAMES_SETPOSH,
    RSCACHE_CS2_NAMES_SETPOSV,
    RSCACHE_CS2_NAMES_SETSIZE,
    RSCACHE_CS2_NAMES_SETTEXTALIGNH,
    RSCACHE_CS2_NAMES_SETTEXTALIGNV,
    RSCACHE_CS2_NAMES_WINDOWMODE,

    RSCACHE_CS2_NAMES_TABLE_COUNT,
};

struct RSCache_CS2_Names
{
    struct RSCache_CS2_Arena arena;
    struct RSCache_CS2_IntMap tables[RSCACHE_CS2_NAMES_TABLE_COUNT];
    /** script id -> `[trigger,name]` string. */
    struct RSCache_CS2_IntMap script_names;
    /** param id -> enum RSCache_CS2_Type, stored offset by one so 0 means absent. */
    struct RSCache_CS2_IntMap param_types;
};

void
RSCache_CS2_NamesInit(struct RSCache_CS2_Names* names);

void
RSCache_CS2_NamesFree(struct RSCache_CS2_Names* names);

/**
 * Load every `*-names.tsv`, `script-names.tsv` and `param-types.tsv` found in
 * `directory`. Missing files are not an error — each table is independent.
 *
 * Returns the number of files read, or -1 if the directory cannot be opened.
 */
int
RSCache_CS2_NamesLoadDirectory(struct RSCache_CS2_Names* names, const char* directory);

const char*
RSCache_CS2_NamesLookup(
    const struct RSCache_CS2_Names* names,
    enum RSCache_CS2_NameTable table,
    int id);

/**
 * Name -> id, the reverse direction the compiler needs.
 *
 * Returns false when the name is absent. Several tables are deliberately
 * non-unique (two objs may share a name), which is why the decompiler always
 * appends the id for those — the reverse lookup is only consulted for the
 * tables where a name identifies one record.
 */
bool
RSCache_CS2_NamesLookupId(
    const struct RSCache_CS2_Names* names,
    enum RSCache_CS2_NameTable table,
    const char* name,
    int* out_id);

/** Script name (`[proc,min]` or just `min`) -> script id. */
bool
RSCache_CS2_NamesScriptId(
    const struct RSCache_CS2_Names* names,
    const char* name,
    int* out_id);

/** The cache name string for a script, e.g. `[proc,min]`, or NULL. */
const char*
RSCache_CS2_NamesScript(const struct RSCache_CS2_Names* names, int script_id);

/** RSCACHE_CS2_TYPE_NONE when the param is unknown. */
enum RSCache_CS2_Type
RSCache_CS2_NamesParamType(const struct RSCache_CS2_Names* names, int param_id);

/** Record a param's type from a source other than the TSV — a cache, say. */
void
RSCache_CS2_NamesSetParamType(
    struct RSCache_CS2_Names* names,
    int param_id,
    enum RSCache_CS2_Type type);

/**
 * Format an int constant as source, given the prototype the solver assigned.
 *
 * Writes into `out` and returns true; returns false when the constant has no
 * spelling under that prototype — an unnamed `stat`, or a colour with a byte
 * set above its 24 bits. Upstream throws in exactly those cases, and a wrong
 * guess would produce source that does not compile back.
 */
bool
RSCache_CS2_NamesFormatInt(
    const struct RSCache_CS2_Names* names,
    int value,
    enum RSCache_CS2_Type type,
    const char* identifier,
    char* out,
    int out_capacity);

#endif
