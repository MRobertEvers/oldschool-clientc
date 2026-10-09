#ifndef SRC_SERVERSCRIPT_SSC_BUILD_H
#define SRC_SERVERSCRIPT_SSC_BUILD_H

/*
 * The incremental script pack build. docs/serverpack.md is the design note;
 * this is the contract.
 *
 * A UNIT is one `.rs2` file. What it compiled to depends on exactly three
 * things, and the build keeps all three per unit in `<out>/ssc.state`:
 *
 *   - its own text (size, mtime and a 64-bit content hash);
 *   - every question it asked while compiling — each symbol-table lookup and
 *     each script name it called, with a digest of the answer it got;
 *   - the compiler and the build's configuration (lanes, roots, packs).
 *
 * A build re-reads the tree's mtimes, re-asks every recorded question against
 * the new tables, and compiles only the units whose text or answers moved.
 * Everything else is reused byte for byte. A unit that changed one proc's
 * signature therefore recompiles exactly the units that call that proc.
 *
 * Script ids are stable: a name keeps the id it was first given for as long as
 * the state file lives, a new name takes the next unused id, and a removed
 * script leaves an empty slot behind rather than handing its id to someone
 * else. A build with no state numbers the tree in the order the old compiler
 * did, so its output is byte-identical to a non-incremental compile.
 *
 * Every build writes `<out>/pack.manifest` (what the server checks staleness
 * against, and what a hot reload diffs) and `<out>/pack.log` (one line per
 * unit and why).
 */

#include "ssc.h"

#include <stdint.h>

/** Loads the build's symbol table into `symbols`. Returns 0 on failure, having
 *  printed why. Called at most once, and only when something needs compiling. */
typedef int (*SSC_SymbolLoader)(void* context, struct SSC_Symbols* symbols);

struct SSC_BuildOptions
{
    /** Where script.dat, script.idx, pack.manifest, pack.log and ssc.state go. */
    const char* out;
    /** The tree root manifest paths are written relative to (`--content-root`). */
    const char* content_root;
    /** Source roots and excluded subtrees, as SSC_CompileRoots takes them. */
    const struct SSC_SourceRoot* roots;
    int root_count;
    const char* const* excludes;
    int exclude_count;
    /** Everything about the configuration that is not a file the build reads:
     *  the lane selection, the pack directory list, the compiler version. A
     *  different value recompiles every unit (and keeps the ids). */
    uint64_t config_key;
    /** For the manifest only: "scape2009_summoning rs558_ancient_curses" or "". */
    const char* lanes;

    SSC_SymbolLoader load_symbols;
    void* load_context;

    /** Recompile every unit, keeping the ids. */
    int full;
    /** Forget the ids too: number the tree afresh, as the old compiler did. */
    int renumber;
    /** Print every unit's line, not only the ones that compiled. */
    int verbose;
    /** Print why this one file is (or is not) stale, change nothing, return. */
    const char* explain;
    /** Decide what would compile and print it; change nothing. */
    int dry_run;
};

/** Returns 0 on success; 1 on a compile error; 2 on a usage or I/O error. */
int
SSC_BuildPack(const struct SSC_BuildOptions* options);

/** A hash of the running executable's bytes, for `config_key`: a rebuilt
 *  compiler is a different compiler. 0 when the executable cannot be read. */
uint64_t
SSC_BuildExecutableHash(const char* argv0);

#endif
