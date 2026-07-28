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
    int name_count;
    int name_capacity;

    struct SSVM_Script* scripts;
    int script_count;

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

    if( build->local_count >= SSC_MAX_LOCALS )
    {
        fail(compiler, "more than %d locals in one script", SSC_MAX_LOCALS);
        return NULL;
    }

    local = &build->locals[build->local_count++];
    snprintf(local->name, sizeof(local->name), "%s", name);
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
 */
static int
script_id_for_bare_name(struct SSC_Compiler* compiler, const char* name)
{
    static const char* const k_triggers[] = { "proc",      "label", "queue",
                                              "softtimer", "timer", "walktrigger" };
    size_t i;

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

/** Emit a call to `[proc,name]` or `[label,name]`. */
static int
parse_call(struct SSC_Compiler* compiler, const char* bare_name, int is_label)
{
    char full[SSC_MAX_NAME];
    int script_id;

    snprintf(full, sizeof(full), "[%s,%s]", is_label ? "label" : "proc", bare_name);
    script_id = script_id_for_name(compiler, full);
    if( script_id < 0 )
        return fail(compiler, "no %s named '%s'", is_label ? "label" : "proc", bare_name);

    /* Arguments are pushed left to right; the callee pops them in reverse into
     * its locals, which restores source order. */
    if( SSC_LexIsPunct(&compiler->lexer, "(") )
    {
        SSC_LexNext(&compiler->lexer);
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
        {
            for( ;; )
            {
                int arg_is_string = 0;

                if( !parse_expression(compiler, &arg_is_string) )
                    return 0;
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
    }

    emit(compiler, is_label ? SS_OP_JUMP_WITH_PARAMS : SS_OP_GOSUB_WITH_PARAMS, script_id);
    return 1;
}

/** Emit a command call. `dot` selects the secondary-pointer form. */
static int
parse_command(struct SSC_Compiler* compiler, const char* name, int* is_string)
{
    int opcode;
    const struct SSVM_OpcodeMeta* meta;
    int dot = (name[0] == '.');
    int variadic = 0;

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

    /* A command may be written bare when it takes nothing — `p_pausebutton;`. */
    if( SSC_LexIsPunct(&compiler->lexer, "(") )
    {
        SSC_LexNext(&compiler->lexer);
        if( !SSC_LexIsPunct(&compiler->lexer, ")") )
        {
            for( ;; )
            {
                int arg_is_string = 0;

                if( !parse_expression(compiler, &arg_is_string) )
                    return 0;
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

    if( is_string )
        *is_string = meta->str_out > 0;
    return 1;
}

/*
 * Is the text between a `<` and its `>` an interpolation, or markup?
 *
 * Both live in string literals and only the first is compiled:
 *
 *   interpolation   <$name>  <%varp>  <displayname>  <.displayname>
 *                   <tostring($level)>  <lowercase(oc_name($ingredient))>
 *   markup          <br>  <col=ff0000>  <p,happy>  <lt>
 *
 * A leading sigil settles it immediately. Otherwise the leading identifier has
 * to name a command *and* be followed by `(` or nothing else — that second half
 * matters: `<p,happy>` would slip through on the name test alone if a command
 * were ever called `p`, and the corpus has 6,577 of those.
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

    if( inner[i] == '$' || inner[i] == '%' )
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

    return SSVM_OpcodeFromName(name) >= 0;
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
        return parse_call(compiler, text, 0);

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

        /* A name is a command if the opcode table knows it, otherwise a symbol
         * from the packs. Commands win: content never names a symbol after a
         * command, and the reference resolves the same way. */
        if( SSVM_OpcodeFromName(text) >= 0 )
        {
            SSC_LexNext(lexer);
            return parse_command(compiler, text, is_string);
        }

        symbol = SSC_SymbolsFind(compiler->symbols, text, SSC_SYM_UNKNOWN);
        if( symbol )
        {
            emit(compiler, SS_OP_PUSH_CONSTANT_INT, symbol->value);
            SSC_LexNext(lexer);
            return 1;
        }

        /* A queue/timer/softtimer/walktrigger argument names a script, and its
         * value is that script's id. */
        {
            int script_id = script_id_for_bare_name(compiler, text);

            if( script_id >= 0 )
            {
                emit(compiler, SS_OP_PUSH_CONSTANT_INT, script_id);
                SSC_LexNext(lexer);
                return 1;
            }
        }
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

    snprintf(op, sizeof(op), "%s", lexer->current.text);
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
            snprintf(local_name, sizeof(local_name), "%s", lexer->current.text);
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
        if( !parse_call(compiler, text, is_label) )
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
    snprintf(trigger_name, sizeof(trigger_name), "%s", lexer->current.text);
    SSC_LexNext(lexer);

    if( !SSC_LexIsPunct(lexer, ",") )
        return fail(compiler, "expected ',' after the trigger name");
    SSC_LexNext(lexer);

    /* mapzone/zone subjects are coords — `[mapzoneexit,0_49_46]` — which lex as
     * a number rather than a name. */
    if( lexer->current.kind == SSC_TOK_IDENT || lexer->current.kind == SSC_TOK_INT )
    {
        snprintf(subject, sizeof(subject), "%s", lexer->current.text);
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
        snprintf(out_name, name_capacity, "[command,%s]", subject);
        *out_lookup_key = -1;
        return 2;
    }

    trigger = SSVM_TriggerFromName(trigger_name);
    if( trigger < 0 )
        return fail(compiler, "unknown trigger '%s'", trigger_name);

    snprintf(out_name, name_capacity, "[%s,%s]", trigger_name, subject);

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
        /* A three-part coord lexes to its first component, which is exactly
         * what the reference's parseInt("0_49_46") yields — and why all 118 of
         * its mapzone/zone scripts collapse onto one key. Reproduced rather
         * than fixed: the compiled key has to match what the reader expects,
         * and the reference never dispatches these triggers anyway.
         * test-ss-corpus reports the collision count. */
        *out_lookup_key =
            (int32_t)SSVM_LookupKey(trigger, SS_LOOKUP_TYPE, subject_value);
    }
    else
    {
        /* A subject spelled `_name` is a CATEGORY, not a type — the underscore
         * is the source syntax for it, and 310 scripts use the form. The
         * compiled name keeps the underscore (`[oploc1,_outpost_gate]`), so
         * only the lookup strips it. */
        int is_category = subject[0] == '_' && subject[1] != '\0';
        const struct SSC_Symbol* symbol = SSC_SymbolsFind(
            compiler->symbols,
            is_category ? subject + 1 : subject,
            is_category ? SSC_SYM_CATEGORY : SSC_SYM_UNKNOWN);

        if( !symbol )
            return fail(compiler, "unknown %s subject '%s' for trigger '%s'",
                        is_category ? "category" : "type", subject, trigger_name);

        /* The on-disk key is an i32 with the subject at bit 10, so anything
         * above 2^21 cannot be represented. Refusing here is the difference
         * between a compile error and a script that silently never fires. */
        if( symbol->value < 0 || symbol->value >= (1 << 21) )
            return fail(compiler,
                        "subject '%s' is %d, which does not fit the 21-bit subject field",
                        subject, symbol->value);

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
        snprintf(type, sizeof(type), "%s", lexer->current.text);
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

    if( !compiler )
        return NULL;
    compiler->symbols = symbols;
    compiler->scripts =
        (struct SSVM_Script*)calloc(SSC_MAX_SCRIPTS, sizeof(struct SSVM_Script));
    compiler->names = (char(*)[SSC_MAX_NAME])calloc(SSC_MAX_SCRIPTS, SSC_MAX_NAME);
    compiler->name_capacity = SSC_MAX_SCRIPTS;
    if( !compiler->scripts || !compiler->names )
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
    if( !compiler || index < 0 || index >= compiler->script_count )
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
    if( !data )
    {
        fclose(file);
        return NULL;
    }
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
            snprintf(trigger, sizeof(trigger), "%s", lexer.current.text);
            SSC_LexNext(&lexer);
            if( !SSC_LexIsPunct(&lexer, ",") )
                continue;
            SSC_LexNext(&lexer);
            snprintf(subject, sizeof(subject), "%s", lexer.current.text);
            SSC_LexNext(&lexer);

            /* Command declarations are not scripts and must not take ids. */
            if( strcmp(trigger, "command") == 0 )
                continue;

            if( compiler->name_count < compiler->name_capacity )
            {
                snprintf(compiler->names[compiler->name_count], SSC_MAX_NAME, "[%s,%s]",
                         trigger, subject);
                compiler->name_count++;
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
     * required to write it. */
    if( build->op_count == 0 || build->opcodes[build->op_count - 1] != SS_OP_RETURN )
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

int
SSC_CompileFile(
    struct SSC_Compiler* compiler,
    const char* path,
    struct SSC_Diag* diag)
{
    size_t length = 0;
    char* source = read_file(path, &length);

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
        if( !finish_script(compiler) )
            break;
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

/** Collect every `.rs2` under `dir`, recursively. */
static int
collect_sources(const char* dir, char*** out_paths, int* out_count, int* out_capacity)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;

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
            collect_sources(path, out_paths, out_count, out_capacity);
            continue;
        }

        length = strlen(entry->d_name);
        if( length < 4 || strcmp(entry->d_name + length - 4, ".rs2") != 0 )
            continue;

        if( *out_count == *out_capacity )
        {
            int capacity = *out_capacity ? *out_capacity * 2 : 64;
            char** grown = (char**)realloc(*out_paths, (size_t)capacity * sizeof(char*));

            if( !grown )
                break;
            *out_paths = grown;
            *out_capacity = capacity;
        }
        (*out_paths)[(*out_count)++] = strdup(path);
    }
    closedir(handle);
    return 1;
}

int
SSC_CompileDir(
    struct SSC_Compiler* compiler,
    const char* dir,
    struct SSC_Diag* diag)
{
    char** paths = NULL;
    int count = 0;
    int capacity = 0;
    int ok = 1;
    int i;

    collect_sources(dir, &paths, &count, &capacity);
    /* Sorted so script ids are stable across machines — the container indexes
     * by id, and a gosub compiled today must still resolve tomorrow. */
    qsort(paths, (size_t)count, sizeof(char*), compare_paths);

    for( i = 0; i < count && ok; i++ )
        ok = SSC_Declare(compiler, paths[i], diag);
    for( i = 0; i < count && ok; i++ )
        ok = SSC_CompileFile(compiler, paths[i], diag);

    for( i = 0; i < count; i++ )
        free(paths[i]);
    free(paths);
    return ok;
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
            snprintf(diag->message, sizeof(diag->message), "cannot write %s", path);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/script.idx", dir);
    idx = fopen(path, "wb");
    if( !idx )
    {
        fclose(dat);
        if( diag )
            snprintf(diag->message, sizeof(diag->message), "cannot write %s", path);
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
