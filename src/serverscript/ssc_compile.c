/*
 * The RuneScript parser and code generator, in one pass.
 *
 * Bytecode is emitted as the parser walks, with jump targets backpatched once
 * known. There is no AST because nothing in the language needs one: no
 * construct's *code* depends on something later in the same expression, only
 * its jump *targets* do, and backpatching handles exactly that.
 *
 * The two things worth knowing before reading:
 *
 *   Branch operands are instruction-index deltas, applied as pc += delta
 *   followed by ++pc, so a branch at index B landing on index N emits N-B-1.
 *   patch_to_here() is the only place that arithmetic appears.
 *
 *   Locals are allocated in two separate spaces, ints and strings, each
 *   indexed from zero, because PUSH_INT_LOCAL and PUSH_STRING_LOCAL address
 *   different arrays. A single counter would compile but read the wrong slot.
 */

#include "ssc.h"
#include <assert.h>

#include "ss_meta.h"
#include "ssc_lex.h"
#include "ssvm_provider.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Build state                                                         */
/* ------------------------------------------------------------------ */

struct SSC_Local
{
    char name[64];
    int slot;
    int is_string;
};

struct SSC_Build
{
    char name[SSC_MAX_NAME];
    int32_t lookup_key;

    uint16_t opcodes[SSC_MAX_OPS];
    int32_t int_operands[SSC_MAX_OPS];
    char* string_operands[SSC_MAX_OPS];
    int op_count;

    struct SSVM_SwitchTable tables[SSC_MAX_SWITCH_TABLES];
    struct SSVM_SwitchCase cases[SSC_MAX_SWITCH_TABLES][SSC_MAX_SWITCH_CASES];
    int table_count;

    struct SSC_Local locals[SSC_MAX_LOCALS];
    int local_count;
    int int_locals;
    int string_locals;
    int int_args;
    int string_args;
    /*
     * The DECLARED type of each argument, in order, as its ScriptVarType char.
     *
     * `int_args`/`string_args` are the counts the VM needs — every type but
     * `string` is an int on the stack — and they were all this compiler kept.
     * The wire format has always carried the types themselves (see
     * SSVM_ScriptEncode), and one caller genuinely needs them rather than the
     * counts: a `::command` fills a `[debugproc]`'s parameters from words typed
     * by a human, so `npc $target` has to know it is an npc *name* to resolve.
     * That is the reference's `script.info.parameterTypes`, used in exactly the
     * same place.
     */
    uint8_t param_types[SS_MAX_PARAM_TYPES];
    int param_type_count;

    int32_t line_pcs[SSC_MAX_OPS];
    int32_t line_numbers[SSC_MAX_OPS];
    int line_count;
    int last_line;
};

struct SSC_Compiler
{
    struct SSC_Symbols* symbols;

    /** Script id -> name, filled by the declare pass so `~proc()` can resolve a
     *  callee defined in a file compiled later. */
    char (*names)[SSC_MAX_NAME];
    /** Script id -> the argument list it declared, also from the declare pass.
     *  A gosub pushes its arguments and the callee pops exactly what its header
     *  said, so a call that passes the wrong number leaves the stack skewed —
     *  and the symptom is the *next* command reading someone else's value, tens
     *  of instructions later. -1 means "declared no argument list". */
    int8_t* name_int_args;
    int8_t* name_str_args;
    /** Script id -> 1 when its return list is exactly one `string`, 0 otherwise,
     *  -1 when the declare pass saw no header for it.
     *
     *  A call is an expression, and the caller has to know which stack the
     *  answer landed on: `~add_article(~stat_name($stat))` passes a string, and
     *  without this the outer call counted it as an int and refused a call that
     *  was correct. One value only; the arity lives in the two arrays below. */
    int8_t* name_str_return;
    /** Script id -> how many values its return list puts on each stack, from the
     *  same declare pass. -1 means the declare pass saw no header for it.
     *
     *  A proc that returns two values passed straight to a proc that takes two
     *  is the reference's ordinary idiom —
     *  `~movecoord_loc_return(~door_open(loc_angle, loc_shape))`, doors.rs2 —
     *  and the argument counter used to score every argument expression as one
     *  value, so it refused that call as "takes 2 int, called with 1". Nothing
     *  was wrong with the script. Counting the callee's declared returns is what
     *  makes the reference's door, double-door and stairs scripts compile as
     *  written; commands never needed it because their argument lists are not
     *  counted at all (their meta carries arity and the VM pops it). */
    int8_t* name_int_returns;
    int8_t* name_str_returns;
    /**
     * Script id -> the symbol kind each declared parameter asks a bare argument
     * to resolve as, in order; SSC_SYM_UNKNOWN for a parameter whose type names
     * no namespace (`int`, `coord`, `boolean`, …).
     *
     * A `~proc` call is the other half of the collision the `arg_kind_hint`
     * table in parse_command covers, and it was the half with nothing guarding
     * it: `~cheat_maxstat(attack)` compiled to `~cheat_maxstat(259)` — varp
     * `attack`, which sorts before the stat — and `::jas` aborted on
     * "stat_advance 259 is not a skill" from inside the callee, naming a line
     * that is correct. The declared parameter type is exactly what the
     * reference resolves these from, and the declare pass already reads it.
     *
     * Positional, so a script with more than SS_MAX_PARAM_TYPES parameters
     * simply stops hinting past that point — the same bound `param_types` has.
     */
    uint8_t (*name_param_kinds)[SS_MAX_PARAM_TYPES];
    /** The return arity of the call `parse_call` most recently finished, so an
     *  argument that *is* a call can be scored by what it actually pushed.
     *  -1 when that callee declared no header. */
    /** Set by parse_command on every command it compiles; an enclosing argument
     *  list reads and clears it per argument to distinguish a direct command
     *  expression from an ordinary one. */
    int saw_command_call;
    /** Fixed return arity of the most recently compiled command, or -1 for a
     *  runtime-typed command such as db_getfield. Unlike procedures, command
     *  arity comes from ss_meta. */
    int last_command_int_returns;
    int last_command_str_returns;
    int last_call_int_returns;
    int last_call_str_returns;
    int name_count;
    int name_capacity;

    struct SSVM_Script* scripts;
    int script_count;

    /** Kind to try first when resolving a bare identifier, or SSC_SYM_UNKNOWN.
     *  Set by parse_command around a command whose arguments are a language
     *  enumeration the packs also use as data names — see parse_expression. */
    enum SSC_SymbolKind arg_kind_hint;

    /** Set for the one argument position that names a *server script* rather
     *  than a value — `settimer(<here>, 30)`. The script namespace is not in the
     *  symbol table, so without this the generic lookup answers first and a
     *  collision compiles to a content id. See parse_expression. */
    int arg_is_script_name;
    /** The trigger that position wants — "timer" for `settimer`, "queue" for the
     *  queue family. Tried before the generic name-addressed order, so
     *  `settimer(x)` cannot pick up a `[proc,x]` that happens to share the name. */
    const char* arg_script_trigger;

    /**
     * The declared types of the last `table:column` an expression resolved, or
     * NULL — the `.dbtable`'s own text ("string", "coord,int,int,int,LIST").
     *
     * `db_getfield` is the one command whose return type is *data*: the column
     * decides which stack it pushes onto, so the opcode table's `str_out = 0`
     * describes only the common int case (ss_meta.h `runtime_typed`). Nothing
     * carried the column's answer to the caller, so a string column read inside
     * a string literal — `mes("You hold the <db_getfield($data,
     * runecraft_table:name, 0)> Talisman…")` — had TOSTRING appended to a value
     * that was already on the *string* stack. The int stack was empty and the
     * script aborted at the message, which is why the talisman-on-ruins
     * teleport (runecraft.rs2:95) died after the animation and the sound with
     * nothing said.
     *
     * Read and cleared per argument by parse_command, which keeps the one from
     * `db_getfield`'s column position rather than whatever a nested call
     * resolved last.
     */
    const char* last_dbcolumn_types;

    /**
     * Set while the file being read is a lane seam (struct SSC_SourceRoot's
     * `weak`), so a name a real lane already declared is stepped over here
     * rather than reported as a duplicate.
     */
    int weak_source;
    /** How many names the strong roots declared, fixed before the weak pass
     *  starts. A weak declaration is an override candidate only against those:
     *  two weak files sharing a name is the ordinary duplicate, not a seam. */
    int strong_name_count;
    /** The seam names a lane took over, so the compile pass drops those bodies
     *  instead of writing them over the lane's at the same id. */
    char (*shadowed)[SSC_MAX_NAME];
    int shadowed_count;
    int shadowed_capacity;

