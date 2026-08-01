#ifndef SRC_SERVERSCRIPT_SSC_H
#define SRC_SERVERSCRIPT_SSC_H

/*
 * RuneScript compiler: .rs2 source -> script.dat / script.idx.
 *
 * Single pass. The parser emits bytecode as it goes and backpatches branch
 * targets, rather than building an AST first — which is how RuneScript
 * compilers are normally written and removes a whole layer of the program. It
 * works because the language has no construct whose code depends on something
 * later in the same expression; only jump *targets* are unknown at emit time,
 * and those are exactly what backpatching handles.
 *
 * Symbol resolution comes from `.pack` files (id=name, one per line) and
 * `.constant` files, the same inputs the reference compiler uses. Command
 * signatures come from the generated opcode table, so a command's arity has one
 * source of truth shared with the VM.
 *
 * Scope, as measured against LostCity's own 1,257 game scripts rather than
 * guessed at. In: trigger headers (bare, braced, category `_name` and coord
 * subjects), if / else if / else with optional braces, while, switch_<type>,
 * return, def_<type> with or without an initialiser, single and multiple
 * assignment, all four variable namespaces plus their `.`-prefixed secondary
 * form, ^constants, ~proc() / @label() including dot-named scripts, bare script
 * names for proc/label/queue/timer arguments, command calls with the .dot form
 * and the `queue*(...)(...)` vararg form, calc() and parenthesised expressions,
 * short-circuit conditions with grouping, string literals with full `<expr>`
 * interpolation, db `table:column` references, hex, negative and coord
 * literals.
 *
 * Out, and each for a stated reason:
 *
 *   arrays               the reference throws on all three opcodes and the
 *                        corpus never emits them
 *   stat / npc_stat /    bare-name enumerations whose values are not in the
 *   fontmetrics names    reference checkout. Deliberately not guessed: a wrong
 *                        stat or font id compiles cleanly and silently reads
 *                        the wrong thing, which is worse than a compile error.
 *                        Seeded enumerations (npc_mode, locshape, the
 *                        ScriptVarType names) all came from a verifiable source.
 */

#include "ssvm_script.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    /* LostCity's own content is 9,334 scripts, so this is not a theoretical
     * bound. It was 4096, and the declare pass silently dropped every name past
     * it — which surfaced as "no proc named update_all" for a proc that plainly
     * exists, in a file the compiler had already read. */
    SSC_MAX_SCRIPTS = 16384,
    SSC_MAX_OPS = 8192,
    SSC_MAX_LOCALS = 256,
    SSC_MAX_SWITCH_TABLES = 32,
    SSC_MAX_SWITCH_CASES = 256,
    SSC_MAX_SYMBOLS = 65536,
    SSC_MAX_NAME = 128,
    /* `$a, $b, $c = ~proc()` — a proc may return several values at once.
     *
     * This was 8, which is smaller than the reference's own content needs:
     * `[proc,equip_get_bonuses]` returns thirteen — the eleven equipment
     * bonuses plus the prayer and range ones — and every caller unpacks all
     * thirteen in one assignment. A round number chosen without a corpus to
     * check it against, in other words, and it rejected a verbatim port. */
    SSC_MAX_ASSIGN_TARGETS = 32,
    /* Values in a `queue*(...)(a, b, c)` vararg block. */
    SSC_MAX_VARARG_TYPES = 16,
};

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

struct SSC_Diag
{
    char file[256];
    int line;
    char message[256];
};

/* ------------------------------------------------------------------ */
/* Symbols                                                             */
/* ------------------------------------------------------------------ */

/** Which namespace a name lives in. Mirrors the `.pack` file it came from. */
enum SSC_SymbolKind
{
    SSC_SYM_UNKNOWN = 0,
    SSC_SYM_NPC,
    SSC_SYM_OBJ,
    SSC_SYM_LOC,
    SSC_SYM_INV,
    SSC_SYM_SEQ,
    SSC_SYM_SPOTANIM,
    SSC_SYM_COMPONENT,
    SSC_SYM_INTERFACE,
    SSC_SYM_VARP,
    SSC_SYM_VARBIT,
    SSC_SYM_VARN,
    SSC_SYM_VARS,
    SSC_SYM_NPC_MODE,
    SSC_SYM_LOCSHAPE,
    SSC_SYM_TYPE,
    SSC_SYM_DBTABLE,
    SSC_SYM_DBCOLUMN,
    SSC_SYM_ENUM,
    SSC_SYM_STRUCT,
    SSC_SYM_PARAM,
    SSC_SYM_CATEGORY,
    SSC_SYM_SYNTH,
    SSC_SYM_STAT,
    SSC_SYM_CONSTANT,
    SSC_SYM_SCRIPT,
    SSC_SYM_KIND_COUNT,
};

