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
 * Scope. In: trigger headers with arguments and returns, if / else if / else,
 * while, switch_<type> with multi-value and default cases, return, def_<type>,
 * assignment, %varp and %varbit, ^constants, ~proc() and @label(), command
 * calls including the .dot form, calc() with + - * / %, conditions with
 * = ! < > <= >= & |, string literals with <$var> interpolation, null, and
 * coord literals. Out: arrays (the reference throws on all three opcodes and
 * the corpus never emits them) and the queue* vararg type-string sugar.
 */

#include "ssvm_script.h"

#include <stddef.h>
#include <stdint.h>

enum
{
    SSC_MAX_SCRIPTS = 4096,
    SSC_MAX_OPS = 8192,
    SSC_MAX_LOCALS = 256,
    SSC_MAX_SWITCH_TABLES = 32,
    SSC_MAX_SWITCH_CASES = 256,
    SSC_MAX_SYMBOLS = 65536,
    SSC_MAX_NAME = 128,
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
};

struct SSC_Symbols
{
    struct SSC_Symbol* entries;
    int count;
    int capacity;
    /** Sorted index over `entries`, rebuilt when a load adds names. */
    int32_t* order;
    int sorted;
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

/** Loads every `*.constant` under a directory tree. */
int
SSC_SymbolsLoadConstantDir(
    struct SSC_Symbols* symbols,
    const char* dir);

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