    struct SSC_Build build;
    struct SSC_Lexer lexer;
    struct SSC_Diag* diag;
    int failed;
    char source_path[256];
};

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static int
fail(struct SSC_Compiler* compiler, const char* fmt, ...)
{
    va_list args;

    if( compiler->failed )
        return 0;
    compiler->failed = 1;

    if( compiler->diag )
    {
        snprintf(compiler->diag->file, sizeof(compiler->diag->file), "%s",
                 compiler->lexer.file);
        compiler->diag->line = compiler->lexer.current.line;
        va_start(args, fmt);
        vsnprintf(compiler->diag->message, sizeof(compiler->diag->message), fmt, args);
        va_end(args);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Emission                                                            */
/* ------------------------------------------------------------------ */

static int
emit(struct SSC_Compiler* compiler, int opcode, int32_t operand)
{
    struct SSC_Build* build = &compiler->build;
    int index = build->op_count;

    if( index >= SSC_MAX_OPS )
        return fail(compiler, "script exceeds %d instructions", SSC_MAX_OPS), -1;

    build->opcodes[index] = (uint16_t)opcode;
    build->int_operands[index] = operand;
    build->string_operands[index] = NULL;
    build->op_count++;

    /* One line-table row per source line, not per instruction: the table only
     * has to answer "which line is this pc in", and the reader searches for the
     * last row at or before the pc. */
    if( compiler->lexer.current.line != build->last_line &&
        build->line_count < SSC_MAX_OPS )
    {
        build->line_pcs[build->line_count] = index;
        build->line_numbers[build->line_count] = compiler->lexer.current.line;
        build->line_count++;
        build->last_line = compiler->lexer.current.line;
    }
    return index;
}

static int
emit_string(struct SSC_Compiler* compiler, const char* text)
{
    int index = emit(compiler, SS_OP_PUSH_CONSTANT_STRING, 0);

    if( index >= 0 )
        compiler->build.string_operands[index] = strdup(text ? text : "");
    return index;
}

/** Make the branch at `index` land on the instruction about to be emitted. */
static void
patch_to_here(struct SSC_Compiler* compiler, int index)
{
    if( index < 0 )
        return;
    compiler->build.int_operands[index] = compiler->build.op_count - index - 1;
}

/* ------------------------------------------------------------------ */
/* Locals                                                              */
/* ------------------------------------------------------------------ */

static struct SSC_Local*
find_local(struct SSC_Compiler* compiler, const char* name)
{
    int i;

    for( i = 0; i < compiler->build.local_count; i++ )
    {
        if( strcmp(compiler->build.locals[i].name, name) == 0 )
            return &compiler->build.locals[i];
    }
    return NULL;
}

static struct SSC_Local*
declare_local(struct SSC_Compiler* compiler, const char* name, int is_string)
{
    struct SSC_Build* build = &compiler->build;
    struct SSC_Local* local;

    /* Locals are script-scoped. A declaration in each arm of an `if` names
     * the same slot: only one arm executes, but both are compiled into the one
     * locals table. Allocating a second slot while find_local() kept resolving
     * the first made this shape silently read an uninitialised value:
     *
     *     if (...) { def_int $option = 1; ... }
     *     else     { def_int $option = 2; if ($option = 2) ... }
     *
     * The second assignment went to slot 1 and the comparison read slot 0.
     * Reuse an existing declaration, and reject a spelling that attempts to
     * change stacks because no single slot can represent both types. */
    local = find_local(compiler, name);
    if( local )
    {
        if( local->is_string != is_string )
        {
            fail(compiler, "local '$%s' redeclared with a different type", name);
            return NULL;
        }
        return local;
    }

    if( build->local_count >= SSC_MAX_LOCALS )
    {
        fail(compiler, "more than %d locals in one script", SSC_MAX_LOCALS);
        return NULL;
    }

    local = &build->locals[build->local_count++];
    snprintf(local->name, sizeof(local->name), "%.63s", name);
    local->is_string = is_string;
    /* Ints and strings are addressed out of separate arrays, each indexed from
     * zero, so they need separate counters. Sharing one compiles fine and reads
     * the wrong slot at run time. */
    local->slot = is_string ? build->string_locals++ : build->int_locals++;
    return local;
}

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/* Every ScriptVarType except `string` lives on the int stack, so the compiler
 * only ever needs to know which of the two an expression produced. Types beyond
 * that exist for symbol resolution, which the symbol table already handles. */
static int
type_is_string(const char* type)
{
    return type && strcmp(type, "string") == 0;
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static int
parse_expression(struct SSC_Compiler* compiler, int* is_string);

static int
parse_statement(struct SSC_Compiler* compiler);

static int
script_id_for_name(struct SSC_Compiler* compiler, const char* name)
{
    int i;

    for( i = 0; i < compiler->name_count; i++ )
    {
        if( strcmp(compiler->names[i], name) == 0 )
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Lane seams                                                          */
/* ------------------------------------------------------------------ */

/** Was this name declared by a lane, leaving the base tree's seam unused? */
static int
is_shadowed(struct SSC_Compiler* compiler, const char* name)
{
    int i;

    for( i = 0; i < compiler->shadowed_count; i++ )
    {
        if( strcmp(compiler->shadowed[i], name) == 0 )
            return 1;
    }
    return 0;
}

static void
shadow_add(struct SSC_Compiler* compiler, const char* name)
{
    if( compiler->shadowed_count == compiler->shadowed_capacity )
    {
        int capacity = compiler->shadowed_capacity ? compiler->shadowed_capacity * 2 : 32;
        char (*grown)[SSC_MAX_NAME] =
            (char(*)[SSC_MAX_NAME])realloc(compiler->shadowed, (size_t)capacity * SSC_MAX_NAME);

        assert(grown);
        compiler->shadowed = grown;
        compiler->shadowed_capacity = capacity;
    }
    snprintf(compiler->shadowed[compiler->shadowed_count++], SSC_MAX_NAME, "%s", name);
}

/*
 * Resolve a `%name` to the opcodes that read and write it.
 *
 * Four namespaces share the sigil — player vars, varbits, npc vars and shared
 * world vars — and each has its own opcode pair. Which one a name belongs to is
 * only knowable from the symbol tables, so resolution happens here rather than
 * at each of the (read, write) call sites.
 */
static int
resolve_variable(
    struct SSC_Compiler* compiler,
    const char* name,
    int* out_push,
    int* out_pop,
    int32_t* out_id)
{
    static const struct
    {
        enum SSC_SymbolKind kind;
        int push;
        int pop;
    } k_kinds[] = {
        { SSC_SYM_VARP, SS_OP_PUSH_VARP, SS_OP_POP_VARP },
        { SSC_SYM_VARBIT, SS_OP_PUSH_VARBIT, SS_OP_POP_VARBIT },
        { SSC_SYM_VARN, SS_OP_PUSH_VARN, SS_OP_POP_VARN },
        { SSC_SYM_VARS, SS_OP_PUSH_VARS, SS_OP_POP_VARS },
    };
    size_t i;

    for( i = 0; i < sizeof(k_kinds) / sizeof(k_kinds[0]); i++ )
    {
        const struct SSC_Symbol* symbol =
            SSC_SymbolsFind(compiler->symbols, name, k_kinds[i].kind);

        if( !symbol )
            continue;
        *out_push = k_kinds[i].push;
        *out_pop = k_kinds[i].pop;
        *out_id = symbol->value;
        return 1;
    }
    return 0;
}

/*
 * `%name = value` where `name` is a varp other variables live inside.
 *
 * This is the one rule in the compiler that exists because of a shipped bug
 * rather than a grammar: opening the bank reset the current tab, because
 * `bank_withdrawnotes` named bit 0 of varp 115 and the write went to all 32 bits
 * of it (CONTENT_ARCHITECTURE.md §6.1). Nothing failed — the name resolved, the
 * opcode was legal, and what broke was somebody else's variable.
 *
 * A 2004 varp is very often a rev-230 varbit *range*, so a verbatim port of a
 * reference script writes the container by name and destroys its neighbours
 * (docs/LOSTCITY_PORT_TRIAGE.md §7.5). Refusing the write is the only place that
 * class can be caught before it is state in a save file.
 *
 * Reads are a warning, not an error: reading a carrier gives the packed word,
 * which is wrong but recoverable and is occasionally what a migration wants.
 * Writes are an error, because the damage is already done by the time anyone
 * looks.
 */
static int
check_carrier_write(
    struct SSC_Compiler* compiler,
    const char* name,
    int pop,
    int32_t varp)
{
    const struct SSC_VarpCarrier* carrier;
    char listed[192];
    size_t used = 0;

    if( pop != SS_OP_POP_VARP )
        return 1;
    carrier = SSC_SymbolsCarrier(compiler->symbols, varp);
    if( !carrier || carrier->exempt )
        return 1;

    listed[0] = '\0';
    for( int i = 0; i < carrier->sample_count; i++ )
    {
        int written = snprintf(listed + used, sizeof(listed) - used, "%s%s (%d..%d)",
                               used ? ", " : "", carrier->sample[i], carrier->sample_start[i],
                               carrier->sample_end[i]);

        if( written < 0 || (size_t)written >= sizeof(listed) - used )
            break;
        used += (size_t)written;
    }
    return fail(compiler,
                "`%%%s` is varp %d, which %d varbit(s) are packed into — writing it whole "
                "destroys them (%s%s). Write the varbit, or declare `wholewrite=allow` on "
                "the varp with a reason",
                name, varp, carrier->bits, listed,
                carrier->bits > carrier->sample_count ? ", ..." : "");
}

/*
 * Reading a carrier gives the packed word, not a value.
 *
 * A warning and not an error, because the read is recoverable and there are real
 * uses for the word — a migration that unpacks it by hand, a transmit check. What
 * it is almost never is what the reference script meant, so it says so.
 *
 * `wholeread=allow` is where content states one of those real uses, and it is a
 * different key from `wholewrite=allow` on purpose: a whole read is recoverable
 * and a whole write is not, so silencing the first must not license the second.
 * A `wholewrite` exemption covers reads too — a varp that may be overwritten
 * whole has already conceded the packing.
 */
static void
warn_carrier_read(
    struct SSC_Compiler* compiler,
    const char* name,
    int push,
    int32_t varp)
{
    const struct SSC_VarpCarrier* carrier;

    if( push != SS_OP_PUSH_VARP )
        return;
    carrier = SSC_SymbolsCarrier(compiler->symbols, varp);
    if( !carrier || carrier->exempt || carrier->read_exempt )
        return;
    fprintf(stderr,
            "sscompile: %s:%d: warning: `%%%s` reads varp %d whole, and %d varbit(s) are "
            "packed into it — this is the container, not a value\n",
            compiler->lexer.file, compiler->lexer.current.line, name, varp, carrier->bits);
}

/**
 * Resolve a bare name that refers to a script rather than to a value.
 *
 * `queue(my_handler, 3, 0)` names `[queue,my_handler]`, `settimer(tick_me, 10)`
 * names `[timer,tick_me]`, and `gosub(helper)` names `[proc,helper]` — the
 * argument's declared type is what picks the trigger. Rather than thread the
 * parameter type down here, try each name-addressed trigger in turn: the
 * namespaces do not overlap in practice, and a name in none of them falls
 * through to the normal error.
 *
 * Returns the script id, or -1.
 *
 * `out_stated` (optional) reports *how* it was found: 1 when the argument's own
 * declared trigger named the script exactly, 0 when the fallback order below
 * picked one. Only the caller can act on that, and the difference matters —
 * see the note in parse_expression.
 */
static int
script_id_for_bare_name(struct SSC_Compiler* compiler, const char* name, int* out_stated)
{
    static const char* const k_triggers[] = { "proc",      "label", "queue",
                                              "softtimer", "timer", "walktrigger" };
    size_t i;

    if( out_stated )
        *out_stated = 0;

    /* An argument position that states its trigger gets it first: `settimer(x)`
     * means `[timer,x]` even in a tree that also has a `[proc,x]`, and the order
     * below would otherwise hand it the proc. */
    if( compiler->arg_script_trigger )
    {
        char full[SSC_MAX_NAME];
        int id;

        snprintf(full, sizeof(full), "[%s,%s]", compiler->arg_script_trigger, name);
        id = script_id_for_name(compiler, full);
        if( id >= 0 )
        {
            if( out_stated )
                *out_stated = 1;
            return id;
        }
    }

    for( i = 0; i < sizeof(k_triggers) / sizeof(k_triggers[0]); i++ )
    {
        char full[SSC_MAX_NAME];
        int id;

        snprintf(full, sizeof(full), "[%s,%s]", k_triggers[i], name);
        id = script_id_for_name(compiler, full);
        if( id >= 0 )
            return id;
    }
    return -1;
}

/**
 * The symbol kind a declared parameter type wants a bare argument resolved as,
 * or SSC_SYM_UNKNOWN when the type names no namespace (`int`, `coord`,
 * `boolean`, `npc_uid`, …).
 *
 * A ScriptVarType and the pack namespace it names are the same word for
 * everything that has a pack, so the content register's own mapping answers
 * almost all of it. Three spellings it cannot: `locshape` and `npc_mode` are
 * enumerations with no pack (SSC_SymbolsSeedBuiltins puts them in the table),
 * and `namedobj` is an obj whose name the client shows — the OBJ namespace
 * under a second type name.
 *
 * Deliberately not covered: the script-typed parameters (`queue`, `timer`).
 * Those want `arg_is_script_name` rather than a symbol kind, and no header in
 * this tree declares one — the commands that take a script are covered by
 * k_script_arg_ops in parse_command. A proc that grows such a parameter needs
 * the script path threaded through here as well, not a kind.
 */
static enum SSC_SymbolKind
param_type_kind(const char* type)
{
    if( strcmp(type, "namedobj") == 0 )
        return SSC_SYM_OBJ;
    if( strcmp(type, "locshape") == 0 )
        return SSC_SYM_LOCSHAPE;
    if( strcmp(type, "npc_mode") == 0 )
        return SSC_SYM_NPC_MODE;
    return SSC_SymbolKindForNamespace(type);
}

/** Emit a call to `[proc,name]` or `[label,name]`. */
static int
parse_call(
    struct SSC_Compiler* compiler,
    const char* bare_name,
    int is_label,
    int* is_string)
{
    char full[SSC_MAX_NAME];
    int script_id;
    int pushed_ints = 0;
    int pushed_strs = 0;

    snprintf(full, sizeof(full), "[%s,%s]", is_label ? "label" : "proc", bare_name);
    script_id = script_id_for_name(compiler, full);
    if( script_id < 0 )
        return fail(compiler, "no %s named '%s'", is_label ? "label" : "proc", bare_name);

    /* Arguments are pushed left to right; the callee pops them in reverse into
     * its locals, which restores source order. */
    if( SSC_LexIsPunct(&compiler->lexer, "(") )
    {
        /* A proc's own arguments are values, whatever position the *call* sits
         * in — `settimer(~pick(poison), 5)` must not read `poison` as a script. */
        int saved_script_arg = compiler->arg_is_script_name;
        const char* saved_script_trigger = compiler->arg_script_trigger;
        /* Same reasoning for the kind hint: the enclosing command's hint is
         * about the enclosing command's argument list, and this one has its own
         * types. `stat_advance(~lowest(fishing), 1)` must read `fishing` as
         * whatever `[proc,lowest]` declared, not as a stat. */
        enum SSC_SymbolKind saved_hint = compiler->arg_kind_hint;
        /* Which declared parameter the next argument fills. An argument that is
         * itself a call can push more than one value, and only its declared
         * return arity says how many — once that is unknown, so is every
         * position after it, and hinting stops rather than guesses. */
        int param_index = 0;
        int param_index_known = 1;
        int saved_saw_command = compiler->saw_command_call;
        const uint8_t* param_kinds =
            script_id < compiler->name_count ? compiler->name_param_kinds[script_id] : NULL;

        compiler->arg_is_script_name = 0;
        compiler->arg_script_trigger = NULL;
        SSC_LexNext(&compiler->lexer);
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
        {
            for( ;; )
            {
                int arg_is_string = 0;
                /*
                 * An argument that *is* a call can push more than one value.
                 *
                 * `parse_expression` returns immediately after `parse_call` when
                 * the argument's first token is a `~name`, so this token test is
                 * exactly "the whole argument is a call" — and the nested call
                 * has left its declared return arity behind in
                 * `last_call_*_returns`. Everything else pushes one, which is
                 * what this loop assumed for every argument until 2026-08-02.
                 *
                 * A callee whose header the declare pass never saw reports -1
                 * and is scored as one, i.e. as before: unknown arity must not
                 * turn into a confident wrong number.
                 */
                int arg_is_call = compiler->lexer.current.kind == SSC_TOK_PROC;
                int arg_is_command =
                    compiler->lexer.current.kind == SSC_TOK_IDENT &&
                    SSVM_OpcodeFromName(compiler->lexer.current.text) >= 0;

                compiler->last_call_int_returns = -1;
                compiler->last_call_str_returns = -1;
                compiler->last_command_int_returns = -1;
                compiler->last_command_str_returns = -1;
                compiler->saw_command_call = 0;
                compiler->arg_kind_hint =
                    (param_kinds && param_index_known && param_index < SS_MAX_PARAM_TYPES)
                        ? (enum SSC_SymbolKind)param_kinds[param_index]
                        : SSC_SYM_UNKNOWN;
                if( !parse_expression(compiler, &arg_is_string) )
                    return 0;
                if( arg_is_call && compiler->last_call_int_returns >= 0 )
                {
                    param_index += compiler->last_call_int_returns +
                                   compiler->last_call_str_returns;
                    pushed_ints += compiler->last_call_int_returns;
                    pushed_strs += compiler->last_call_str_returns;
                }
                else if( arg_is_command &&
                         compiler->last_command_int_returns >= 0 )
                {
                    param_index += compiler->last_command_int_returns +
                                   compiler->last_command_str_returns;
                    pushed_ints += compiler->last_command_int_returns;
                    pushed_strs += compiler->last_command_str_returns;
                }
                else if( arg_is_string )
                {
                    param_index++;
                    pushed_strs++;
                }
                else
                {
                    param_index++;
                    pushed_ints++;
                }
                /* A call whose arity nothing here knows — a callee with no
                 * header, or a command, whose meta carries counts rather than
                 * a signature — leaves the next parameter's index a guess. The
                 * arity check below already treats those as approximate; the
                 * hint has to as well, because a wrong hint resolves silently
                 * where a wrong count is at worst reported. */
                if( (arg_is_call && compiler->last_call_int_returns < 0) ||
                    (arg_is_command && compiler->last_command_int_returns < 0) ||
                    (!arg_is_command && compiler->saw_command_call) )
                    param_index_known = 0;
                if( SSC_LexIsPunct(&compiler->lexer, "," ) )
                {
                    SSC_LexNext(&compiler->lexer);
                    continue;
                }
                break;
            }
        }
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
            return fail(compiler, "expected ')' to close the argument list");
        SSC_LexNext(&compiler->lexer);
        compiler->arg_is_script_name = saved_script_arg;
        compiler->arg_script_trigger = saved_script_trigger;
        compiler->arg_kind_hint = saved_hint;
        compiler->saw_command_call = saved_saw_command;
    }

    /*
     * The callee pops exactly what its header declared, so a call that pushed a
     * different number leaves the stack skewed for everything after it. The
     * damage shows up as an unrelated command reading someone else's value tens
     * of instructions later — the failure mode the corpus verifier exists to
     * catch, and one nothing checks for a tree's own content. The declare pass
     * already read every header, so the comparison is free.
     *
     * -1 means the declare pass never saw an argument list for that name, which
     * only happens for a script id that came from somewhere other than a
     * header; those are left unchecked rather than reported wrongly.
     */
    if( script_id < compiler->name_count && compiler->name_int_args[script_id] >= 0 &&
        (pushed_ints != compiler->name_int_args[script_id] ||
         pushed_strs != compiler->name_str_args[script_id]) )
        return fail(compiler,
                    "'%s' takes %d int and %d string arguments, called with %d and %d",
                    full, compiler->name_int_args[script_id],
                    compiler->name_str_args[script_id], pushed_ints, pushed_strs);

    emit(compiler, is_label ? SS_OP_JUMP_WITH_PARAMS : SS_OP_GOSUB_WITH_PARAMS, script_id);
    /* Which stack the answer is on, for a call used as an expression. */
    if( is_string )
        *is_string = script_id < compiler->name_count &&
                     compiler->name_str_return[script_id] == 1;
    /* What this call left behind, for a caller whose argument it is. Written
     * last so a nested call's value is overwritten by the outer one's, which is
     * the order the argument loop above reads it in. */
    if( script_id < compiler->name_count )
    {
        compiler->last_call_int_returns = compiler->name_int_returns[script_id];
        compiler->last_call_str_returns = compiler->name_str_returns[script_id];
    }
    else
    {
        compiler->last_call_int_returns = -1;
        compiler->last_call_str_returns = -1;
    }
    return 1;
}

/*
 * Does a `.dbtable` column's declared type list start with `string`?
 *
 * `types` is the text after the column name — "string", "coord,int,int,int,LIST",
 * "namedobj,int,int". Only the first entry is consulted, because that is the
 * only one an expression can be: a multi-value column pushes every value, and a
 * caller reading one of them (an interpolation, a `def_string` initialiser)
 * reads the first. A column mixing a string with an int is not something an
 * expression can express either way.
 */
static int
dbcolumn_first_type_is_string(const char* types)
{
    assert(types);
    while( *types == ' ' || *types == '\t' )
        types++;
    return strncmp(types, "string", 6) == 0 &&
           (types[6] == '\0' || types[6] == ',' || types[6] == ' ' || types[6] == '\t');
}

/** Emit a command call. `dot` selects the secondary-pointer form. */
static int
parse_command(struct SSC_Compiler* compiler, const char* name, int* is_string)
{
    int opcode;
    const struct SSVM_OpcodeMeta* meta;
    int dot = (name[0] == '.');
    int variadic = 0;
    /* The `.dbtable` types of the column a `db_getfield` was handed, filled by
     * the argument loop below and read by the return-type decision at the end.
     * NULL for every other command, and for a `db_getfield` whose column came
     * from a variable rather than a `table:column` literal. */
    const char* column_types = NULL;

    /*
     * `queue*(script, delay)(args...)` is a different opcode from `queue`, not a
     * modifier on it — QUEUEVARARG rather than QUEUE. The trailing `*` lexes as
     * its own token, so it is still the current one here.
     */
    if( SSC_LexIsPunct(&compiler->lexer, "*") )
    {
        char vararg_name[80];

        SSC_LexNext(&compiler->lexer);
        snprintf(vararg_name, sizeof(vararg_name), "%svararg", name);
        opcode = SSVM_OpcodeFromName(vararg_name);
        if( opcode < 0 )
            return fail(compiler, "'%s' has no vararg form", name);
        variadic = 1;
    }
    else
    {
        opcode = SSVM_OpcodeFromName(name);
    }

    if( opcode < 0 )
        return fail(compiler, "unknown command '%s'", name);

    meta = SSVM_OpcodeMeta(opcode);
    if( !meta->known )
        return fail(compiler, "command '%s' has no declared signature", name);

    /*
     * A stat is a language-level enumeration written bare — `stat(prayer)` —
     * and this cache uses three of the 23 names for other things as well. The
     * hint tells parse_expression to try SSC_SYM_STAT first for the duration of
     * this command's argument list, and only for this family, so a bare
     * `fishing` outside one still resolves to the loc it names. See
     * parse_expression for the collision list and what it costs to get wrong.
     *
     * It nests: a `stat(...)` inside another command's arguments sets the hint
     * on entry and puts back what it found on the way out.
     */
    {
        const char* op_name = SSVM_OpcodeName(opcode);
        enum SSC_SymbolKind saved_hint = compiler->arg_kind_hint;
        enum SSC_SymbolKind base_hint = SSC_SYM_UNKNOWN;
        /*
         * Leading arguments that are a *type* rather than a value.
         *
         * `enum(int, string, my_enum, $key)` states the enum's input and output
         * types, and the reference's typed signature is what tells its compiler
         * to read those two as ScriptVarType names. There is no per-argument
         * type in `ss_meta.gen.h` — it carries counts, not signatures — so the
         * position is stated here, in the same shape and for the same reason as
         * the stat hint above it.
         *
         * It matters because a type name and a command can be the same word.
         * `enum(stat, string, stat_name_enum, $stat)` compiled to the `stat`
         * *command* — commands win everywhere else — which pushed nothing and
         * underflowed the stack one op later, and the failure surfaced as a
         * blank skill name in an unrelated message.
         */
        int type_args = 0;
        int arg_index = 0;
        /* Set when an argument was itself a call whose pushed count this pass
         * cannot know exactly — `arg_index` is then a LOWER bound. */
        int arg_lower_bound = 0;
        int saved_saw_command = compiler->saw_command_call;
        /*
         * Leading arguments that name a *server script* rather than a value.
         *
         * Stated here for the same reason `type_args` is: `ss_meta.gen.h` carries
         * argument counts, not signatures, and the reference's compiler gets this
         * from the declared parameter type (`timer`, `queue`). Every one of these
         * commands takes the script first and integers after, so the count is one.
         *
         * The list is explicit rather than a prefix match on purpose, and the
         * reference's own signatures (`content/scripts/engine.rs2`) are the
         * authority for both membership and the parameter's type — a prefix match
         * sweeps in exactly the wrong ones. `GETWALKTRIGGER` takes nothing, and
         * all three `npc_*` lookalikes take an **int**, not a script:
         *
         *   [command,npc_settimer](int $interval)
         *   [command,npc_queue](int $ai_queue, int $arg, int $delay)
         *   [command,npc_walktrigger](int $ai_queue, int $arg)
         *
         * Those name an `[ai_queue<n>]` / `[ai_timer]` by *number*, keyed on the
         * npc's own type — so hinting them would read an interval as a script
         * name. They were on this list for one revision on the strength of their
         * spelling, which is the same reasoning-from-the-name that caused the bug
         * this table exists to prevent.
         */
        static const struct
        {
            const char* op;
            const char* trigger;
        } k_script_arg_ops[] = {
            { "QUEUE", "queue" },
            { "QUEUEVARARG", "queue" },
            { "STRONGQUEUE", "queue" },
            { "STRONGQUEUEVARARG", "queue" },
            { "WEAKQUEUE", "queue" },
            { "WEAKQUEUEVARARG", "queue" },
            { "LONGQUEUE", "queue" },
            { "LONGQUEUEVARARG", "queue" },
            { "CLEARQUEUE", "queue" },
            { "GETQUEUE", "queue" },
            { "SETTIMER", "timer" },
            { "CLEARTIMER", "timer" },
            { "GETTIMER", "timer" },
            { "SOFTTIMER", "softtimer" },
            { "CLEARSOFTTIMER", "softtimer" },
            { "WALKTRIGGER", "walktrigger" },
        };
        int script_args = 0;
        const char* script_trigger = NULL;
        int saved_script_arg = compiler->arg_is_script_name;
        const char* saved_script_trigger = compiler->arg_script_trigger;
        /* Argument position whose `table:column` decides this command's return
         * type, or -1. `db_getfield` is the whole list: it is the only command
         * that reads a column's declared type and pushes onto the stack that
         * type names. See `last_dbcolumn_types`. */
        int column_types_arg = (op_name && strcmp(op_name, "DB_GETFIELD") == 0) ? 1 : -1;

        if( op_name )
        {
            for( size_t k = 0; k < sizeof(k_script_arg_ops) / sizeof(k_script_arg_ops[0]);
                 k++ )
            {
                if( strcmp(op_name, k_script_arg_ops[k].op) == 0 )
                {
                    script_args = 1;
                    script_trigger = k_script_arg_ops[k].trigger;
                    break;
                }
            }
        }

        if( op_name && strcmp(op_name, "ENUM") == 0 )
            type_args = 2;
        /*
         * ENUM_GETOUTPUTCOUNT takes an enum *value* (the enum's id), not a
         * ScriptVarType. Treating it as a type-position argument made
         * `enum_getoutputcount(enum_4067)` fail with "'enum_4067' is not a
         * type" and forced callers to pass a variable or hardcode the count.
         * LostCity's surface is the same: `enum_getoutputcount(stats)`.
         */

        /*
         * `NPC_BASESTAT` is spelled out because it is the one member of the stat
         * family whose name does not start with either prefix — and the prefixes
         * were the whole membership test, so it was the one stat command whose
         * argument was resolved unhinted. `npc_basestat(hitpoints)` therefore
         * compiled to `npc_basestat(2100)` (param `hitpoints`, which sorts before
         * the stat), `npc_base_stat()` answered its `default:` and pushed 0, and
         * TzKal-Zuk's health overlay read `1200/0`. Exactly the failure the
         * comment above says the hint exists to prevent, missed on a spelling.
         *
         * Every other stat command is covered: `STAT`, `STAT_*` (add/advance/
         * base/boost/drain/heal/random/sub/total) and `NPC_STAT*` (stat/statadd/
         * statheal/statsub). Check a new one against this list by name shape,
         * not by assuming a prefix reaches it.
         */
        if( op_name &&
            (strncmp(op_name, "STAT_", 5) == 0 || strcmp(op_name, "STAT") == 0 ||
             strncmp(op_name, "NPC_STAT", 8) == 0 || strcmp(op_name, "NPC_BASESTAT") == 0) )
            base_hint = SSC_SYM_STAT;
        /* `split_init(..., p12_full)` names an archive in the cache's font
         * metrics index.  Unlike the old reference checkout, rev 239 ships
         * that namespace as pack/13_fonts.pack, so resolve it with the same
         * typed-argument protection stats and interfaces receive. */
        else if( op_name && strcmp(op_name, "SPLIT_INIT") == 0 )
            base_hint = SSC_SYM_FONTMETRICS;
        /*
         * The open family's arguments name interfaces, and a bare interface
         * name is ambiguous for exactly the reason a bare stat name is.
         *
         * `if_openmain_side(farming_tools, farming_tools_side)` compiled to
         * `if_openmain_side(7516, 126)`, because `farming_tools` is interface
         * 125, varp 615 *and* loc 7516, and LOC sorts before INTERFACE. The
         * server then sent a well-formed IF_OPENSUB for an interface that does
         * not exist, the client said "pack 7516 missing from cache; skipping
         * mount", and the panel simply was not on screen — with nothing
         * anywhere naming the mistake. That is what
         * docs/LOSTCITY_PORT_TRIAGE.md §7.5 means by "the danger is the ones
         * that resolve".
         *
         * A component argument — IF_OPENSUB's first, `toplevel_osrs_stretch:
         * mainmodal` — is unaffected: no interface carries that name, so the
         * hinted lookup misses and the unhinted one finds the component. Every
         * other argument in the family is an int.
         */
        else if( op_name && strncmp(op_name, "IF_OPEN", 7) == 0 )
            base_hint = SSC_SYM_INTERFACE;
        /*
         * The spotanim family's leading argument names a spotanim, and a bare
         * spotanim name is ambiguous for exactly the reason a bare stat or
         * interface name is: a spotanim's `anim=` field commonly reuses the
         * spotanim's own name for its seq, so the same word is defined in both
         * the SEQ pack and the SPOTANIM pack.
         *
         * `spotanim_map(tzhaar_rock_smash, coord, 0, 0)` compiled to
         * `spotanim_map(2660, coord, 0, 0)` — 2660 is the seq
         * `tzhaar_rock_smash` (SEQ sorts before SPOTANIM in SSC_SymbolKind),
         * not spotanim 451, which is what the name actually names in the
         * SPOTANIM pack. The client spawned the wrong graphic at the target's
         * tile; JalTok-Jad's ranged "falling rock" effect silently never
         * appeared, while the un-collided `spotanim_pl(firewave_impact, ...)`
         * on the same line rendered fine.
         */
        else if( op_name && (strcmp(op_name, "SPOTANIM_MAP") == 0 ||
                              strcmp(op_name, "SPOTANIM_PL") == 0 ||
                              strcmp(op_name, "SPOTANIM_NPC") == 0) )
            base_hint = SSC_SYM_SPOTANIM;
        compiler->arg_kind_hint = base_hint;

        /* A command may be written bare when it takes nothing — `p_pausebutton;`. */
        if( SSC_LexIsPunct(&compiler->lexer, "(") )
        {
            SSC_LexNext(&compiler->lexer);
            if( !SSC_LexIsPunct(&compiler->lexer, ")") )
            {
                for( ;; )
                {
                    int arg_is_string = 0;

                    /* Same value-vs-expression distinction the proc-call loop
                     * makes: an argument that is itself a call can leave more
                     * than one value. A `~proc` reports its declared arity; a
                     * nested COMMAND does not (a db_getfield on a two-column
                     * field pushes two and says nothing), so any call argument
                     * marks the count as a lower bound rather than exact. */
                    int arg_is_proc = compiler->lexer.current.kind == SSC_TOK_PROC;

                    compiler->last_call_int_returns = -1;
                    compiler->last_call_str_returns = -1;
                    compiler->saw_command_call = 0;
                    compiler->last_dbcolumn_types = NULL;
                    compiler->arg_kind_hint =
                        arg_index < type_args ? SSC_SYM_TYPE : base_hint;
                    /* These commands declare their second argument as an obj
                     * (`namedobj` for the add forms). A bare name still needs
                     * that declared namespace here: `shark` is both obj 385
                     * and npc 1830 in rev 239, and the generic resolver picks
                     * the npc. That made every `obj_add(..., shark, ...)`
                     * quietly put a waterskin-shaped id on the floor. */
                    if( arg_index == 1 && op_name &&
                        (strcmp(op_name, "OBJ_ADD") == 0 ||
                         strcmp(op_name, "OBJ_ADDALL") == 0 ||
                         strcmp(op_name, "OBJ_ADD_PRIVATE") == 0 ||
                         strcmp(op_name, "OBJ_FIND") == 0) )
                        compiler->arg_kind_hint = SSC_SYM_OBJ;
                    /* `sound_synth(arrow_launch, ...)` names a synth, not the
                     * sequence which shares that cache name. As with the
                     * spotanim family, the command signature supplies the
                     * namespace which bare-name sorting cannot infer. */
                    if( arg_index == 0 && op_name &&
                        strcmp(op_name, "SOUND_SYNTH") == 0 )
                        compiler->arg_kind_hint = SSC_SYM_SYNTH;
                    compiler->arg_is_script_name = arg_index < script_args;
                    compiler->arg_script_trigger =
                        arg_index < script_args ? script_trigger : NULL;
                    if( !parse_expression(compiler, &arg_is_string) )
                        return 0;
                    compiler->arg_is_script_name = 0;
                    compiler->arg_script_trigger = NULL;
                    /* `db_getfield(row, table:column, index)`: the column names
                     * the stack the result lands on, and this is the only place
                     * it is visible. Taken from position 1 alone so a nested
                     * call's own column reference cannot stand in for it. */
                    if( column_types_arg == arg_index )
                        column_types = compiler->last_dbcolumn_types;
                    if( arg_is_proc && compiler->last_call_int_returns >= 0 )
                        arg_index += compiler->last_call_int_returns +
                                     compiler->last_call_str_returns;
                    else
                        arg_index++;
                    if( arg_is_proc || compiler->last_call_int_returns >= 0 ||
                        compiler->saw_command_call )
                        arg_lower_bound = 1;
                    if( SSC_LexIsPunct(&compiler->lexer, ",") )
                    {
                        SSC_LexNext(&compiler->lexer);
                        continue;
                    }
                    break;
                }
            }
            if( !SSC_LexIsPunct(&compiler->lexer, ")") )
                return fail(compiler, "expected ')' after arguments to '%s'", name);
            SSC_LexNext(&compiler->lexer);
        }
        compiler->arg_kind_hint = saved_hint;
        compiler->arg_is_script_name = saved_script_arg;
        compiler->arg_script_trigger = saved_script_trigger;
        compiler->saw_command_call = saved_saw_command;

        /*
         * The arity check, and it is not a nicety.
         *
         * A command's argument count was never compared against its signature,
         * so a call with the wrong number compiled clean and desynced the stack
         * at runtime — the VM pops what the opcode says and the surplus stays
         * behind, which shifts every value by one. `queue` states three
         * (`[command,queue](queue $queue, int $delay, int $arg)`); ten sites in
         * this tree passed four, so the SCRIPT ID was read out of the delay slot
         * and each of those queued a garbage id. Nothing reported it: a wrong
         * script id is a valid script id.
         *
         * Skipped for `variadic` commands, whose trailing count is decided by a
         * type string, and for `known == 0`, whose signature is a placeholder
         * the VM already refuses to execute.
         */
        if( !variadic && meta->known )
        {
            int declared = (int)meta->int_in + (int)meta->str_in;

            /*
             * Only two verdicts are sound.
             *
             * `arg_index > declared` is always wrong: every argument pushes at
             * least one value, so more arguments than slots cannot be right.
             *
             * `arg_index < declared` is only wrong when nothing in the list
             * could have pushed extra — a `~proc` returning three, or a
             * `db_getfield` on a `coord,coord` column, legitimately fills
             * several slots from one expression. Reporting those was the
             * check's own false-positive class: `movecoord(coord,
             * ~door_open_move_player_out_of_way($angle))` is correct and reads
             * as two-of-four.
             */
            if( arg_index > declared || (arg_index < declared && !arg_lower_bound) )
            {
                /*
                 * Fatal. It was reported-only for exactly one session — the
                 * length of time it took to clear the ~160 sites the tree had
                 * when the check was written.
                 *
                 * What it found, and none of it was theoretical: `npc_del`
                 * (declared with no arguments) called with one at 19 sites;
                 * `obj_add` given a duration in the COUNT slot at 120, so every
                 * one of those drops spawned 200 items; ten `queue` calls
                 * passing two arguments to a command that states one, which put
                 * the SCRIPT ID in the delay slot; and `mes("::boost <stat> …")`
                 * compiling `<stat>` as a call because the interpolation test
                 * did not ask whether the command took arguments.
                 *
                 * It also caught the first fix for `obj_add` being wrong — 107
                 * of those sites pass `~randomherb`, a proc returning
                 * `(namedobj, int)`, so the count was already there. A check
                 * that only reported would not have said so.
                 */
                return fail(compiler, "'%s' takes %d argument(s), %d given", name, declared,
                            arg_index);
            }
        }
    }

    /*
     * The vararg block, and the type string that describes it.
     *
     * popScriptArgs reads a type string off the top of the stack and uses its
     * characters to decide what to pop next, so the runtime layout is
     *   declared args, vararg values, type string
     * with the type string last. 'i' covers every ScriptVarType except string.
     */
    if( variadic )
    {
        char types[SSC_MAX_VARARG_TYPES + 1];
        int type_count = 0;

        if( !SSC_LexIsPunct(&compiler->lexer, "(") )
            return fail(compiler, "expected '(' for the vararg list of '%s'", name);
        SSC_LexNext(&compiler->lexer);

        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
        {
            for( ;; )
            {
                int arg_is_string = 0;

                if( type_count >= SSC_MAX_VARARG_TYPES )
                    return fail(compiler, "more than %d vararg values",
                                SSC_MAX_VARARG_TYPES);
                if( !parse_expression(compiler, &arg_is_string) )
                    return 0;
                types[type_count++] = arg_is_string ? 's' : 'i';

                if( SSC_LexIsPunct(&compiler->lexer, ",") )
                {
                    SSC_LexNext(&compiler->lexer);
                    continue;
                }
                break;
            }
        }
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
            return fail(compiler, "expected ')' to close the vararg list");
        SSC_LexNext(&compiler->lexer);

        types[type_count] = '\0';
        emit_string(compiler, types);
    }

    /* Commands carry a one-byte operand that is the dot flag, never an id. */
    emit(compiler, opcode, dot ? 1 : 0);
    /* Tell an enclosing argument list that one of its arguments was a command.
     * Fixed signatures keep their exact return arity. Runtime-typed commands
     * (`db_getfield`, params, enum) remain unknown because data chooses both the
     * stack and, for DB columns, the number of values. */
    compiler->saw_command_call = 1;
    compiler->last_command_int_returns = meta->runtime_typed ? -1 : meta->int_out;
    compiler->last_command_str_returns = meta->runtime_typed ? -1 : meta->str_out;

    if( is_string )
        *is_string = column_types ? dbcolumn_first_type_is_string(column_types)
                                  : meta->str_out > 0;
    return 1;
}

/*
 * Is the text between a `<` and its `>` an interpolation, or markup?
 *
 * Both live in string literals and only the first is compiled:
 *
 *   interpolation   <$name>  <%varp>  <~proc($arg)>  <displayname>
 *                   <.displayname>  <tostring($level)>
 *                   <lowercase(oc_name($ingredient))>
 *   markup          <br>  <col=ff0000>  <p,happy>  <lt>
 *
 * A leading sigil settles it immediately. Otherwise the leading identifier has
 * to name a command *and* be followed by `(` or nothing else — that second half
 * matters: `<p,happy>` would slip through on the name test alone if a command
 * were ever called `p`, and the corpus has 6,577 of those.
 *
 * `~` is a sigil for the same reason `$` is, and it was missing: a proc call
 * inside a literal is an ordinary expression to `compile_interpolation`, but
 * this test called it markup, so `mes("a <~stat_name($stat)> level")` reached
 * the player with those 21 characters in it. No client tag begins with `~`, so
 * there is nothing to weigh against.
 */
static int
is_interpolation(const char* inner, size_t length)
{
    size_t i = 0;
    char name[64];
    size_t name_length = 0;

    while( i < length && (inner[i] == ' ' || inner[i] == '\t') )
        i++;
    if( i >= length )
        return 0;

    if( inner[i] == '$' || inner[i] == '%' || inner[i] == '~' )
        return 1;

    if( inner[i] == '.' )
        i++;
    while( i < length && name_length + 1 < sizeof(name) &&
           ((inner[i] >= 'a' && inner[i] <= 'z') || (inner[i] >= 'A' && inner[i] <= 'Z') ||
            (inner[i] >= '0' && inner[i] <= '9') || inner[i] == '_') )
        name[name_length++] = inner[i++];
    name[name_length] = '\0';
    if( name_length == 0 )
        return 0;

    while( i < length && (inner[i] == ' ' || inner[i] == '\t') )
        i++;
    if( i < length && inner[i] != '(' )
        return 0;

    {
        int opcode = SSVM_OpcodeFromName(name);
        const struct SSVM_OpcodeMeta* meta;

        if( opcode < 0 )
            return 0;
        if( i < length ) /* followed by '(' — an ordinary call, always a call */
            return 1;

        /*
         * A bare command name with no `(` is an interpolation only if the
         * command takes nothing. `<displayname>` does; `<stat>` does not, and
         * `mes("::boost <stat> <constant> <percent>")` — a usage line telling
         * the player what to type — compiled to a `stat` CALL with no argument.
         * That popped a value the caller never pushed, so the message printed a
         * skill name pulled off whatever was underneath and the stack was one
         * short from there on.
         *
         * Caught by the new arity check rather than by anyone reading it, which
         * is the argument for the arity check: this had been in three files
         * since the cheats were written.
         */
        meta = SSVM_OpcodeMeta(opcode);
        if( !meta || !meta->known )
            return 0;
        return (int)meta->int_in + (int)meta->str_in == 0;
    }
}

/** Compile `inner` as an expression and leave a STRING on the stack. */
static int
compile_interpolation(
    struct SSC_Compiler* compiler,
    const char* inner,
    size_t length)
{
    struct SSC_Lexer saved = compiler->lexer;
    struct SSC_Lexer nested;
    int is_string = 0;
    int ok;

    SSC_LexInit(&nested, inner, length, saved.file);
    nested.line = saved.current.line;
    compiler->lexer = nested;
    SSC_LexNext(&compiler->lexer);

    ok = parse_expression(compiler, &is_string);
    if( ok && !is_string )
    {
        /* `<tostring($n)>` converts explicitly, but `<$n>` on an int local and
         * any int-returning command do not — they still have to become text
         * before JOIN_STRING sees them. */
        emit(compiler, SS_OP_TOSTRING, 0);
    }

    compiler->lexer = saved;
    return ok;
}

/**
 * A string literal, expanding its interpolations.
 *
 * `"you have <$count> coins"` compiles to three pushes and a JOIN_STRING, the
 * same shape the reference emits. Markup stays inside the literal chunk around
 * it, so `"<p,happy>Hello, <$name>!"` is two literals and one expression rather
 * than four pieces.
 */
static int
parse_string_literal(struct SSC_Compiler* compiler, const char* text)
{
    char chunk[1024];
    size_t chunk_length = 0;
    int parts = 0;
    const char* cursor = text;

    while( *cursor )
    {
        if( *cursor == '<' )
        {
            const char* scan = cursor + 1;
            int depth = 1;

            while( *scan && depth > 0 )
            {
                if( *scan == '<' )
                    depth++;
                else if( *scan == '>' )
                    depth--;
                if( depth > 0 )
                    scan++;
            }

            if( depth == 0 && is_interpolation(cursor + 1, (size_t)(scan - cursor - 1)) )
            {
                if( chunk_length )
                {
                    chunk[chunk_length] = '\0';
                    emit_string(compiler, chunk);
                    parts++;
                    chunk_length = 0;
                }
                if( !compile_interpolation(compiler, cursor + 1, (size_t)(scan - cursor - 1)) )
                    return 0;
                parts++;
                cursor = scan + 1;
                continue;
            }
        }

        if( chunk_length + 1 < sizeof(chunk) )
            chunk[chunk_length++] = *cursor;
        cursor++;
    }

    /* A literal with no interpolations still has to push something, and so does
     * the empty string. */
    if( chunk_length || parts == 0 )
    {
        chunk[chunk_length] = '\0';
        emit_string(compiler, chunk);
        parts++;
    }

    if( parts > 1 )
        emit(compiler, SS_OP_JOIN_STRING, parts);
    return 1;
}

/** Arithmetic inside calc(): + - * / % with the usual precedence. */
static int
parse_calc_term(struct SSC_Compiler* compiler);

static int
parse_calc_factor(struct SSC_Compiler* compiler)
{
    int is_string = 0;

    if( SSC_LexIsPunct(&compiler->lexer, "(") )
    {
        SSC_LexNext(&compiler->lexer);
        if( !parse_calc_term(compiler) )
            return 0;
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
            return fail(compiler, "expected ')' in calc");
        SSC_LexNext(&compiler->lexer);
        return 1;
    }
    return parse_expression(compiler, &is_string);
}

static int
parse_calc_product(struct SSC_Compiler* compiler)
{
    if( !parse_calc_factor(compiler) )
        return 0;

    while( SSC_LexIsPunct(&compiler->lexer, "*") || SSC_LexIsPunct(&compiler->lexer, "/") ||
           SSC_LexIsPunct(&compiler->lexer, "%") )
    {
        char op = compiler->lexer.current.text[0];

        SSC_LexNext(&compiler->lexer);
        if( !parse_calc_factor(compiler) )
            return 0;
        emit(compiler,
             op == '*' ? SS_OP_MULTIPLY : (op == '/' ? SS_OP_DIVIDE : SS_OP_MODULO), 0);
    }
    return 1;
}

static int
parse_calc_term(struct SSC_Compiler* compiler)
{
    if( !parse_calc_product(compiler) )
        return 0;

    while( SSC_LexIsPunct(&compiler->lexer, "+") || SSC_LexIsPunct(&compiler->lexer, "-") )
    {
        char op = compiler->lexer.current.text[0];

        SSC_LexNext(&compiler->lexer);
        if( !parse_calc_product(compiler) )
            return 0;
        emit(compiler, op == '+' ? SS_OP_ADD : SS_OP_SUB, 0);
    }
    return 1;
}

/**
 * One value onto the stack.
 *
 * Reports through `is_string` which stack it landed on, because the caller
 * needs that to pick between PUSH_INT_LOCAL and PUSH_STRING_LOCAL and to know
 * what a local's declared type should be.
 */
static int
parse_expression(struct SSC_Compiler* compiler, int* is_string)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    const struct SSC_Token* token = &lexer->current;
    char text[512];
    int32_t secondary = 0;

    *is_string = 0;

    /* `.%inferno_glyph_dir` reads the variable off the SECONDARY pointer. The
     * lexer leaves the dot as punctuation here (it only glues a dot onto a
     * following identifier, and `%` is not one), so the parser takes it. Var
     * ops carry the flag in bit 16 of the operand, not in the dot byte
     * commands use. */
    if( SSC_LexIsPunct(lexer, ".") && SSC_LexPeek(lexer)->kind == SSC_TOK_VAR )
    {
        secondary = 1 << 16;
        SSC_LexNext(lexer);
    }

    /* A parenthesised expression, which content uses outside calc() too:
     * `$value = (calc(random(1) + 9));`. */
    if( SSC_LexIsPunct(lexer, "(") )
    {
        SSC_LexNext(lexer);
        if( !parse_expression(compiler, is_string) )
            return 0;
        if( !SSC_LexIsPunct(lexer, ")") )
            return fail(compiler, "expected ')' to close a parenthesised expression");
        SSC_LexNext(lexer);
        return 1;
    }

    snprintf(text, sizeof(text), "%s", token->text);

    switch( token->kind )
    {
    case SSC_TOK_INT:
        emit(compiler, SS_OP_PUSH_CONSTANT_INT, token->value);
        SSC_LexNext(lexer);
        return 1;

    case SSC_TOK_STRING:
        *is_string = 1;
        SSC_LexNext(lexer);
        return parse_string_literal(compiler, text);

    case SSC_TOK_LOCAL:
    {
        struct SSC_Local* local = find_local(compiler, text);

        if( !local )
            return fail(compiler, "'$%s' names no local in scope", text);
        *is_string = local->is_string;
        emit(compiler, local->is_string ? SS_OP_PUSH_STRING_LOCAL : SS_OP_PUSH_INT_LOCAL,
             local->slot);
        SSC_LexNext(lexer);
        return 1;
    }

    case SSC_TOK_VAR:
    {
        int push;
        int pop;
        int32_t id;

        if( !resolve_variable(compiler, text, &push, &pop, &id) )
            return fail(compiler, "unknown variable '%%%s'", text);
        warn_carrier_read(compiler, text, push, id);
        emit(compiler, push, id | secondary);
        SSC_LexNext(lexer);
        return 1;
    }

    case SSC_TOK_CONSTANT:
    {
        const struct SSC_Symbol* constant =
            SSC_SymbolsFind(compiler->symbols, text, SSC_SYM_CONSTANT);
        struct SSC_Lexer saved;
        struct SSC_Lexer nested;
        int ok;

        if( !constant || !constant->text )
            return fail(compiler, "unknown constant '^%s'", text);

        /* A constant expands to source text, so compile it by lexing the text
         * in place. That is what makes `^some_coord` and `^some_string` work
         * without the symbol table having to know which it is. */
        saved = compiler->lexer;
        SSC_LexInit(&nested, constant->text, strlen(constant->text), lexer->file);
        nested.line = saved.current.line;
        compiler->lexer = nested;
        SSC_LexNext(&compiler->lexer);
        ok = parse_expression(compiler, is_string);
        compiler->lexer = saved;
        if( !ok )
            return 0;
        SSC_LexNext(lexer);
        return 1;
    }

    case SSC_TOK_PROC:
        SSC_LexNext(lexer);
        return parse_call(compiler, text, 0, is_string);

    case SSC_TOK_IDENT:
    {
        const struct SSC_Symbol* symbol;

        if( strcmp(text, "null") == 0 )
        {
            emit(compiler, SS_OP_PUSH_CONSTANT_INT, -1);
            SSC_LexNext(lexer);
            return 1;
        }
        if( strcmp(text, "true") == 0 || strcmp(text, "false") == 0 )
        {
            emit(compiler, SS_OP_PUSH_CONSTANT_INT, text[0] == 't' ? 1 : 0);
            SSC_LexNext(lexer);
            return 1;
        }
        if( strcmp(text, "calc") == 0 )
        {
            SSC_LexNext(lexer);
            if( !SSC_LexIsPunct(lexer, "(") )
                return fail(compiler, "expected '(' after calc");
            SSC_LexNext(lexer);
            if( !parse_calc_term(compiler) )
                return 0;
            if( !SSC_LexIsPunct(lexer, ")") )
                return fail(compiler, "expected ')' to close calc");
            SSC_LexNext(lexer);
            return 1;
        }

        /*
         * A type-position argument is a type, even when a command shares its
         * spelling — the one place the "commands win" rule below does not hold.
         *
         * `enum(stat, string, ...)`: `stat` there is ScriptVarType.STAT, not the
         * `stat(...)` command, and nothing in the grammar makes that slot a
         * call. parse_command sets this hint for exactly those positions.
         */
        if( compiler->arg_kind_hint == SSC_SYM_TYPE )
        {
            symbol = SSC_SymbolsFind(compiler->symbols, text, SSC_SYM_TYPE);
            if( symbol )
            {
                emit(compiler, SS_OP_PUSH_CONSTANT_INT, symbol->value);
                SSC_LexNext(lexer);
                return 1;
            }
            return fail(compiler, "'%s' is not a type", text);
        }

        /* A name is a command if the opcode table knows it, otherwise a symbol
         * from the packs. Commands win: content never names a symbol after a
         * command, and the reference resolves the same way. */
        if( SSVM_OpcodeFromName(text) >= 0 )
        {
            SSC_LexNext(lexer);
            return parse_command(compiler, text, is_string);
        }

        /*
         * A stat command's arguments name stats, and the bare name is ambiguous.
         *
         * `SSC_SymbolsFind` with no kind returns the lowest-numbered kind that
         * has the name, and this cache collides three of the 23 stat names with
         * something that sorts earlier: `hitpoints` is also param 2100,
         * `attack` is also varp 259, `fishing` is also loc 20926. Without the
         * hint `stat_heal(hitpoints, 3, 0)` compiles to `stat_heal(2100, 3, 0)`
         * and silently heals nothing, which is exactly the failure the
         * reference's typed argument lists prevent and this compiler has no
         * types to prevent with.
         *
         * The hint is set by parse_command for the stat family only, so a bare
         * `fishing` anywhere else still means the loc.
         */
        if( compiler->arg_kind_hint != SSC_SYM_UNKNOWN )
        {
            symbol = SSC_SymbolsFind(compiler->symbols, text, compiler->arg_kind_hint);
            if( symbol )
            {
                emit(compiler, SS_OP_PUSH_CONSTANT_INT, symbol->value);
                SSC_LexNext(lexer);
                return 1;
            }
        }

        /*
         * A queue/timer/softtimer/walktrigger argument names a *script*, and the
         * script namespace lives outside the symbol table — so it has to be
         * consulted before the generic lookup, not after it.
         *
         * It used to be after, and that is the same collision class as the stat
         * and interface hints above, with a worse landing. `settimer(poison, 30)`
         * found obj 273 named `poison` and never reached `[timer,poison]`, so the
         * engine armed a timer whose "script id" was an obj id and then ran
         * whatever script happened to sit at script 273 —
         * `[label,woman_im_looking_for_a_lady]`, whose first line is a
         * `~chatplayer_anim`. The symptom was a stray dialogue box on a player
         * nowhere near West Ardougne, and *which* line it was moved every time
         * the tree grew, because the only thing choosing it was the allocation
         * order of an unrelated namespace.
         *
         * Only the hinted position resolves this way. A bare name anywhere else
         * still means what the packs say it means.
         */
        if( compiler->arg_is_script_name )
        {
            int stated = 0;
            int script_id = script_id_for_bare_name(compiler, text, &stated);

            if( script_id >= 0 )
            {
                /*
                 * Say so when the name was ambiguous: the script won, and the
                 * reader of `settimer(poison, …)` cannot see that from the
                 * source. Silence here is what let the bug above live.
                 *
                 * `stated` is what "ambiguous" means. When the argument's own
                 * trigger named the script exactly — `settimer(poison, 30)`
                 * found `[timer,poison]`, `queue(prince_complete, 0, 0)` found
                 * `[queue,prince_complete]` — the position declared which
                 * namespace it wanted and got it, and there is nothing for a
                 * reader to check. Noting those said "resolved correctly" 25
                 * times a build, which is how a diagnostic stops being read.
                 * The report is kept for the case it was written for: the
                 * fallback trigger order picked one, and *that* choice is a
                 * guess made by k_triggers rather than by the source.
                 */
                symbol = stated ? NULL : SSC_SymbolsFindValue(compiler->symbols, text);
                if( symbol )
                {
                    fprintf(stderr,
                            "sscompile: %s:%d: note: '%s' names both a script and a "
                            "symbol of kind %d (value %d); the script wins in this "
                            "position\n",
                            compiler->lexer.file, compiler->lexer.current.line, text,
                            (int)symbol->kind, symbol->value);
                }
                emit(compiler, SS_OP_PUSH_CONSTANT_INT, script_id);
                SSC_LexNext(lexer);
                return 1;
            }
        }

        /*
         * A bare identifier never means a constant, so the lookup that backs it
         * excludes the kind.
         *
         * Constants are written `^name` and expand to source *text*:
         * SSC_SymbolsLoadConstants keeps the text and passes 0 for the value,
         * because which kind of literal it is is only decidable where it is
         * used. A bare name that matched one therefore did not compile to what
         * the author wrote — `prince_complete` is 110 and compiled to 0 — and
         * the emit below said nothing about it. Same landing as the
         * `settimer(poison, …)` bug above, one namespace over.
         */
        symbol = SSC_SymbolsFindValue(compiler->symbols, text);
        if( symbol )
        {
            /* A `table:column` carries its declared types along, for the one
             * caller that needs them — see `last_dbcolumn_types`. */
            if( symbol->kind == SSC_SYM_DBCOLUMN )
                compiler->last_dbcolumn_types = symbol->text;
            emit(compiler, SS_OP_PUSH_CONSTANT_INT, symbol->value);
            SSC_LexNext(lexer);
            return 1;
        }

        /* Not a hinted position, but still a script name — `[if_button]`-style
         * arguments and anything the hint table does not cover. */
        {
            int script_id = script_id_for_bare_name(compiler, text, NULL);

            if( script_id >= 0 )
            {
                emit(compiler, SS_OP_PUSH_CONSTANT_INT, script_id);
                SSC_LexNext(lexer);
                return 1;
            }
        }
        /* Nothing else claims the name, so a constant of that name is what was
         * meant — and the fix is one character. */
        if( SSC_SymbolsFind(compiler->symbols, text, SSC_SYM_CONSTANT) )
            return fail(compiler,
                        "'%s' is a constant — write '^%s'. A bare name resolves in the pack "
                        "namespaces, where a constant carries no value, so this would have "
                        "compiled to 0",
                        text, text);
        return fail(compiler, "'%s' is not a command, constant, symbol or script", text);
    }

    default:
        return fail(compiler, "expected a value, found '%s'", text);
    }
}

/* ------------------------------------------------------------------ */
/* Conditions                                                          */
/* ------------------------------------------------------------------ */

/*
 * Conditions.
 *
 * `if (a = b & c = d | e = f)` short-circuits, so a condition does not evaluate
 * to a value on the stack — it compiles to branches. Two lists come out:
 *
 *   false_list  branches taken when the condition does not hold; the caller
 *               patches them past the guarded block
 *   true_list   branches taken when an alternative already decided the answer
 *               is yes; the caller patches them to the block's first
 *               instruction
 *
 * Precedence is the usual one: `&` binds tighter than `|`.
 */

#define SSC_MAX_COND_BRANCHES 32

struct SSC_Condition
{
    int true_list[SSC_MAX_COND_BRANCHES];
    int true_count;
    int false_list[SSC_MAX_COND_BRANCHES];
    int false_count;
};

static int
cond_push(
    struct SSC_Compiler* compiler,
    int* list,
    int* count,
    int index)
{
    if( *count >= SSC_MAX_COND_BRANCHES )
        return fail(compiler, "condition has more than %d terms", SSC_MAX_COND_BRANCHES);
    list[(*count)++] = index;
    return 1;
}

/** One comparison. Emits the branch taken when it does NOT hold. */
static int
parse_comparison(struct SSC_Compiler* compiler, int* out_branch)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    int left_is_string = 0;
    int right_is_string = 0;
    char op[4];
    int opcode;

    if( !parse_expression(compiler, &left_is_string) )
        return 0;

    if( lexer->current.kind != SSC_TOK_PUNCT )
        return fail(compiler, "expected a comparison operator in the condition");

    snprintf(op, sizeof(op), "%.3s", lexer->current.text);
    SSC_LexNext(lexer);

    if( !parse_expression(compiler, &right_is_string) )
        return 0;

    /* The emitted branch is the INVERSE of the source comparison, because it
     * jumps over the block when the condition does not hold. */
    if( strcmp(op, "=") == 0 )
        opcode = SS_OP_BRANCH_NOT;
    else if( strcmp(op, "!") == 0 || strcmp(op, "!=") == 0 )
        opcode = SS_OP_BRANCH_EQUALS;
    else if( strcmp(op, "<") == 0 )
        opcode = SS_OP_BRANCH_GREATER_THAN_OR_EQUALS;
    else if( strcmp(op, ">") == 0 )
        opcode = SS_OP_BRANCH_LESS_THAN_OR_EQUALS;
    else if( strcmp(op, "<=") == 0 )
        opcode = SS_OP_BRANCH_GREATER_THAN;
    else if( strcmp(op, ">=") == 0 )
        opcode = SS_OP_BRANCH_LESS_THAN;
    else
        return fail(compiler, "'%s' is not a comparison operator", op);

    *out_branch = emit(compiler, opcode, 0);
    return 1;
}

static int
parse_condition(struct SSC_Compiler* compiler, struct SSC_Condition* cond);

static void
patch_condition_true(struct SSC_Compiler* compiler, struct SSC_Condition const* cond);

/** `a & b & c`: every term must hold, so each failure jumps straight out. */
static int
parse_and_terms(struct SSC_Compiler* compiler, struct SSC_Condition* cond)
{
    for( ;; )
    {
        if( SSC_LexIsPunct(&compiler->lexer, "(") )
        {
            struct SSC_Condition inner;

            SSC_LexNext(&compiler->lexer);
            if( !parse_condition(compiler, &inner) )
                return 0;
            if( !SSC_LexIsPunct(&compiler->lexer, ")") )
                return fail(compiler, "expected ')' to close a grouped condition");
            SSC_LexNext(&compiler->lexer);

            /* The group holding means "carry on with the rest of this & chain",
             * not "enter the block" — so its true branches land here rather
             * than escaping to the caller. Its failures do escape: one failed
             * term fails the whole conjunction. */
            patch_condition_true(compiler, &inner);
            for( int i = 0; i < inner.false_count; i++ )
            {
                if( !cond_push(compiler, cond->false_list, &cond->false_count,
                               inner.false_list[i]) )
                    return 0;
            }
        }
        else
        {
            int branch;

            if( !parse_comparison(compiler, &branch) )
                return 0;
            if( !cond_push(compiler, cond->false_list, &cond->false_count, branch) )
                return 0;
        }

        if( !SSC_LexIsPunct(&compiler->lexer, "&") )
            return 1;
        SSC_LexNext(&compiler->lexer);
    }
}

static int
parse_condition(struct SSC_Compiler* compiler, struct SSC_Condition* cond)
{
    memset(cond, 0, sizeof(*cond));

    for( ;; )
    {
        int before = cond->false_count;

        if( !parse_and_terms(compiler, cond) )
            return 0;

        if( !SSC_LexIsPunct(&compiler->lexer, "|") )
            return 1;
        SSC_LexNext(&compiler->lexer);

        /* This alternative held, so the whole condition holds: jump to the
         * block. The failures it collected only mean "try the next
         * alternative", so they land here rather than escaping. */
        {
            int taken = emit(compiler, SS_OP_BRANCH, 0);

            if( !cond_push(compiler, cond->true_list, &cond->true_count, taken) )
                return 0;
            for( int i = before; i < cond->false_count; i++ )
                patch_to_here(compiler, cond->false_list[i]);
            cond->false_count = before;
        }
    }
}

static void
patch_condition_true(struct SSC_Compiler* compiler, struct SSC_Condition const* cond)
{
    for( int i = 0; i < cond->true_count; i++ )
        patch_to_here(compiler, cond->true_list[i]);
}

static void
patch_condition_false(struct SSC_Compiler* compiler, struct SSC_Condition const* cond)
{
    for( int i = 0; i < cond->false_count; i++ )
        patch_to_here(compiler, cond->false_list[i]);
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

/**
 * The body of an if / else / while.
 *
 * Braces are optional around a single statement — `if (random(4) ! 0) return;`
 * is idiomatic and common in the corpus.
 */
static int
parse_block(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;

    if( !SSC_LexIsPunct(lexer, "{") )
        return parse_statement(compiler);
    SSC_LexNext(lexer);

    while( !SSC_LexIsPunct(lexer, "}") && lexer->current.kind != SSC_TOK_EOF )
    {
        if( !parse_statement(compiler) )
            return 0;
    }
    if( !SSC_LexIsPunct(lexer, "}") )
        return fail(compiler, "expected '}'");
    SSC_LexNext(lexer);
    return 1;
}

static int
parse_if(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    struct SSC_Condition cond;
    int skip_else = -1;

    SSC_LexNext(lexer); /* 'if' */
    if( !SSC_LexIsPunct(lexer, "(") )
        return fail(compiler, "expected '(' after if");
    SSC_LexNext(lexer);

    if( !parse_condition(compiler, &cond) )
        return 0;

    if( !SSC_LexIsPunct(lexer, ")") )
        return fail(compiler, "expected ')' to close the condition");
    SSC_LexNext(lexer);

    patch_condition_true(compiler, &cond);
    if( !parse_block(compiler) )
        return 0;

    if( lexer->current.kind == SSC_TOK_IDENT && strcmp(lexer->current.text, "else") == 0 )
    {
        skip_else = emit(compiler, SS_OP_BRANCH, 0);
        patch_condition_false(compiler, &cond);
        SSC_LexNext(lexer);

        if( lexer->current.kind == SSC_TOK_IDENT && strcmp(lexer->current.text, "if") == 0 )
        {
            if( !parse_if(compiler) )
                return 0;
        }
        else if( !parse_block(compiler) )
        {
            return 0;
        }
        patch_to_here(compiler, skip_else);
    }
    else
    {
        patch_condition_false(compiler, &cond);
    }
    return 1;
}

static int
parse_while(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    struct SSC_Condition cond;
    int loop_top;
    int back;

    SSC_LexNext(lexer); /* 'while' */
    if( !SSC_LexIsPunct(lexer, "(") )
        return fail(compiler, "expected '(' after while");
    SSC_LexNext(lexer);

    loop_top = compiler->build.op_count;
    if( !parse_condition(compiler, &cond) )
        return 0;

    if( !SSC_LexIsPunct(lexer, ")") )
        return fail(compiler, "expected ')' to close the condition");
    SSC_LexNext(lexer);

    patch_condition_true(compiler, &cond);
    if( !parse_block(compiler) )
        return 0;

    back = emit(compiler, SS_OP_BRANCH, 0);
    compiler->build.int_operands[back] = loop_top - back - 1;
    patch_condition_false(compiler, &cond);
    return 1;
}

/** Fold a case label to a constant. Case keys live in the table, not in code. */
static int
parse_case_value(struct SSC_Compiler* compiler, int32_t* out)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    const struct SSC_Symbol* symbol;

    if( lexer->current.kind == SSC_TOK_INT )
    {
        *out = lexer->current.value;
        SSC_LexNext(lexer);
        return 1;
    }
    if( lexer->current.kind == SSC_TOK_CONSTANT )
    {
        symbol = SSC_SymbolsFind(compiler->symbols, lexer->current.text, SSC_SYM_CONSTANT);
        if( !symbol || !symbol->text )
            return fail(compiler, "unknown constant '^%s'", lexer->current.text);
        *out = (int32_t)atoi(symbol->text);
        SSC_LexNext(lexer);
        return 1;
    }
    if( lexer->current.kind == SSC_TOK_IDENT )
    {
        if( strcmp(lexer->current.text, "true") == 0 ||
            strcmp(lexer->current.text, "false") == 0 )
        {
            *out = lexer->current.text[0] == 't' ? 1 : 0;
            SSC_LexNext(lexer);
            return 1;
        }
        symbol = SSC_SymbolsFind(compiler->symbols, lexer->current.text, SSC_SYM_UNKNOWN);
        if( !symbol )
            return fail(compiler, "case value '%s' is not a known symbol", lexer->current.text);
        *out = symbol->value;
        SSC_LexNext(lexer);
        return 1;
    }
    return fail(compiler, "expected a constant case value");
}

static int
parse_switch(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    struct SSC_Build* build = &compiler->build;
    int table_index;
    int switch_index;
    int is_string = 0;
    int default_branch = -1;
    int exits[SSC_MAX_SWITCH_CASES];
    int exit_count = 0;
    int case_count = 0;

    if( build->table_count >= SSC_MAX_SWITCH_TABLES )
        return fail(compiler, "more than %d switch tables in one script",
                    SSC_MAX_SWITCH_TABLES);
    table_index = build->table_count++;

    SSC_LexNext(lexer); /* switch_<type> */
    if( !SSC_LexIsPunct(lexer, "(") )
        return fail(compiler, "expected '(' after switch");
    SSC_LexNext(lexer);

    if( !parse_expression(compiler, &is_string) )
        return 0;
    if( !SSC_LexIsPunct(lexer, ")") )
        return fail(compiler, "expected ')' after the switch subject");
    SSC_LexNext(lexer);

    switch_index = emit(compiler, SS_OP_SWITCH, table_index);
    /* Control falls straight through the SWITCH when no case matched, so the
     * jump to the default arm goes here, before any case body. */
    default_branch = emit(compiler, SS_OP_BRANCH, 0);

    if( !SSC_LexIsPunct(lexer, "{") )
        return fail(compiler, "expected '{' to open the switch");
    SSC_LexNext(lexer);

    while( !SSC_LexIsPunct(lexer, "}") && lexer->current.kind != SSC_TOK_EOF )
    {
        int is_default = 0;
        int32_t keys[SSC_MAX_SWITCH_CASES];
        int key_count = 0;
        int arm;
        int i;

        if( lexer->current.kind != SSC_TOK_IDENT || strcmp(lexer->current.text, "case") != 0 )
            return fail(compiler, "expected 'case' inside the switch");
        SSC_LexNext(lexer);

        if( lexer->current.kind == SSC_TOK_IDENT && strcmp(lexer->current.text, "default") == 0 )
        {
            is_default = 1;
            SSC_LexNext(lexer);
        }
        else
        {
            for( ;; )
            {
                int32_t key;

                if( key_count >= SSC_MAX_SWITCH_CASES )
                    return fail(compiler, "too many case values");
                if( !parse_case_value(compiler, &key) )
                    return 0;
                keys[key_count++] = key;
                if( SSC_LexIsPunct(lexer, "," ) )
                {
                    SSC_LexNext(lexer);
                    continue;
                }
                break;
            }
        }

        if( !SSC_LexIsPunct(lexer, ":") )
            return fail(compiler, "expected ':' after the case value");
        SSC_LexNext(lexer);

        arm = build->op_count;
        if( is_default )
        {
            patch_to_here(compiler, default_branch);
            default_branch = -1;
        }
        else
        {
            for( i = 0; i < key_count; i++ )
            {
                if( case_count >= SSC_MAX_SWITCH_CASES )
                    return fail(compiler, "too many cases in one switch");
                build->cases[table_index][case_count].key = keys[i];
                /* Delta 0 means "no such case" to the VM, so an empty arm
                 * immediately after the SWITCH would vanish. Nothing here can
                 * produce one: the unconditional default branch always sits
                 * between the switch and the first arm. */
                build->cases[table_index][case_count].delta = arm - switch_index - 1;
                case_count++;
            }
        }

        while( !SSC_LexIsPunct(lexer, "}") && lexer->current.kind != SSC_TOK_EOF &&
               !(lexer->current.kind == SSC_TOK_IDENT &&
                 strcmp(lexer->current.text, "case") == 0) )
        {
            if( !parse_statement(compiler) )
                return 0;
        }

        /* Cases do not fall through. */
        if( exit_count < SSC_MAX_SWITCH_CASES )
            exits[exit_count++] = emit(compiler, SS_OP_BRANCH, 0);
    }

    if( !SSC_LexIsPunct(lexer, "}") )
        return fail(compiler, "expected '}' to close the switch");
    SSC_LexNext(lexer);

    if( default_branch >= 0 )
        patch_to_here(compiler, default_branch);
    {
        int i;

        for( i = 0; i < exit_count; i++ )
            patch_to_here(compiler, exits[i]);
    }

    build->tables[table_index].cases = build->cases[table_index];
    build->tables[table_index].case_count = (uint16_t)case_count;
    return 1;
}

static int
parse_statement(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    char text[512];

    snprintf(text, sizeof(text), "%s", lexer->current.text);

    if( lexer->current.kind == SSC_TOK_IDENT )
    {
        if( strcmp(text, "if") == 0 )
            return parse_if(compiler);
        if( strcmp(text, "while") == 0 )
            return parse_while(compiler);
        if( strncmp(text, "switch_", 7) == 0 )
            return parse_switch(compiler);

        if( strcmp(text, "return") == 0 )
        {
            SSC_LexNext(lexer);
            if( SSC_LexIsPunct(lexer, "(") )
            {
                SSC_LexNext(lexer);
                if( !SSC_LexIsPunct(lexer, ")") )
                {
                    for( ;; )
                    {
                        int is_string = 0;

                        if( !parse_expression(compiler, &is_string) )
                            return 0;
                        if( SSC_LexIsPunct(lexer, ",") )
                        {
                            SSC_LexNext(lexer);
                            continue;
                        }
                        break;
                    }
                }
                if( !SSC_LexIsPunct(lexer, ")") )
                    return fail(compiler, "expected ')' after return values");
                SSC_LexNext(lexer);
            }
            emit(compiler, SS_OP_RETURN, 0);
            if( SSC_LexIsPunct(lexer, ";") )
                SSC_LexNext(lexer);
            return 1;
        }

        if( strncmp(text, "def_", 4) == 0 )
        {
            const char* type = text + 4;
            int is_string_type = type_is_string(type);
            char local_name[64];
            int value_is_string = 0;
            struct SSC_Local* local;

            SSC_LexNext(lexer);
            if( lexer->current.kind != SSC_TOK_LOCAL )
                return fail(compiler, "expected a $local after def_%s", type);
            snprintf(local_name, sizeof(local_name), "%.63s", lexer->current.text);
            SSC_LexNext(lexer);

            /* `def_string $name;` with no initialiser just reserves the slot.
             * The VM zeroes locals on entry, so there is nothing to emit. */
            if( !SSC_LexIsPunct(lexer, "=") )
            {
                if( !declare_local(compiler, local_name, is_string_type) )
                    return 0;
                if( SSC_LexIsPunct(lexer, ";") )
                    SSC_LexNext(lexer);
                return 1;
            }
            SSC_LexNext(lexer);

            if( !parse_expression(compiler, &value_is_string) )
                return 0;

            local = declare_local(compiler, local_name, is_string_type);
            if( !local )
                return 0;
            emit(compiler, is_string_type ? SS_OP_POP_STRING_LOCAL : SS_OP_POP_INT_LOCAL,
                 local->slot);

            if( SSC_LexIsPunct(lexer, ";") )
                SSC_LexNext(lexer);
            return 1;
        }

        /* Anything else that starts with a name is a command call. */
        SSC_LexNext(lexer);
        if( !parse_command(compiler, text, NULL) )
            return 0;
        if( SSC_LexIsPunct(lexer, ";") )
            SSC_LexNext(lexer);
        return 1;
    }

    if( lexer->current.kind == SSC_TOK_PROC || lexer->current.kind == SSC_TOK_LABEL )
    {
        int is_label = lexer->current.kind == SSC_TOK_LABEL;

        SSC_LexNext(lexer);
        if( !parse_call(compiler, text, is_label, NULL) )
            return 0;
        if( SSC_LexIsPunct(lexer, ";") )
            SSC_LexNext(lexer);
        return 1;
    }

    if( lexer->current.kind == SSC_TOK_LOCAL )
    {
        /* `$x, $z = ~door_open(...)` — a proc can return several values, and
         * they come off the stack last-first, so the targets are popped in
         * reverse of how they were written. */
        struct SSC_Local* targets[SSC_MAX_ASSIGN_TARGETS];
        int target_count = 0;
        int value_is_string = 0;

        for( ;; )
        {
            struct SSC_Local* local;

            if( lexer->current.kind != SSC_TOK_LOCAL )
                return fail(compiler, "expected a $local in the assignment list");
            local = find_local(compiler, lexer->current.text);
            if( !local )
                return fail(compiler, "'$%s' names no local in scope", lexer->current.text);
            if( target_count >= SSC_MAX_ASSIGN_TARGETS )
                return fail(compiler, "more than %d assignment targets",
                            SSC_MAX_ASSIGN_TARGETS);
            targets[target_count++] = local;
            SSC_LexNext(lexer);

            if( !SSC_LexIsPunct(lexer, ",") )
                break;
            SSC_LexNext(lexer);
        }

        if( !SSC_LexIsPunct(lexer, "=") )
            return fail(compiler, "expected '=' after the assignment target");
        SSC_LexNext(lexer);

        if( !parse_expression(compiler, &value_is_string) )
            return 0;

        for( int i = target_count - 1; i >= 0; i-- )
        {
            emit(compiler,
                 targets[i]->is_string ? SS_OP_POP_STRING_LOCAL : SS_OP_POP_INT_LOCAL,
                 targets[i]->slot);
        }
        if( SSC_LexIsPunct(lexer, ";") )
            SSC_LexNext(lexer);
        return 1;
    }

    if( lexer->current.kind == SSC_TOK_VAR ||
        (SSC_LexIsPunct(lexer, ".") && SSC_LexPeek(lexer)->kind == SSC_TOK_VAR) )
    {
        int value_is_string = 0;
        int push;
        int pop;
        int32_t id;
        int32_t secondary = 0;

        if( SSC_LexIsPunct(lexer, ".") )
        {
            secondary = 1 << 16;
            SSC_LexNext(lexer);
            snprintf(text, sizeof(text), "%s", lexer->current.text);
        }

        if( !resolve_variable(compiler, text, &push, &pop, &id) )
            return fail(compiler, "unknown variable '%%%s'", text);
        if( !check_carrier_write(compiler, text, pop, id) )
            return 0;
        SSC_LexNext(lexer);
        if( !SSC_LexIsPunct(lexer, "=") )
            return fail(compiler, "expected '=' after %%%s", text);
        SSC_LexNext(lexer);

        if( !parse_expression(compiler, &value_is_string) )
            return 0;
        emit(compiler, pop, id | secondary);
        if( SSC_LexIsPunct(lexer, ";") )
            SSC_LexNext(lexer);
        return 1;
    }

    if( SSC_LexIsPunct(lexer, ";") )
    {
        SSC_LexNext(lexer);
        return 1;
    }

    if( SSC_LexIsPunct(lexer, "*") )
        return fail(compiler,
                    "the vararg form (queue*/strongqueue*/weakqueue*/longqueue*) "
                    "is not supported; it packs a type string the compiler does not build");

    return fail(compiler, "unexpected '%s' at the start of a statement", text);
}

/* ------------------------------------------------------------------ */
/* Script headers                                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse `[trigger,subject]` and everything the header implies.
 *
 * Writes the full bracketed name (which is what the container indexes by) and
 * the lookup key, and returns 0 when the line is not a header at all.
 */
static int
parse_header(
    struct SSC_Compiler* compiler,
    char* out_name,
    size_t name_capacity,
    int32_t* out_lookup_key)
{
    struct SSC_Lexer* lexer = &compiler->lexer;
    char trigger_name[64];
    char subject[SSC_MAX_NAME];
    int trigger;
    int subject_is_coord = 0;
    int32_t subject_value = 0;

    if( !SSC_LexIsPunct(lexer, "[") )
        return fail(compiler, "expected '[' to start a script header");
    SSC_LexNext(lexer);

    if( lexer->current.kind != SSC_TOK_IDENT )
        return fail(compiler, "expected a trigger name");
    snprintf(trigger_name, sizeof(trigger_name), "%.63s", lexer->current.text);
    SSC_LexNext(lexer);

    if( !SSC_LexIsPunct(lexer, ",") )
        return fail(compiler, "expected ',' after the trigger name");
    SSC_LexNext(lexer);

    /* mapzone/zone subjects are coords — `[mapzoneexit,0_49_46]` — which lex as
     * a number rather than a name. */
    if( lexer->current.kind == SSC_TOK_IDENT || lexer->current.kind == SSC_TOK_INT )
    {
        snprintf(subject, sizeof(subject), "%.127s", lexer->current.text);
        subject_is_coord = lexer->current.kind == SSC_TOK_INT;
        subject_value = lexer->current.value;
    }
    else if( SSC_LexIsPunct(lexer, "_") )
    {
        snprintf(subject, sizeof(subject), "_");
    }
    else
    {
        return fail(compiler, "expected a subject after the trigger");
    }
    SSC_LexNext(lexer);

    /* `[command,queue*]` declares the vararg form; the star is part of the
     * declaration's name, not syntax we need past this point. */
    if( SSC_LexIsPunct(lexer, "*") )
        SSC_LexNext(lexer);

    if( !SSC_LexIsPunct(lexer, "]") )
        return fail(compiler, "expected ']' to close the script header");
    SSC_LexNext(lexer);

    /* engine.rs2 declares the language's commands with `[command,name](sig)`.
     * Those are signatures, not scripts — the generated opcode table already
     * carries them — so the file is walked and skipped rather than compiled. */
    if( strcmp(trigger_name, "command") == 0 )
    {
        snprintf(out_name, name_capacity, "[command,%.115s]", subject);
        *out_lookup_key = -1;
        return 2;
    }

    trigger = SSVM_TriggerFromName(trigger_name);
    if( trigger < 0 )
        return fail(compiler, "unknown trigger '%s'", trigger_name);

    snprintf(out_name, name_capacity, "[%.32s,%.90s]", trigger_name, subject);

    /* Name-addressed triggers carry -1: they are reached by script id from a
     * gosub or a queue, never looked up by trigger. */
    if( trigger == SS_TRIGGER_PROC || trigger == SS_TRIGGER_LABEL ||
        trigger == SS_TRIGGER_DEBUGPROC || trigger == SS_TRIGGER_QUEUE ||
        trigger == SS_TRIGGER_TIMER || trigger == SS_TRIGGER_SOFTTIMER ||
        trigger == SS_TRIGGER_WALKTRIGGER )
    {
        *out_lookup_key = -1;
    }
    else if( strcmp(subject, "_") == 0 )
    {
        *out_lookup_key = (int32_t)SSVM_LookupKey(trigger, SS_LOOKUP_GLOBAL, 0);
    }
    else if( subject_is_coord )
    {
        /*
         * A coord subject is **name-addressed**, like a proc: -1, so it is not
         * in the key index at all.
         *
         * That is by construction rather than by taste, and the arithmetic is
         * why. `zone`/`zoneexit` subjects are five-part and `ssc_lex.c` packs
         * them into 28 bits (`(level << 28) | ((mx * 64 + lx) << 14) | …`);
         * `SSVM_LookupKey` puts the subject at bit 10 and the compiled field is
         * an i32, so the pack's top 21 bits are simply lost.
         *
         * Measured over the reference's 427 five-part headers rather than
         * argued: **78 truncate to a negative key**, which `ssvm_provider.c`
         * deliberately keeps out of `by_key` — `[zone,0_50_50_16_16]` packs to
         * 52,694,160 and keys to -1,875,754,333. The other **349 stay
         * non-negative** and land on a subject field that is not the coord under
         * any reading, **10 of them colliding** with a neighbour — which is the
         * `dup zone x10` line `test-ss-corpus` prints. The sign turns on bit 1
         * of `mx`, so which half a zone falls in is an accident of where it is.
         *
         * `mapzone`/`mapzoneexit` are three-part and lex to their first
         * component, which for all 379 of them is 0 — the reference's own
         * `parseInt("0_49_46")` collapse. One key, 379 scripts.
         *
         * Neither shape is addressable by key, which is why the engine formats
         * the header's own text and asks `getByName`
         * (`mock230_scripts_run_trigger_at`, `NetworkPlayer.updateMap`). Writing
         * a key nothing can look up was harmless only while nothing dispatched
         * these triggers; that stopped being true with triage §9 step 5c.
         *
         * `test-ss-corpus` is unaffected: it loads the reference's *own*
         * compiled pack, so its duplicate counts describe the reference's
         * compiler rather than this one.
         */
        (void)subject_value;
        *out_lookup_key = -1;
    }
    else
    {
        /* A subject spelled `_name` is a CATEGORY, not a type — the underscore
         * is the source syntax for it, and 310 scripts use the form. The
         * compiled name keeps the underscore (`[oploc1,_outpost_gate]`), so
         * only the lookup strips it. */
        int is_category = subject[0] == '_' && subject[1] != '\0';
        const struct SSC_Symbol* symbol = NULL;

        /*
         * Resolve the subject in the namespace the trigger implies, before
         * falling back to "any".
         *
         * A name can exist in more than one namespace, and an unqualified
         * lookup takes whichever kind sorts first. `grim_pendant` is obj 11197
         * AND loc 24780, so `[oploc1,grim_pendant]` resolved to the OBJ and
         * filed Grim Tales' pendant under an unrelated loc id, where the
         * walkthrough fallback owned the key and won. The pendant did nothing,
         * and nothing said why.
         *
         * The fallback is deliberate: only loc/npc/obj are mapped here, so a
         * trigger family this does not know about resolves exactly as before.
         */
        if( !is_category )
        {
            enum SSC_SymbolKind want = SSC_SYM_UNKNOWN;

            if( strncmp(trigger_name, "ai_", 3) == 0 )
                want = SSC_SYM_NPC; /* the npc running the AI, whatever it acts on */
            else if( strncmp(trigger_name, "oploc", 5) == 0 ||
                     strncmp(trigger_name, "aplloc", 6) == 0 ||
                     strncmp(trigger_name, "opheldloc", 9) == 0 ||
                     strcmp(trigger_name, "locstep") == 0 )
                want = SSC_SYM_LOC;
            else if( strncmp(trigger_name, "opnpc", 5) == 0 ||
                     strncmp(trigger_name, "apnpc", 5) == 0 )
                want = SSC_SYM_NPC;
            else if( strncmp(trigger_name, "opheld", 6) == 0 ||
                     strncmp(trigger_name, "opobj", 5) == 0 ||
                     strncmp(trigger_name, "apobj", 5) == 0 )
                want = SSC_SYM_OBJ;

            /* `opheldt`/`opnpct` and friends take an interface COMPONENT
             * (`magic_spellbook:magic_dart`), not an obj or npc — the colon is
             * what says so, and every one of them in this tree has it. */
            if( want != SSC_SYM_UNKNOWN && !strchr(subject, ':') )
            {
                symbol = SSC_SymbolsFind(compiler->symbols, subject, want);
                if( !symbol && SSC_SymbolsFind(compiler->symbols, subject, SSC_SYM_UNKNOWN) )
                {
                    /*
                     * The name exists, but not as the kind this trigger acts
                     * on. Falling back would compile it against the wrong
                     * namespace's id and register the body somewhere unrelated
                     * — which is how `[opheldu,mdaughter_cliff_boulder]` (a
                     * loc, written as an item-on-item) meant that using the
                     * rope on the boulder did nothing, 30 times over. There is
                     * no reading of that which is correct, so it is an error
                     * rather than a fallback.
                     */
                    return fail(compiler,
                                "subject '%s' of trigger '%s' is not a %s; "
                                "use the trigger that matches what it is",
                                subject, trigger_name,
                                want == SSC_SYM_LOC   ? "loc"
                                : want == SSC_SYM_NPC ? "npc"
                                                      : "obj");
                }
            }
        }
        if( !symbol )
            symbol = SSC_SymbolsFind(
                compiler->symbols,
                is_category ? subject + 1 : subject,
                is_category ? SSC_SYM_CATEGORY : SSC_SYM_UNKNOWN);

        if( !symbol )
            return fail(compiler, "unknown %s subject '%s' for trigger '%s'",
                        is_category ? "category" : "type", subject, trigger_name);

        /*
         * The on-disk key is an i32 with the subject at bit 10, so anything at
         * or above 2^21 cannot be represented.
         *
         * That is a real limit of LostCity's format and it is not widened here:
         * the format is the reference's, `test-ss-roundtrip` proves this
         * compiler reproduces it byte for byte, and a header change would
         * forfeit that for one trigger family.
         *
         * A rev-230 interface component is `(interface << 16) | child`, so
         * every component above interface 31 overflows — `orbs:runbutton` is
         * 10,485,788. Those compile **name-addressed** instead: lookup_key -1
         * puts the script in the by-name table under its full bracket name,
         * and the engine resolves the component's name from the same pack the
         * compiler read it from. Nothing is packed, so nothing can collide.
         *
         * A negative id is still an error — that is a broken symbol, not a
         * wide one.
         */
        if( symbol->value < 0 )
            return fail(compiler, "subject '%s' resolved to a negative id (%d)", subject,
                        symbol->value);
        if( symbol->value >= (1 << 21) )
        {
            *out_lookup_key = -1;
            return 1;
        }

        *out_lookup_key = (int32_t)SSVM_LookupKey(
            trigger,
            (is_category || symbol->kind == SSC_SYM_CATEGORY) ? SS_LOOKUP_CATEGORY
                                                              : SS_LOOKUP_TYPE,
            symbol->value);
    }
    return 1;
}

/** Parse `(type $name, ...)` argument and `(type, ...)` return lists. */
static int
parse_header_lists(struct SSC_Compiler* compiler)
{
    struct SSC_Lexer* lexer = &compiler->lexer;

    if( !SSC_LexIsPunct(lexer, "(") )
        return 1; /* no arguments */
    SSC_LexNext(lexer);

    while( !SSC_LexIsPunct(lexer, ")") && lexer->current.kind != SSC_TOK_EOF )
    {
        char type[64];
        int is_string;

        if( lexer->current.kind != SSC_TOK_IDENT )
            return fail(compiler, "expected an argument type");
        snprintf(type, sizeof(type), "%.63s", lexer->current.text);
        is_string = type_is_string(type);
        SSC_LexNext(lexer);

        if( lexer->current.kind != SSC_TOK_LOCAL )
            return fail(compiler, "expected a $name after the argument type");
        if( !declare_local(compiler, lexer->current.text, is_string) )
            return 0;
        if( is_string )
            compiler->build.string_args++;
        else
            compiler->build.int_args++;
        /* Keep the type as well as the stack it lives on — see param_types.
         * A type the symbol table does not know is `int`, which is what the
         * stack answer already said it was. */
        if( compiler->build.param_type_count < SS_MAX_PARAM_TYPES )
        {
            const struct SSC_Symbol* declared =
                SSC_SymbolsFind(compiler->symbols, type, SSC_SYM_TYPE);

            compiler->build.param_types[compiler->build.param_type_count++] =
                (uint8_t)(declared ? declared->value : 105 /* int */);
        }
        SSC_LexNext(lexer);

        if( SSC_LexIsPunct(lexer, ",") )
            SSC_LexNext(lexer);
    }
    if( !SSC_LexIsPunct(lexer, ")") )
        return fail(compiler, "expected ')' to close the argument list");
    SSC_LexNext(lexer);

    /* The return list is parsed and discarded: return arity is not stored in
     * the format at all — a caller simply reads whatever RETURN left behind. */
    if( SSC_LexIsPunct(lexer, "(") )
    {
        SSC_LexNext(lexer);
        while( !SSC_LexIsPunct(lexer, ")") && lexer->current.kind != SSC_TOK_EOF )
            SSC_LexNext(lexer);
        if( SSC_LexIsPunct(lexer, ")") )
            SSC_LexNext(lexer);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Compiler lifecycle                                                  */
/* ------------------------------------------------------------------ */

struct SSC_Compiler*
SSC_New(struct SSC_Symbols* symbols)
{
    struct SSC_Compiler* compiler = (struct SSC_Compiler*)calloc(1, sizeof(*compiler));

    assert(compiler);
    compiler->symbols = symbols;
    compiler->scripts =
        (struct SSVM_Script*)calloc(SSC_MAX_SCRIPTS, sizeof(struct SSVM_Script));
    compiler->names = (char(*)[SSC_MAX_NAME])calloc(SSC_MAX_SCRIPTS, SSC_MAX_NAME);
    compiler->name_int_args = (int8_t*)malloc(SSC_MAX_SCRIPTS);
    compiler->name_str_args = (int8_t*)malloc(SSC_MAX_SCRIPTS);
    compiler->name_str_return = (int8_t*)malloc(SSC_MAX_SCRIPTS);
    compiler->name_int_returns = (int8_t*)malloc(SSC_MAX_SCRIPTS);
    compiler->name_str_returns = (int8_t*)malloc(SSC_MAX_SCRIPTS);
    /* Zeroed, i.e. SSC_SYM_UNKNOWN: a script whose header the declare pass
     * never sees hints nothing, which is how every call site behaved before. */
    compiler->name_param_kinds = (uint8_t(*)[SS_MAX_PARAM_TYPES])calloc(
        SSC_MAX_SCRIPTS, SS_MAX_PARAM_TYPES);
    compiler->name_capacity = SSC_MAX_SCRIPTS;
    if( compiler->name_int_args && compiler->name_str_args && compiler->name_str_return &&
        compiler->name_int_returns && compiler->name_str_returns )
    {
        memset(compiler->name_int_args, -1, SSC_MAX_SCRIPTS);
        memset(compiler->name_str_args, -1, SSC_MAX_SCRIPTS);
        memset(compiler->name_str_return, -1, SSC_MAX_SCRIPTS);
        memset(compiler->name_int_returns, -1, SSC_MAX_SCRIPTS);
        memset(compiler->name_str_returns, -1, SSC_MAX_SCRIPTS);
    }
    if( !compiler->scripts || !compiler->names || !compiler->name_int_args ||
        !compiler->name_str_args || !compiler->name_str_return ||
        !compiler->name_int_returns || !compiler->name_str_returns ||
        !compiler->name_param_kinds )
    {
        SSC_Free(compiler);
        return NULL;
    }
    return compiler;
}

void
SSC_Free(struct SSC_Compiler* compiler)
{
    int i;

    if( !compiler )
        return;
    for( i = 0; i < compiler->script_count; i++ )
        SSVM_ScriptFree(&compiler->scripts[i]);
    free(compiler->scripts);
    free(compiler->names);
    free(compiler->name_int_args);
    free(compiler->name_str_args);
    free(compiler->name_str_return);
    free(compiler->name_int_returns);
    free(compiler->name_str_returns);
    free(compiler->name_param_kinds);
    free(compiler->shadowed);
    free(compiler);
}

int
SSC_ScriptCount(const struct SSC_Compiler* compiler)
{
    return compiler ? compiler->script_count : 0;
}

const struct SSVM_Script*
SSC_ScriptAt(
    const struct SSC_Compiler* compiler,
    int index)
{
    assert(compiler);
    if( index < 0 || index >= compiler->script_count )
        return NULL;
    return &compiler->scripts[index];
}

/* ------------------------------------------------------------------ */
/* File handling                                                       */
/* ------------------------------------------------------------------ */

static char*
read_file(const char* path, size_t* out_length)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* data;

    *out_length = 0;
    if( !file )
        return NULL;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }

    data = (char*)malloc((size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    *out_length = (size_t)size;
    return data;
}

int
SSC_Declare(
    struct SSC_Compiler* compiler,
    const char* path,
    struct SSC_Diag* diag)
{
    size_t length = 0;
    char* source = read_file(path, &length);
    struct SSC_Lexer lexer;

    if( !source )
    {
        if( diag )
            snprintf(diag->message, sizeof(diag->message), "cannot read %s", path);
        return 0;
    }

    /* Names only. A file may call a proc declared in a file compiled later, so
     * every name has to exist before any code is emitted. */
    SSC_LexInit(&lexer, source, length, path);
    SSC_LexNext(&lexer);
    while( lexer.current.kind != SSC_TOK_EOF )
    {
        if( SSC_LexIsPunct(&lexer, "[") )
        {
            char trigger[64];
            char subject[SSC_MAX_NAME];

            SSC_LexNext(&lexer);
            if( lexer.current.kind != SSC_TOK_IDENT )
                continue;
            snprintf(trigger, sizeof(trigger), "%.63s", lexer.current.text);
            SSC_LexNext(&lexer);
            if( !SSC_LexIsPunct(&lexer, ",") )
                continue;
            SSC_LexNext(&lexer);
            snprintf(subject, sizeof(subject), "%.127s", lexer.current.text);
            SSC_LexNext(&lexer);

            /* Command declarations are not scripts and must not take ids. */
            if( strcmp(trigger, "command") == 0 )
                continue;

            /*
             * A seam a lane took over. The lane's declaration already holds the
             * name and this one must not become a second id — that is precisely
             * the duplicate the check below refuses. Recorded rather than merely
             * skipped, because the emit pass has to drop this body too: it would
             * otherwise resolve the same name back to the lane's id and write
             * the default over the lane's real script.
             *
             * Only against the strong roots. Two seams sharing a name, or two
             * lanes, is the ordinary duplicate and is still an error.
             */
            if( compiler->weak_source )
            {
                char seam_name[SSC_MAX_NAME];
                int existing;

                snprintf(seam_name, sizeof(seam_name), "[%.32s,%.90s]", trigger, subject);
                existing = script_id_for_name(compiler, seam_name);
                if( existing >= 0 && existing < compiler->strong_name_count )
                {
                    shadow_add(compiler, seam_name);
                    continue;
                }
            }

            /*
             * Some script names are singleton entry points. Two declarations
             * cannot be composed and there is no meaningful precedence: the
             * old compiler assigned both declarations a slot and finish_script
             * then resolved both bodies back to the first matching name,
             * silently replacing whichever body compiled first.
             *
             * That behavior first hid a second [debugproc,crystal_set]. It also
             * let quest NPC bootstraps replace [login,_], skipping the canonical
             * login's IF_SETEVENTS burst and making every backpack item inert.
             * Reject both singleton families during the declaration pass so a
             * source-order winner can never reach a script pack.
             */
            if( strcmp(trigger, "debugproc") == 0 ||
                (strcmp(trigger, "login") == 0 && strcmp(subject, "_") == 0) )
            {
                char full_name[SSC_MAX_NAME];

                snprintf(full_name, sizeof(full_name), "[%.32s,%.90s]", trigger, subject);
                if( script_id_for_name(compiler, full_name) >= 0 )
                {
                    if( diag )
                    {
                        snprintf(diag->file, sizeof(diag->file), "%s", path);
                        diag->line = lexer.current.line;
                        if( strcmp(trigger, "debugproc") == 0 )
                            snprintf(
                                diag->message,
                                sizeof(diag->message),
                                "duplicate global debug command '%s'; keep exactly one declaration",
                                full_name);
                        else
                            snprintf(
                                diag->message,
                                sizeof(diag->message),
                                "duplicate global login trigger '%s'; keep one canonical declaration and call procedures from it",
                                full_name);
                    }
                    free(source);
                    return 0;
                }
            }

            if( compiler->name_count < compiler->name_capacity )
            {
                int slot = compiler->name_count;

                snprintf(compiler->names[slot], SSC_MAX_NAME, "[%.32s,%.90s]", trigger, subject);
                compiler->name_count++;

                /*
                 * A name declared twice does not compose and has no precedence
                 * rule: both declarations take an id, but finish_script resolves
                 * every body back to the FIRST matching name, so the file the
                 * compiler happens to reach LAST silently replaces the other's
                 * body and the loser's id is left empty. That is how Sheep
                 * Shearer, Rune Mysteries and A Tail of Two Cats all became
                 * unstartable — no diagnostic, correct-looking source, dead
                 * content.
                 *
                 * A hard error, like the singleton families above (debugproc,
                 * [login,_]): the tree carried a backlog of 67 of these and now
                 * carries none, so the only thing a duplicate can be from here
                 * is a regression. Two files that both need one npc or loc share
                 * it the way areas/lumbridge/scripts/fred_the_farmer.rs2 and
                 * quest_coldwar do — one trigger, branching into a `[label,...]`
                 * the other file owns.
                 */
                if( script_id_for_name(compiler, compiler->names[slot]) != slot )
                {
                    if( diag )
                    {
                        snprintf(diag->file, sizeof(diag->file), "%s", path);
                        diag->line = lexer.current.line;
                        snprintf(diag->message, sizeof(diag->message),
                                 "duplicate script name '%s'; declare it once and branch "
                                 "into a [label,...] from the other file",
                                 compiler->names[slot]);
                    }
                    free(source);
                    return 0;
                }

                /*
                 * The argument list, counted the same way parse_arg_list does.
                 *
                 * `lexer.current` is the `]` here; a header with arguments has
                 * `(type $name, ...)` immediately after it. Only the types are
                 * needed — enough to check a call site pushed the right number
                 * onto each of the two stacks — so this reads them and leaves
                 * the real parse to the emit pass.
                 */
                if( SSC_LexIsPunct(&lexer, "]") )
                {
                    SSC_LexNext(&lexer);
                    if( SSC_LexIsPunct(&lexer, "(") )
                    {
                        int ints = 0;
                        int strs = 0;
                        int params = 0;

                        SSC_LexNext(&lexer);
                        while( !SSC_LexIsPunct(&lexer, ")") &&
                               lexer.current.kind != SSC_TOK_EOF )
                        {
                            if( lexer.current.kind == SSC_TOK_IDENT )
                            {
                                if( type_is_string(lexer.current.text) )
                                    strs++;
                                else
                                    ints++;
                                /* The type is also what a bare argument at that
                                 * position should resolve as — see
                                 * name_param_kinds. Read here rather than in
                                 * parse_header_lists because a call site can be
                                 * compiled before its callee's body. */
                                if( params < SS_MAX_PARAM_TYPES )
                                    compiler->name_param_kinds[slot][params++] =
                                        (uint8_t)param_type_kind(lexer.current.text);
                            }
                            SSC_LexNext(&lexer); /* the type */
                            if( lexer.current.kind == SSC_TOK_LOCAL )
                                SSC_LexNext(&lexer); /* the $name */
                            if( SSC_LexIsPunct(&lexer, ",") )
                                SSC_LexNext(&lexer);
                        }
                        compiler->name_int_args[slot] = (int8_t)ints;
                        compiler->name_str_args[slot] = (int8_t)strs;
                        /* Past the `)`, so the return list below is looked for
                         * where it actually starts. */
                        if( SSC_LexIsPunct(&lexer, ")") )
                            SSC_LexNext(&lexer);
                    }
                    else
                    {
                        compiler->name_int_args[slot] = 0;
                        compiler->name_str_args[slot] = 0;
                    }
                    /*
                     * The return list, which follows the argument list:
                     * `[proc,stat_name](stat $stat)(string)`. Two things are
                     * recorded — which stack a single answer landed on
                     * (name_str_return) and how many values land on each
                     * (name_int_returns / name_str_returns), which is what lets
                     * a multi-return call be passed straight into another call's
                     * argument list.
                     */
                    compiler->name_str_return[slot] = 0;
                    compiler->name_int_returns[slot] = 0;
                    compiler->name_str_returns[slot] = 0;
                    if( SSC_LexIsPunct(&lexer, "(") )
                    {
                        int returns = 0;
                        int strings = 0;

                        SSC_LexNext(&lexer);
                        while( !SSC_LexIsPunct(&lexer, ")") &&
                               lexer.current.kind != SSC_TOK_EOF )
                        {
                            if( lexer.current.kind == SSC_TOK_IDENT )
                            {
                                returns++;
                                if( type_is_string(lexer.current.text) )
                                    strings++;
                            }
                            SSC_LexNext(&lexer);
                            if( SSC_LexIsPunct(&lexer, ",") )
                                SSC_LexNext(&lexer);
                        }
                        compiler->name_str_return[slot] =
                            (int8_t)(returns == 1 && strings == 1);
                        compiler->name_int_returns[slot] = (int8_t)(returns - strings);
                        compiler->name_str_returns[slot] = (int8_t)strings;
                    }
                    continue;
                }
            }
            else
            {
                /* Never drop a name quietly: the symptom is a later file
                 * failing to resolve a proc that visibly exists, which reads as
                 * a parser bug rather than a capacity one. */
                free(source);
                return fail(compiler, "more than %d scripts; raise SSC_MAX_SCRIPTS",
                            SSC_MAX_SCRIPTS);
            }
        }
        SSC_LexNext(&lexer);
    }

    free(source);
    return 1;
}

/** Move the finished build into a script slot. */
static int
finish_script(struct SSC_Compiler* compiler)
{
    struct SSC_Build* build = &compiler->build;
    struct SSVM_Script* script;
    int index;
    int i;

    /* Every script must end in RETURN: the VM errors when pc runs past the last
     * instruction, so a script without one could never complete. Content is not
     * required to write it.
     *
     * Unconditionally, and that is the whole point. This used to skip the
     * append when the last opcode already WAS a return — which is exactly
     * wrong for the commonest shape in the tree:
     *
     *     [proc,clear_desertheat_timer]
     *     if (<cond>) {
     *         ...
     *         return;
     *     }
     *
     * `parse_if` branches over the body to `op_count`, the body's own last
     * opcode is the RETURN, so the append was skipped and the not-taken branch
     * jumped one past the end. The script worked whenever the condition held
     * and aborted with "ran past the last instruction without a return"
     * whenever it did not — which for that proc meant every player leaving a
     * desert map square with the heat timer already off.
     *
     * The cost of always appending is one unreachable byte per script that did
     * not need it. */
    emit(compiler, SS_OP_RETURN, 0);

    index = script_id_for_name(compiler, build->name);
    if( index < 0 )
        return fail(compiler, "script '%s' was never declared", build->name);
    if( index >= SSC_MAX_SCRIPTS )
        return fail(compiler, "too many scripts");

    script = &compiler->scripts[index];
    SSVM_ScriptFree(script);

    script->id = index;
    script->name = strdup(build->name);
    script->source_path = strdup(compiler->source_path);
    script->lookup_key = build->lookup_key;
    script->int_local_count = (uint16_t)build->int_locals;
    script->string_local_count = (uint16_t)build->string_locals;
    script->int_arg_count = (uint16_t)build->int_args;
    script->string_arg_count = (uint16_t)build->string_args;
    script->param_type_count = (uint8_t)build->param_type_count;
    for( i = 0; i < build->param_type_count; i++ )
        script->param_types[i] = build->param_types[i];

    script->op_count = build->op_count;
    script->opcodes = (uint16_t*)calloc((size_t)build->op_count, sizeof(uint16_t));
    script->int_operands = (int32_t*)calloc((size_t)build->op_count, sizeof(int32_t));
    script->string_operands = (char**)calloc((size_t)build->op_count, sizeof(char*));
    memcpy(script->opcodes, build->opcodes, (size_t)build->op_count * sizeof(uint16_t));
    memcpy(script->int_operands, build->int_operands,
           (size_t)build->op_count * sizeof(int32_t));
    for( i = 0; i < build->op_count; i++ )
        script->string_operands[i] = build->string_operands[i];

    script->switch_table_count = (uint8_t)build->table_count;
    if( build->table_count )
    {
        script->switch_tables = (struct SSVM_SwitchTable*)calloc(
            (size_t)build->table_count, sizeof(struct SSVM_SwitchTable));
        for( i = 0; i < build->table_count; i++ )
        {
            uint16_t count = build->tables[i].case_count;

            script->switch_tables[i].case_count = count;
            script->switch_tables[i].cases =
                (struct SSVM_SwitchCase*)calloc(count ? count : 1, sizeof(struct SSVM_SwitchCase));
            memcpy(script->switch_tables[i].cases, build->cases[i],
                   (size_t)count * sizeof(struct SSVM_SwitchCase));
        }
    }

    script->line_count = (uint16_t)build->line_count;
    if( build->line_count )
    {
        script->line_pcs = (int32_t*)calloc((size_t)build->line_count, sizeof(int32_t));
        script->line_numbers = (int32_t*)calloc((size_t)build->line_count, sizeof(int32_t));
        memcpy(script->line_pcs, build->line_pcs, (size_t)build->line_count * sizeof(int32_t));
        memcpy(script->line_numbers, build->line_numbers,
               (size_t)build->line_count * sizeof(int32_t));
    }

    if( index >= compiler->script_count )
        compiler->script_count = index + 1;
    return 1;
}

/**
 * Give an already-finished script a second name.
 *
 * Content stacks headers on one body all over this tree —
 *
 *     [oploc1,hunting_sapling_full_green]
 *     [oploc1,hunting_sapling_full_orange]
 *     [oploc1,hunting_sapling_full_red]
 *     ~hunter_net_take;
 *
 * — 1,287 times, meaning every leaf/variant/colour of a thing shares one
 * handler. The compile loop reads a header, parses statements until the next
 * `[`, and finishes; a stacked header therefore finished an EMPTY body, so
 * only the LAST name in each stack got the code and every earlier one compiled
 * to a bare RETURN. Nothing said so: the ids existed, the pack loaded, and the
 * loc/npc simply did nothing when clicked. Aliasing here is what the content
 * has always been written against.
 *
 * A deep copy rather than a second `finish_script`: that function hands the
 * build's `string_operands` pointers to the script it fills rather than
 * copying them, so running it twice over one build would put the same `char*`
 * in two scripts and free it twice.
 */
static int
alias_script(struct SSC_Compiler* compiler, int src_index, const char* name, int32_t lookup_key)
{
    const struct SSVM_Script* src;
    struct SSVM_Script* dst;
    int index;
    int i;

    index = script_id_for_name(compiler, name);
    if( index < 0 )
        return fail(compiler, "script '%s' was never declared", name);
    if( index >= SSC_MAX_SCRIPTS )
        return fail(compiler, "too many scripts");
    if( index == src_index )
        return 1;

    src = &compiler->scripts[src_index];
    dst = &compiler->scripts[index];
    SSVM_ScriptFree(dst);

    *dst = *src;
    dst->id = index;
    dst->name = strdup(name);
    dst->source_path = strdup(src->source_path ? src->source_path : "");
    /* Its own trigger, not the body's — that is the whole point of the alias. */
    dst->lookup_key = lookup_key;

    dst->opcodes = (uint16_t*)calloc((size_t)src->op_count, sizeof(uint16_t));
    dst->int_operands = (int32_t*)calloc((size_t)src->op_count, sizeof(int32_t));
    dst->string_operands = (char**)calloc((size_t)src->op_count, sizeof(char*));
    memcpy(dst->opcodes, src->opcodes, (size_t)src->op_count * sizeof(uint16_t));
    memcpy(dst->int_operands, src->int_operands, (size_t)src->op_count * sizeof(int32_t));
    for( i = 0; i < src->op_count; i++ )
        dst->string_operands[i] = src->string_operands[i] ? strdup(src->string_operands[i]) : NULL;

    if( src->switch_table_count )
    {
        dst->switch_tables = (struct SSVM_SwitchTable*)calloc(
            (size_t)src->switch_table_count, sizeof(struct SSVM_SwitchTable));
        for( i = 0; i < src->switch_table_count; i++ )
        {
            uint16_t count = src->switch_tables[i].case_count;

            dst->switch_tables[i].case_count = count;
            dst->switch_tables[i].cases =
                (struct SSVM_SwitchCase*)calloc(count ? count : 1, sizeof(struct SSVM_SwitchCase));
            memcpy(dst->switch_tables[i].cases, src->switch_tables[i].cases,
                   (size_t)count * sizeof(struct SSVM_SwitchCase));
        }
    }
    else
    {
        dst->switch_tables = NULL;
    }

    if( src->line_count )
    {
        dst->line_pcs = (int32_t*)calloc((size_t)src->line_count, sizeof(int32_t));
        dst->line_numbers = (int32_t*)calloc((size_t)src->line_count, sizeof(int32_t));
        memcpy(dst->line_pcs, src->line_pcs, (size_t)src->line_count * sizeof(int32_t));
        memcpy(dst->line_numbers, src->line_numbers,
               (size_t)src->line_count * sizeof(int32_t));
    }
    else
    {
        dst->line_pcs = NULL;
        dst->line_numbers = NULL;
    }

    if( index >= compiler->script_count )
        compiler->script_count = index + 1;
    return 1;
}

int
SSC_CompileFile(
    struct SSC_Compiler* compiler,
    const char* path,
    struct SSC_Diag* diag)
{
    size_t length = 0;
    char* source = read_file(path, &length);
    /* Headers stacked on the body that follows them. See alias_script. A stack
     * is bounded by the file, so a fixed cap only has to exceed the longest one
     * content actually writes by enough margin to not be a limit anyone meets.
     *
     * Was 64, chosen against a longest stack of 23
     * (skill_farming/scripts/farming_compost.rs2). Raised to 256 when Barrows
     * equipment landed: gear/barrows_ops.rs2 binds Wear and Check across six
     * sets x four pieces x five degrade tiers, which is ~120 headers on each of
     * its two bodies — a legitimate shape, and the first content to pass 64.
     *
     * Nothing downstream cares how many there are. These two arrays live only
     * for the span between a run of headers and the body they alias onto, and
     * `alias_script` registers each name against the same `body_index`; the
     * bytecode has one script either way. So this is a buffer, not a format
     * limit, and the alternative — splitting a content file into four
     * near-identical bodies to fit the compiler — would be the tail wagging the
     * dog. At SSC_MAX_NAME (128) this is 32 KB of frame, which is why it is not
     * simply enormous. */
    char pending_names[256][SSC_MAX_NAME];
    int32_t pending_keys[256];
    int pending_count = 0;

    if( !source )
    {
        if( diag )
            snprintf(diag->message, sizeof(diag->message), "cannot read %s", path);
        return 0;
    }

    compiler->diag = diag;
    compiler->failed = 0;
    snprintf(compiler->source_path, sizeof(compiler->source_path), "%s", path);

    SSC_LexInit(&compiler->lexer, source, length, path);
    SSC_LexNext(&compiler->lexer);

    while( compiler->lexer.current.kind != SSC_TOK_EOF && !compiler->failed )
    {
        memset(&compiler->build, 0, sizeof(compiler->build));
        compiler->build.last_line = -1;

        {
            int header = parse_header(compiler, compiler->build.name,
                                      sizeof(compiler->build.name),
                                      &compiler->build.lookup_key);

            if( !header )
                break;
            if( header == 2 )
            {
                /* A command declaration: step over its signature and move on. */
                while( compiler->lexer.current.kind != SSC_TOK_EOF &&
                       !SSC_LexIsPunct(&compiler->lexer, "[") )
                    SSC_LexNext(&compiler->lexer);
                continue;
            }
        }
        if( !parse_header_lists(compiler) )
            break;

        /* A script body may be wrapped in braces — `[label,foo](int $x) { ... }`
         * — or run bare until the next header. Both forms appear in the same
         * file, so the shape is decided per script rather than per file. */
        if( SSC_LexIsPunct(&compiler->lexer, "{") )
        {
            SSC_LexNext(&compiler->lexer);
            while( compiler->lexer.current.kind != SSC_TOK_EOF &&
                   !SSC_LexIsPunct(&compiler->lexer, "}") && !compiler->failed )
            {
                if( !parse_statement(compiler) )
                    break;
            }
            if( !compiler->failed && SSC_LexIsPunct(&compiler->lexer, "}") )
                SSC_LexNext(&compiler->lexer);
        }
        else
        {
            while( compiler->lexer.current.kind != SSC_TOK_EOF &&
                   !SSC_LexIsPunct(&compiler->lexer, "[") && !compiler->failed )
            {
                if( !parse_statement(compiler) )
                    break;
            }
        }
        if( compiler->failed )
            break;

        /*
         * An empty body immediately followed by another header is a stacked
         * header, not a script that does nothing: hold its name and give it the
         * next real body. `[` is the only thing that can follow — the loop above
         * stops at nothing else — so reaching EOF with an empty body means a
         * trailing header with no body at all, which stays empty.
         */
        if( compiler->build.op_count == 0 && SSC_LexIsPunct(&compiler->lexer, "[") )
        {
            if( pending_count >= (int)(sizeof(pending_keys) / sizeof(pending_keys[0])) )
            {
                fail(compiler, "more than %d headers stacked on one body",
                     (int)(sizeof(pending_keys) / sizeof(pending_keys[0])));
                break;
            }
            snprintf(pending_names[pending_count], SSC_MAX_NAME, "%s", compiler->build.name);
            pending_keys[pending_count] = compiler->build.lookup_key;
            pending_count++;
            continue;
        }

        /*
         * The other half of the seam override (see SSC_Declare): this name
         * belongs to a lane in this build, so the default body is discarded
         * rather than finished. finish_script resolves by name, and the name now
         * answers with the lane's id — writing there would replace the lane's
         * script with the stub, which is the failure this whole mechanism
         * exists to avoid, and it would do it silently.
         *
         * A shadowed seam cannot carry stacked headers with it: those names
         * would alias onto a body that is not being emitted. Say so rather than
         * dropping them.
         */
        if( compiler->weak_source && is_shadowed(compiler, compiler->build.name) )
        {
            int i;

            if( pending_count )
            {
                fail(compiler,
                     "lane seam '%s' shares a body with %d other declaration(s); "
                     "give each seam its own body",
                     compiler->build.name, pending_count);
                break;
            }
            for( i = 0; i < compiler->build.op_count; i++ )
                free(compiler->build.string_operands[i]);
            continue;
        }

        if( !finish_script(compiler) )
            break;

        if( pending_count )
        {
            int body_index = script_id_for_name(compiler, compiler->build.name);
            int i;

            for( i = 0; i < pending_count && !compiler->failed; i++ )
                alias_script(compiler, body_index, pending_names[i], pending_keys[i]);
            pending_count = 0;
            if( compiler->failed )
                break;
        }
    }

    free(source);
    return !compiler->failed;
}

/* ------------------------------------------------------------------ */

static int
compare_paths(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/**
 * Is `path` this exclusion, or inside it?
 *
 * Compared as whole path components, so excluding `.../ported_curses` cannot
 * also take `.../ported_curses_extra` with it.
 */
static int
path_is_within(const char* path, const char* prefix)
{
    size_t length = strlen(prefix);

    while( length > 0 && prefix[length - 1] == '/' )
        length--;
    if( strncmp(path, prefix, length) != 0 )
        return 0;
    return path[length] == '\0' || path[length] == '/';
}

/** Collect every `.rs2` under `dir`, recursively, minus the excluded subtrees. */
static int
collect_sources(
    const char* dir,
    const char* const* excludes,
    int exclude_count,
    char*** out_paths,
    int* out_count,
    int* out_capacity)
{
    DIR* handle;
    struct dirent* entry;
    int i;

    for( i = 0; i < exclude_count; i++ )
    {
        if( path_is_within(dir, excludes[i]) )
            return 0;
    }

    handle = opendir(dir);
    if( !handle )
        return 0;

    while( (entry = readdir(handle)) != NULL )
    {
        char path[1024];
        struct stat info;
        size_t length;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if( stat(path, &info) != 0 )
            continue;

        if( S_ISDIR(info.st_mode) )
        {
            collect_sources(path, excludes, exclude_count, out_paths, out_count, out_capacity);
            continue;
        }

        length = strlen(entry->d_name);
        if( length < 4 || strcmp(entry->d_name + length - 4, ".rs2") != 0 )
            continue;

        if( *out_count == *out_capacity )
        {
            int capacity = *out_capacity ? *out_capacity * 2 : 64;
            char** grown = (char**)realloc(*out_paths, (size_t)capacity * sizeof(char*));

            assert(grown);
            *out_paths = grown;
            *out_capacity = capacity;
        }
        (*out_paths)[(*out_count)++] = strdup(path);
    }
    closedir(handle);
    return 1;
}

int
SSC_CompileRoots(
    struct SSC_Compiler* compiler,
    const struct SSC_SourceRoot* roots,
    int root_count,
    const char* const* excludes,
    int exclude_count,
    struct SSC_Diag* diag)
{
    char** strong = NULL;
    int strong_count = 0;
    int strong_capacity = 0;
    char** weak = NULL;
    int weak_count = 0;
    int weak_capacity = 0;
    int ok = 1;
    int i;

    assert(compiler);
    assert(roots);
    assert(root_count > 0);

    /* SSC_Declare runs before SSC_CompileFile ever sets compiler->diag, but
     * both go through fail(), which only writes compiler->diag when it is
     * non-NULL — without this, a fail() during the declare pass silently
     * drops its message and the caller sees an empty diagnostic. */
    compiler->diag = diag;

    for( i = 0; i < root_count; i++ )
    {
        /*
         * An exclusion subtracts a subtree from the roots that *contain* it; it
         * never cancels a root of its own.
         *
         * Every other root is an exclusion too, and that is not a convenience:
         * a lane's scripts and the base tree's seams both live *inside* `--src`,
         * so without this each of them is walked twice — once by the root that
         * contains it and once as itself — and every name in it is a duplicate
         * declaration. Deriving it here rather than asking each caller to
         * subtract its own roots is what keeps that from being a footgun with
         * one correct spelling and several plausible wrong ones.
         */
        const char* kept[128];
        int kept_count = 0;
        int j;

        assert(roots[i].dir);
        for( j = 0; j < exclude_count + root_count; j++ )
        {
            const char* exclusion =
                j < exclude_count ? excludes[j] : roots[j - exclude_count].dir;

            if( path_is_within(roots[i].dir, exclusion) )
                continue;
            /* Dropping one silently is a subtree that quietly compiles back
             * into the pack — abort instead. */
            assert(kept_count < (int)(sizeof(kept) / sizeof(kept[0])));
            kept[kept_count++] = exclusion;
        }
        if( roots[i].weak )
            collect_sources(roots[i].dir, kept, kept_count, &weak, &weak_count,
                            &weak_capacity);
        else
            collect_sources(roots[i].dir, kept, kept_count, &strong, &strong_count,
                            &strong_capacity);
    }
    /* Sorted so script ids are stable across machines — the container indexes
     * by id, and a gosub compiled today must still resolve tomorrow. Sorted
     * across roots rather than per root, so which root a file arrived through
     * cannot move it. */
    qsort(strong, (size_t)strong_count, sizeof(char*), compare_paths);
    qsort(weak, (size_t)weak_count, sizeof(char*), compare_paths);

    /*
     * Every strong name first. A seam only knows it lost once the lane that
     * defines the same name has been declared, and the declare pass is a single
     * forward walk, so the two sets cannot be interleaved.
     */
    for( i = 0; i < strong_count && ok; i++ )
        ok = SSC_Declare(compiler, strong[i], diag);
    compiler->strong_name_count = compiler->name_count;
    compiler->weak_source = 1;
    for( i = 0; i < weak_count && ok; i++ )
        ok = SSC_Declare(compiler, weak[i], diag);
    compiler->weak_source = 0;

    for( i = 0; i < strong_count && ok; i++ )
        ok = SSC_CompileFile(compiler, strong[i], diag);
    compiler->weak_source = 1;
    for( i = 0; i < weak_count && ok; i++ )
        ok = SSC_CompileFile(compiler, weak[i], diag);
    compiler->weak_source = 0;

    for( i = 0; i < strong_count; i++ )
        free(strong[i]);
    free(strong);
    for( i = 0; i < weak_count; i++ )
        free(weak[i]);
    free(weak);
    return ok;
}

int
SSC_CompileDir(
    struct SSC_Compiler* compiler,
    const char* dir,
    struct SSC_Diag* diag)
{
    struct SSC_SourceRoot root;

    assert(compiler);
    assert(dir);

    root.dir = dir;
    root.weak = 0;
    return SSC_CompileRoots(compiler, &root, 1, NULL, 0, diag);
}

/* ------------------------------------------------------------------ */

int
SSC_Write(
    struct SSC_Compiler* compiler,
    const char* dir,
    struct SSC_Diag* diag)
{
    char path[1024];
    FILE* dat;
    FILE* idx;
    uint8_t header[8];
    uint8_t* buffer = NULL;
    size_t capacity = 0;
    struct SSVM_Error err;
    int i;
    int count;

    SSVM_ErrorClear(&err);
    count = compiler->name_count;

    snprintf(path, sizeof(path), "%s/script.dat", dir);
    dat = fopen(path, "wb");
    if( !dat )
    {
        if( diag )
            snprintf(diag->message, sizeof(diag->message), "cannot write %.242s", path);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/script.idx", dir);
    idx = fopen(path, "wb");
    if( !idx )
    {
        fclose(dat);
        if( diag )
            snprintf(diag->message, sizeof(diag->message), "cannot write %.242s", path);
        return 0;
    }

    header[0] = (uint8_t)(count >> 24);
    header[1] = (uint8_t)(count >> 16);
    header[2] = (uint8_t)(count >> 8);
    header[3] = (uint8_t)count;
    header[4] = 0;
    header[5] = 0;
    header[6] = 0;
    header[7] = SSVM_COMPILER_VERSION;
    fwrite(header, 1, 8, dat);
    fwrite(header, 1, 4, idx);

    for( i = 0; i < count; i++ )
    {
        size_t needed = 0;
        uint8_t size_bytes[4];

        /* A declared-but-never-defined name keeps its slot with size 0, which
         * is exactly how the reference records a retired script id. Ids stay
         * stable, so a compiled gosub keeps pointing at the right script. */
        if( i >= compiler->script_count || compiler->scripts[i].op_count == 0 ||
            !SSC_ScriptAt(compiler, i) ||
            !SSVM_ScriptEncode(&compiler->scripts[i], NULL, 0, &needed, &err) )
        {
            memset(size_bytes, 0, 4);
            fwrite(size_bytes, 1, 4, idx);
            continue;
        }

        if( needed > capacity )
        {
            free(buffer);
            capacity = needed * 2;
            buffer = (uint8_t*)malloc(capacity);
        }
        if( !SSVM_ScriptEncode(&compiler->scripts[i], buffer, capacity, &needed, &err) )
        {
            if( diag )
                snprintf(diag->message, sizeof(diag->message), "encoding script %d: %s", i,
                         err.message);
            free(buffer);
            fclose(dat);
            fclose(idx);
            return 0;
        }

        size_bytes[0] = (uint8_t)(needed >> 24);
        size_bytes[1] = (uint8_t)(needed >> 16);
        size_bytes[2] = (uint8_t)(needed >> 8);
        size_bytes[3] = (uint8_t)needed;
        fwrite(size_bytes, 1, 4, idx);
        fwrite(buffer, 1, needed, dat);
    }

    free(buffer);
    fclose(dat);
    fclose(idx);
    return 1;
}