struct SSC_Symbol
{
    char name[SSC_MAX_NAME];
    int32_t value;
    enum SSC_SymbolKind kind;
    /** Constants only: the literal text, so `^player_run_off` can expand to a
     *  string as easily as to a number. */
    char* text;
    /** Constants only: `path:line`, kept so a duplicate declaration can name
     *  both sites. A constant is the one symbol kind whose source is a file the
     *  author wrote rather than a generated index, so it is the one worth
     *  carrying provenance for. */
    char* origin;
};

/**
 * A varp that has varbits packed into it — a *carrier*.
 *
 * A varbit is a named bit range inside a varp, so `%carrier = value` does not
 * write a variable, it overwrites every variable that shares the container. That
 * is `docs/LOSTCITY_PORT_TRIAGE.md` §7.5's whole-varp-write class, and the reason
 * it needs a compiler rule rather than a review habit is that nothing about it
 * fails: the name resolves, the opcode is legal, the write lands, and the damage
 * is to somebody else's state.
 *
 * `bits` is how many varbits are based on the varp; `sample` holds the first few
 * for the diagnostic, because a message that names what it is about to destroy is
 * the difference between a rule and an obstacle.
 */
enum
{
    SSC_CARRIER_SAMPLES = 4
};

struct SSC_VarpCarrier
{
    int32_t varp;
    int bits;
    /** Content declared `wholewrite=allow` on this varp (see `fields/varp.ini`). */
    int exempt;
    int sample_count;
    char sample[SSC_CARRIER_SAMPLES][SSC_MAX_NAME];
    int sample_start[SSC_CARRIER_SAMPLES];
    int sample_end[SSC_CARRIER_SAMPLES];
};

struct SSC_Symbols
{
    struct SSC_Symbol* entries;
    int count;
    int capacity;
    /** Sorted index over `entries`, rebuilt when a load adds names. */
    int32_t* order;
    int sorted;

    /** Carriers, keyed by varp id, in insertion order. See SSC_SymbolsCarrier. */
    struct SSC_VarpCarrier* carriers;
    int carrier_count;
    int carrier_capacity;
    /** Varps content declared `wholewrite=allow` for, whether or not they carry
     *  anything — kept separately so an exemption naming a varp that carries
     *  nothing can be reported rather than silently accepted. */
    int32_t* exempt;
    int exempt_count;
    int exempt_capacity;
};

void
SSC_SymbolsInit(struct SSC_Symbols* symbols);
void
SSC_SymbolsFree(struct SSC_Symbols* symbols);

int
SSC_SymbolsAdd(
    struct SSC_Symbols* symbols,
    const char* name,
    int32_t value,
    enum SSC_SymbolKind kind,
    const char* text);

/**
 * The symbol kind a register namespace supplies, or SSC_SYM_UNKNOWN.
 *
 * Public so a test can assert the invariant that matters: every kind the compiler
 * can emit has at least one namespace mapping to it. Renaming a register row
 * orphaned SSC_SYM_INTERFACE once and nothing noticed.
 */
enum SSC_SymbolKind
SSC_SymbolKindForNamespace(const char* ns);

/** Loads an `id=name` pack file. Returns the number of entries, or -1. */
int
SSC_SymbolsLoadPack(
    struct SSC_Symbols* symbols,
    const char* path,
    enum SSC_SymbolKind kind);

/** Loads a `^name value` constant file. */
int
SSC_SymbolsLoadConstants(
    struct SSC_Symbols* symbols,
    const char* path);

/** Loads every `*.pack` in a directory, mapping the filename to a kind. */
int
SSC_SymbolsLoadPackDir(
    struct SSC_Symbols* symbols,
    const char* dir);

/** Loads every `*.dbtable` under a directory tree, adding `table:column`
 *  symbols. Requires dbtable.pack to be loaded first. */
int
SSC_SymbolsLoadDbTableDir(
    struct SSC_Symbols* symbols,
    const char* dir);

/**
 * Build the carrier set from a directory's varbit record file (`all.varbit`).
 *
 * The `basevar=` key on each varbit record is the only statement anywhere of
 * which varp a varbit lives inside, and the cache is its author — the same file
 * `mock230_varbit.c` calls "the authority for" the ranges. The compiler reads it
 * rather than deriving anything: a second opinion about which varps are shared
 * would be a second opinion that can drift.
 *
 * Call *after* the pack directories: it resolves `basevar=<name>` through the
 * varp symbols they load. Returns the number of carrier varps found, or -1 when
 * the directory has no varbit record file (not an error — `pack/` does not).
 */
int
SSC_SymbolsLoadVarbitBases(
    struct SSC_Symbols* symbols,
    const char* dir);

/**
 * Read `wholewrite=allow` out of every `*.varp` under a directory tree.
 *
 * The escape hatch for the carrier rule, and it is content's to state, not a
 * compiler flag: a tree that genuinely must write a shared container whole says
 * so beside the variable, next to the `transmit`/`scope`/`protect` keys the same
 * file already carries, where the next reader will see it. See
 * `CONTENT_ARCHITECTURE.md` §8.2(c) — a rule content cannot express is a bug in
 * the namespace, and a rule content states in a place only the compiler reads is
 * the same bug wearing a different hat.
 *
 * Call after SSC_SymbolsLoadVarbitBases. Returns the number of exemptions read.
 */
int
SSC_SymbolsLoadVarpDecls(
    struct SSC_Symbols* symbols,
    const char* dir);

/** The carrier record for a varp id, or NULL when nothing is based on it. */
const struct SSC_VarpCarrier*
SSC_SymbolsCarrier(
    const struct SSC_Symbols* symbols,
    int32_t varp);

/**
 * Seed language-level enumerations that have no `.pack` file.
 *
 * Content writes `npc_setmode(wander)` bare, so those names must exist before
 * anything compiles. Call after the packs, so real content wins a collision.
 */
void
SSC_SymbolsSeedBuiltins(struct SSC_Symbols* symbols);

/** Loads every `*.constant` under a directory tree. */
/**
 * Component symbols, composed as `(interface << 16) | child` from
 * `<content_dir>/interfaces/<name>.compack`. Call *after* the pack directories:
 * it reads the interface symbols they load. Returns the number added.
 */
int
SSC_SymbolsLoadComponentDir(
    struct SSC_Symbols* symbols,
    const char* content_dir);

int
SSC_SymbolsLoadConstantDir(
    struct SSC_Symbols* symbols,
    const char* dir);

/*
 * Refuse to compile against a symbol table that answers a name two ways.
 *
 * The compiler used to be *looser than the server it feeds*, in two places, and
 * both are silent:
 *
 *   - **A constant declared twice.** `mock230_content.c`'s loader errors with
 *     `is declared twice`; `SSC_SymbolsAdd` appends unconditionally and the
 *     binary search then returns whichever copy `qsort` — which is not stable —
 *     happened to land first. So one loader refuses the tree and the other
 *     compiles it to an arbitrary value. Importing LostCity's 1,562 constants
 *     is 111 names that already exist here, 14 of them with a *different*
 *     value, so this has to be an error before that import, not after.
 *
 *   - **A `%name` that is two kinds at once.** `content.ini`'s `vardomain`
 *     column declares varp/varbit/varn/vars to share one RuneScript name domain
 *     and `mock230_content.c` enforces it; the compiler never read the column,
 *     so `resolve_variable` silently tried VARP first. That precedence is what
 *     `docs/LOSTCITY_PORT_TRIAGE.md` §7.5 is about: a name that is both compiles
 *     to the whole-varp write and clobbers every varbit packed into it. The cost
 *     of turning it on is zero today — no name is both — which is the only
 *     moment it is free.
 *
 * Returns the number of problems, each already printed to stderr. Call after
 * every load; a non-zero answer must fail the build.
 */
int
SSC_SymbolsValidate(struct SSC_Symbols* symbols);

/** NULL when the name is unknown. `kind` may be SSC_SYM_UNKNOWN to match any. */
const struct SSC_Symbol*
SSC_SymbolsFind(
    struct SSC_Symbols* symbols,
    const char* name,
    enum SSC_SymbolKind kind);

/* ------------------------------------------------------------------ */
/* Compiler                                                            */
/* ------------------------------------------------------------------ */

struct SSC_Compiler;

struct SSC_Compiler*
SSC_New(struct SSC_Symbols* symbols);

void
SSC_Free(struct SSC_Compiler* compiler);

/**
 * Compile one source file, appending its scripts.
 *
 * Two passes over the file set are required overall: the first registers every
 * script's name so `~proc()` can resolve a callee defined later, the second
 * emits code. SSC_Declare does the first, SSC_CompileFile the second.
 */
int
SSC_Declare(
    struct SSC_Compiler* compiler,
    const char* path,
    struct SSC_Diag* diag);

int
SSC_CompileFile(
    struct SSC_Compiler* compiler,
    const char* path,
    struct SSC_Diag* diag);

/** Compile every `.rs2` under `dir`, recursively, in sorted order. */
int
SSC_CompileDir(
    struct SSC_Compiler* compiler,
    const char* dir,
    struct SSC_Diag* diag);

/** Writes `<dir>/script.dat` and `<dir>/script.idx`. */
int
SSC_Write(
    struct SSC_Compiler* compiler,
    const char* dir,
    struct SSC_Diag* diag);

int
SSC_ScriptCount(const struct SSC_Compiler* compiler);

/** Borrowed; valid until SSC_Free. */
const struct SSVM_Script*
SSC_ScriptAt(
    const struct SSC_Compiler* compiler,
    int index);

#endif
