#include "cs2_gen.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cs2_writer
{
    struct RSCache_CS2_FunctionSet* fs;
    struct RSCache_CS2_Function* function;
    const struct RSCache_CS2_Names* names;
    struct RSCache_CS2_StrBuf* out;

    int indents;
    /** Inside a `calc(...)`, so nested arithmetic does not reopen one. */
    bool in_calc;
    /** Cleared by a bare top-level `return`, which prints as nothing at all. */
    bool ending_line;

    /* Variable*: locals already given a `def_`. */
    struct RSCache_CS2_Vec defined_locals;

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_gen_fail(struct cs2_writer* writer, const char* fmt, ...)
{
    if( writer->failed )
        return;
    writer->failed = true;
    if( !writer->error || writer->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(writer->error, (size_t)writer->error_capacity, fmt, args);
    va_end(args);
}

static void
cs2_put(struct cs2_writer* writer, const char* text)
{
    RSCache_CS2_StrBufAppend(writer->out, text);
}

static void
cs2_put_char(struct cs2_writer* writer, char ch)
{
    RSCache_CS2_StrBufAppendChar(writer->out, ch);
}

static void
cs2_put_int(struct cs2_writer* writer, int value)
{
    RSCache_CS2_StrBufAppendInt(writer->out, value);
}

/**
 * Write a cache string as UTF-8.
 *
 * The cache stores strings as windows-1252 wire bytes and this library keeps
 * them that way (EXCEPTIONS.md A2). Source files are text, though, so the one
 * place the bytes become characters is here — a `0xA0` in a message is a
 * non-breaking space, and emitting it raw makes the listing invalid UTF-8. The
 * conversion is a bijection, and the compiler applies its inverse.
 */
static void
cs2_put_cache_string(struct cs2_writer* writer, const char* text)
{
    if( !text )
        return;
    char stack_buffer[1024];
    char* buffer = stack_buffer;
    int needed = RSCache_Cp1252ToUtf8(text, stack_buffer, (int)sizeof(stack_buffer));
    if( needed >= (int)sizeof(stack_buffer) )
    {
        buffer = (char*)malloc((size_t)needed + 1);
        if( !buffer )
        {
            cs2_gen_fail(writer, "out of memory converting a string constant");
            return;
        }
        RSCache_Cp1252ToUtf8(text, buffer, needed + 1);
    }
    /* Quote the two bytes that delimit source strings. The compiler removes
     * these escapes before converting the text back to cache bytes. */
    for( const char* cursor = buffer; *cursor; cursor++ )
    {
        if( *cursor == '"' || *cursor == '\\' )
            RSCache_CS2_StrBufAppendChar(writer->out, '\\');
        RSCache_CS2_StrBufAppendChar(writer->out, *cursor);
    }
    if( buffer != stack_buffer )
        free(buffer);
}

static void
cs2_next_line(struct cs2_writer* writer)
{
    cs2_put_char(writer, '\n');
    for( int i = 0; i < writer->indents; i++ )
        cs2_put_char(writer, '\t');
}

static void
cs2_write_expr(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr);

static void
cs2_write_construct(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct);

/* -------------------------------------------------------------------------
 * Typing access
 * ---------------------------------------------------------------------- */

static struct RSCache_CS2_Typing*
cs2_typing_of_variable(struct cs2_writer* writer, struct RSCache_CS2_Variable* variable)
{
    return RSCache_CS2_TypingsOfVariable(&writer->fs->typings, variable);
}

static const char*
cs2_variable_literal(struct cs2_writer* writer, struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Typing* typing = cs2_typing_of_variable(writer, variable);
    /* A local nothing constrained has no type, and `RSCache_CS2_TypeLiteral`
     * spells that `?` — which is honest in a diagnostic and unusable in source:
     * 22 scripts decompiled to a signature reading `[clientscript,x](? $int0)`
     * that the compiler could not read back. The bank it lives in is known even
     * when the type is not, and that is what `int` and `string` name. */
    if( typing->type == RSCACHE_CS2_TYPE_NONE )
        return RSCache_CS2_VarStackType(variable->kind) == RSCACHE_CS2_STACK_STRING ? "string"
                                                                                   : "int";
    return RSCache_CS2_TypeLiteral(typing->type);
}

/* -------------------------------------------------------------------------
 * Variables
 * ---------------------------------------------------------------------- */

static void
cs2_write_var_identifier(struct cs2_writer* writer, struct RSCache_CS2_Variable* variable)
{
    switch( variable->kind )
    {
    case RSCACHE_CS2_VAR_INT:
    case RSCACHE_CS2_VAR_STRING:
    case RSCACHE_CS2_VAR_ARRAY:
    {
        struct RSCache_CS2_Typing* typing = cs2_typing_of_variable(writer, variable);
        cs2_put(writer, typing->identifier ? typing->identifier : "int");
        if( variable->kind == RSCACHE_CS2_VAR_ARRAY )
            cs2_put(writer, "array");
        cs2_put_int(writer, variable->id);
        return;
    }
    case RSCACHE_CS2_VAR_VARP:
        cs2_put(writer, "var");
        break;
    case RSCACHE_CS2_VAR_VARBIT:
        cs2_put(writer, "varbit");
        break;
    case RSCACHE_CS2_VAR_VARCINT:
        cs2_put(writer, "varcint");
        break;
    case RSCACHE_CS2_VAR_VARCSTRING:
        cs2_put(writer, "varcstring");
        break;
    case RSCACHE_CS2_VAR_VARCLANSETTING:
        cs2_put(writer, "varclansetting");
        break;
    case RSCACHE_CS2_VAR_VARCLAN:
        cs2_put(writer, "varclan");
        break;
    default:
        cs2_gen_fail(writer, "an operand-stack slot survived to code generation");
        return;
    }
    cs2_put_int(writer, variable->id);
}

static void
cs2_write_var_access(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    if( RSCache_CS2_VarIsLocal(expr->variable->kind) )
        cs2_put_char(writer, '$');
    else if( RSCache_CS2_VarIsGlobal(expr->variable->kind) )
        cs2_put_char(writer, '%');
    else
        cs2_gen_fail(writer, "variable is neither local nor global");
    cs2_write_var_identifier(writer, expr->variable);
}

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

static void
cs2_write_expr_list(struct cs2_writer* writer, struct RSCache_CS2_Expr** items, int count)
{
    for( int i = 0; i < count; i++ )
    {
        if( i )
            cs2_put(writer, ", ");
        cs2_write_expr(writer, items[i]);
    }
}

/**
 * An argument list, which is a fresh `calc` context.
 *
 * `in_calc` stops nested arithmetic from reopening a `calc(` it is already
 * inside — right for `a + b * c`, wrong the moment an argument list intervenes.
 * `calc($a - ~proc(0, $b - $c))` is inside a calc at the point `$b - $c` is
 * written, but that subtraction sits in an argument, where bare arithmetic is
 * not legal and the compiler rejects it on the closing paren. Eighteen scripts
 * in cache.osrs239 decompiled to source that would not compile back.
 */
static void
cs2_write_arg_list(struct cs2_writer* writer, struct RSCache_CS2_Expr** items, int count)
{
    cs2_write_expr_list(writer, items, count);
}

/** One argument in its own right — an array index, a `def_` size. */
static void
cs2_write_arg(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    cs2_write_arg_list(writer, &expr, 1);
}

static void
cs2_write_constant(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    if( expr->value.stack_type == RSCACHE_CS2_STACK_STRING )
    {
        cs2_put_char(writer, '"');
        cs2_put_cache_string(writer, expr->value.string_value);
        cs2_put_char(writer, '"');
        return;
    }

    struct RSCache_CS2_Typing* typing =
        RSCache_CS2_TypingsOfElement(&writer->fs->typings, expr);
    char text[512];
    if( !RSCache_CS2_NamesFormatInt(
            writer->names, expr->value.int_value, typing->type, typing->identifier, text,
            (int)sizeof(text)) )
    {
        cs2_gen_fail(
            writer,
            "no source spelling for %d as %s-%s",
            expr->value.int_value,
            RSCache_CS2_TypeLiteral(typing->type),
            typing->identifier ? typing->identifier : "?");
        return;
    }
    cs2_put(writer, text);
}

/**
 * Print `expr` as one side of `op`, parenthesising only where precedence or
 * associativity would otherwise change the meaning.
 */
static void
cs2_write_operand(
    struct cs2_writer* writer,
    struct RSCache_CS2_Expr* expr,
    struct RSCache_CS2_Expr* op,
    bool right_hand_side)
{
    int op_precedence = RSCache_CS2_CommandPrecedence(op->opcode);
    if( op_precedence == 0 )
    {
        cs2_gen_fail(writer, "opcode %d has no precedence but prints infix", op->opcode);
        return;
    }
    int expr_precedence = expr->kind == RSCACHE_CS2_EXPR_OPERATION
                              ? RSCache_CS2_CommandPrecedence(expr->opcode)
                              : 0;
    bool parenthesise = expr_precedence > op_precedence ||
                        (right_hand_side && op_precedence == expr_precedence);
    if( parenthesise )
        cs2_put_char(writer, '(');
    cs2_write_expr(writer, expr);
    if( parenthesise )
        cs2_put_char(writer, ')');
}

static void
cs2_write_binary(
    struct cs2_writer* writer,
    struct RSCache_CS2_Expr* left,
    struct RSCache_CS2_Expr* op,
    struct RSCache_CS2_Expr* right,
    const char* text)
{
    cs2_write_operand(writer, left, op, false);
    cs2_put_char(writer, ' ');
    cs2_put(writer, text);
    cs2_put_char(writer, ' ');
    cs2_write_operand(writer, right, op, true);
}

static void
cs2_write_operation(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** args = NULL;
    int arg_count = 0;
    RSCache_CS2_ExprAsList(expr->arguments, &args, &arg_count, &single);

    switch( expr->opcode )
    {
    case RSCACHE_CS2_OP_DEFINE_ARRAY:
        if( arg_count != 2 || args[0]->kind != RSCACHE_CS2_EXPR_ACCESS )
            break;
        cs2_put(writer, "def_");
        cs2_put(writer, cs2_variable_literal(writer, args[0]->variable));
        cs2_put_char(writer, ' ');
        cs2_write_var_access(writer, args[0]);
        cs2_put_char(writer, '(');
        cs2_write_arg(writer, args[1]);
        cs2_put_char(writer, ')');
        return;

    case RSCACHE_CS2_OP_PUSH_ARRAY_INT:
        if( arg_count != 2 || args[0]->kind != RSCACHE_CS2_EXPR_ACCESS )
            break;
        cs2_write_var_access(writer, args[0]);
        cs2_put_char(writer, '(');
        cs2_write_arg(writer, args[1]);
        cs2_put_char(writer, ')');
        return;

    case RSCACHE_CS2_OP_POP_ARRAY_INT:
        if( arg_count != 3 || args[0]->kind != RSCACHE_CS2_EXPR_ACCESS )
            break;
        cs2_write_var_access(writer, args[0]);
        cs2_put_char(writer, '(');
        cs2_write_arg(writer, args[1]);
        cs2_put(writer, ") = ");
        cs2_write_expr(writer, args[2]);
        return;

    case RSCACHE_CS2_OP_JOIN_STRING:
        /* String interpolation: literal parts inline, everything else in
         * angle brackets. */
        cs2_put_char(writer, '"');
        for( int i = 0; i < arg_count; i++ )
        {
            struct RSCache_CS2_Expr* arg = args[i];
            if( arg->kind == RSCACHE_CS2_EXPR_CONSTANT &&
                arg->value.stack_type == RSCACHE_CS2_STACK_STRING )
            {
                cs2_put_cache_string(writer, arg->value.string_value);
            }
            else
            {
                cs2_put_char(writer, '<');
                cs2_write_expr(writer, arg);
                cs2_put_char(writer, '>');
            }
        }
        cs2_put_char(writer, '"');
        return;

    default:
        break;
    }

    const char* branch_infix = RSCache_CS2_CommandBranchInfix(expr->opcode);
    const char* calc_infix = RSCache_CS2_CommandCalcInfix(expr->opcode);
    if( branch_infix && arg_count == 2 )
    {
        cs2_write_binary(writer, args[0], expr, args[1], branch_infix);
        return;
    }
    if( calc_infix && arg_count == 2 )
    {
        /* Arithmetic is only legal inside `calc`, and one calc covers a whole
         * nested expression rather than each operator. */
        bool was_in_calc = writer->in_calc;
        if( !was_in_calc )
        {
            cs2_put(writer, "calc(");
            writer->in_calc = true;
        }
        cs2_write_binary(writer, args[0], expr, args[1], calc_infix);
        writer->in_calc = was_in_calc;
        if( !was_in_calc )
            cs2_put_char(writer, ')');
        return;
    }

    if( expr->dot )
        cs2_put_char(writer, '.');
    const char* name = RSCache_CS2_CommandName(expr->opcode);
    if( !name )
    {
        cs2_gen_fail(writer, "opcode %d has no name", expr->opcode);
        return;
    }
    cs2_put(writer, name);
    if( arg_count > 0 )
    {
        cs2_put_char(writer, '(');
        cs2_write_arg_list(writer, args, arg_count);
        cs2_put_char(writer, ')');
    }
}

/**
 * The name a call site writes for its target, checking it against the kind of
 * call. A `~name` must be a proc and a hook must be a clientscript; if the name
 * table disagrees, the table and the bytecode describe different scripts and
 * the safe thing is to say so.
 */
static bool
cs2_write_call_target(
    struct cs2_writer* writer,
    int script_id,
    enum RSCache_CS2_Trigger required)
{
    const char* cache_name = RSCache_CS2_NamesScript(writer->names, script_id);
    if( !cache_name )
    {
        cs2_put(writer, "script");
        cs2_put_int(writer, script_id);
        return true;
    }

    struct RSCache_CS2_ScriptName parsed;
    if( !RSCache_CS2_ScriptNameParse(cache_name, &writer->fs->arena, &parsed) )
    {
        cs2_put(writer, "script");
        cs2_put_int(writer, script_id);
        return true;
    }
    if( parsed.trigger != required )
    {
        /* The name table and the bytecode disagree about what this script is.
         * The table is community-recovered and lags the cache, whereas the call
         * site is evidence, so the id is written instead of taking the whole
         * script down. Upstream throws here; that is its single largest cause
         * of failure, and nothing is gained by it. */
        cs2_put(writer, "script");
        cs2_put_int(writer, script_id);
        return true;
    }
    cs2_put(writer, parsed.name);
    return true;
}

static void
cs2_write_proc(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** args = NULL;
    int arg_count = 0;
    RSCache_CS2_ExprAsList(expr->arguments, &args, &arg_count, &single);

    cs2_put_char(writer, '~');
    if( !cs2_write_call_target(writer, expr->script_id, RSCACHE_CS2_TRIGGER_PROC) )
        return;
    if( arg_count > 0 )
    {
        cs2_put_char(writer, '(');
        cs2_write_arg_list(writer, args, arg_count);
        cs2_put_char(writer, ')');
    }
}

static void
cs2_write_clientscript(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    struct RSCache_CS2_Expr* args_single = NULL;
    struct RSCache_CS2_Expr** args = NULL;
    int arg_count = 0;
    RSCache_CS2_ExprAsList(expr->arguments, &args, &arg_count, &args_single);

    struct RSCache_CS2_Expr* triggers_single = NULL;
    struct RSCache_CS2_Expr** triggers = NULL;
    int trigger_count = 0;
    RSCache_CS2_ExprAsList(expr->triggers, &triggers, &trigger_count, &triggers_single);

    if( expr->dot )
        cs2_put_char(writer, '.');
    const char* name = RSCache_CS2_CommandName(expr->opcode);
    if( !name )
    {
        cs2_gen_fail(writer, "opcode %d has no name", expr->opcode);
        return;
    }
    cs2_put(writer, name);
    cs2_put_char(writer, '(');

    if( expr->script_id == -1 )
    {
        /* Clearing the hook rather than setting one. */
        cs2_put(writer, "null");
    }
    else
    {
        /* The whole callback — name, arguments and watched values — is one
         * quoted string in the source. */
        cs2_put_char(writer, '"');
        if( !cs2_write_call_target(
                writer, expr->script_id, RSCACHE_CS2_TRIGGER_CLIENTSCRIPT) )
            return;
        if( arg_count > 0 )
        {
            cs2_put_char(writer, '(');
            cs2_write_expr_list(writer, args, arg_count);
            cs2_put_char(writer, ')');
        }
        if( trigger_count > 0 )
        {
            cs2_put_char(writer, '{');
            cs2_write_expr_list(writer, triggers, trigger_count);
            cs2_put_char(writer, '}');
        }
        cs2_put_char(writer, '"');
    }

    if( expr->component )
    {
        cs2_put(writer, ", ");
        cs2_write_expr(writer, expr->component);
    }
    cs2_put_char(writer, ')');
}

static void
cs2_write_expr(struct cs2_writer* writer, struct RSCache_CS2_Expr* expr)
{
    if( writer->failed || !expr )
        return;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
        cs2_put(writer, RSCache_CS2_EventPropertyLiteral(expr->event_property));
        return;
    case RSCACHE_CS2_EXPR_ACCESS:
        cs2_write_var_access(writer, expr);
        return;
    case RSCACHE_CS2_EXPR_POINTER:
        cs2_write_var_identifier(writer, expr->variable);
        return;
    case RSCACHE_CS2_EXPR_CONSTANT:
        cs2_write_constant(writer, expr);
        return;
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        cs2_write_clientscript(writer, expr);
        return;
    case RSCACHE_CS2_EXPR_PROC:
        cs2_write_proc(writer, expr);
        return;
    case RSCACHE_CS2_EXPR_OPERATION:
        cs2_write_operation(writer, expr);
        return;
    case RSCACHE_CS2_EXPR_COMPOUND:
        cs2_write_expr_list(
            writer, (struct RSCache_CS2_Expr**)expr->children.items, expr->children.count);
        return;
    default:
        cs2_gen_fail(writer, "unprintable expression");
        return;
    }
}

/* -------------------------------------------------------------------------
 * Instructions
 * ---------------------------------------------------------------------- */

static void
cs2_write_assignment(struct cs2_writer* writer, struct RSCache_CS2_Insn* insn)
{
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** defs = NULL;
    int def_count = 0;
    RSCache_CS2_ExprAsList(insn->definitions, &defs, &def_count, &single);

    if( def_count > 0 )
    {
        if( def_count == 1 )
        {
            struct RSCache_CS2_Expr* def = defs[0];
            if( def->kind == RSCACHE_CS2_EXPR_ACCESS &&
                RSCache_CS2_VarIsLocal(def->variable->kind) &&
                RSCache_CS2_VecAddUnique(&writer->defined_locals, def->variable) )
            {
                /* First write to a local declares it. */
                cs2_put(writer, "def_");
                cs2_put(writer, cs2_variable_literal(writer, def->variable));
                cs2_put_char(writer, ' ');
            }
            cs2_write_var_access(writer, def);
        }
        else
        {
            cs2_write_expr_list(writer, defs, def_count);
        }
        cs2_put(writer, " = ");
    }
    cs2_write_expr(writer, insn->expression);
}

static void
cs2_write_insn(struct cs2_writer* writer, struct RSCache_CS2_Insn* insn)
{
    switch( insn->kind )
    {
    case RSCACHE_CS2_INSN_ASSIGNMENT:
        cs2_write_assignment(writer, insn);
        break;
    case RSCACHE_CS2_INSN_RETURN:
    {
        /* A script that returns nothing ends by falling off the end, so its
         * trailing `return` is not written at all. */
        if( writer->indents == 0 && writer->function->return_type_count == 0 )
        {
            writer->ending_line = false;
            return;
        }
        cs2_put(writer, "return");
        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** items = NULL;
        int count = 0;
        RSCache_CS2_ExprAsList(insn->expression, &items, &count, &single);
        if( count > 0 )
        {
            cs2_put_char(writer, '(');
            cs2_write_expr_list(writer, items, count);
            cs2_put_char(writer, ')');
        }
        break;
    }
    default:
        cs2_gen_fail(writer, "unstructured control flow survived reconstruction");
        return;
    }
    cs2_put_char(writer, ';');
}

/* -------------------------------------------------------------------------
 * Constructs
 * ---------------------------------------------------------------------- */

static void
cs2_write_seq(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct)
{
    for( int i = 0; i < construct->instructions.count; i++ )
    {
        cs2_next_line(writer);
        cs2_write_insn(writer, (struct RSCache_CS2_Insn*)construct->instructions.items[i]);
        if( writer->failed )
            return;
    }
    if( construct->next )
        cs2_write_construct(writer, construct->next);
}

static void
cs2_write_if(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_next_line(writer);
    for( int i = 0; i < construct->branch_conditions.count; i++ )
    {
        if( i )
            cs2_put(writer, " else if (");
        else
            cs2_put(writer, "if (");
        cs2_write_expr(writer, (struct RSCache_CS2_Expr*)construct->branch_conditions.items[i]);
        cs2_put(writer, ") {");
        writer->indents++;
        cs2_write_construct(
            writer, (struct RSCache_CS2_Construct*)construct->branch_bodies.items[i]);
        writer->indents--;
        cs2_next_line(writer);
        cs2_put_char(writer, '}');
        if( writer->failed )
            return;
    }
    if( construct->otherwise )
    {
        cs2_put(writer, " else {");
        writer->indents++;
        cs2_write_construct(writer, construct->otherwise);
        writer->indents--;
        cs2_next_line(writer);
        cs2_put_char(writer, '}');
    }
    if( construct->next )
        cs2_write_construct(writer, construct->next);
}

static void
cs2_write_while(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_next_line(writer);
    cs2_put(writer, "while (");
    cs2_write_expr(writer, construct->condition);
    cs2_put(writer, ") {");
    writer->indents++;
    cs2_write_construct(writer, construct->body);
    writer->indents--;
    cs2_next_line(writer);
    cs2_put_char(writer, '}');
    if( construct->next )
        cs2_write_construct(writer, construct->next);
}

static void
cs2_write_switch(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_next_line(writer);

    /* The switch is typed: `switch_int`, `switch_stat` and so on, because the
     * case labels are spelled under that type. */
    struct RSCache_CS2_TypingList* typings =
        RSCache_CS2_TypingsOfExpr(&writer->fs->typings, construct->expression);
    if( typings->count != 1 )
    {
        cs2_gen_fail(writer, "switch subject does not produce exactly one value");
        return;
    }
    struct RSCache_CS2_Typing* typing = typings->items[0];

    cs2_put(writer, "switch_");
    cs2_put(writer, RSCache_CS2_TypeLiteral(typing->type));
    cs2_put(writer, " (");
    cs2_write_expr(writer, construct->expression);
    cs2_put(writer, ") {");

    for( int i = 0; i < construct->case_keys.count; i++ )
    {
        struct RSCache_CS2_IntVec* keys =
            (struct RSCache_CS2_IntVec*)construct->case_keys.items[i];
        writer->indents++;
        cs2_next_line(writer);
        cs2_put(writer, "case ");
        for( int j = 0; j < keys->count; j++ )
        {
            if( j )
                cs2_put(writer, ", ");
            char text[512];
            if( !RSCache_CS2_NamesFormatInt(
                    writer->names, keys->items[j], typing->type, typing->identifier, text,
                    (int)sizeof(text)) )
            {
                cs2_gen_fail(
                    writer,
                    "no source spelling for case %d as %s",
                    keys->items[j],
                    RSCache_CS2_TypeLiteral(typing->type));
                writer->indents--;
                return;
            }
            cs2_put(writer, text);
        }
        cs2_put(writer, " :");
        writer->indents++;
        cs2_write_construct(
            writer, (struct RSCache_CS2_Construct*)construct->case_bodies.items[i]);
        writer->indents--;
        writer->indents--;
        if( writer->failed )
            return;
    }

    if( construct->default_case )
    {
        writer->indents++;
        cs2_next_line(writer);
        cs2_put(writer, "case default :");
        writer->indents++;
        cs2_write_construct(writer, construct->default_case);
        writer->indents--;
        writer->indents--;
    }

    cs2_next_line(writer);
    cs2_put_char(writer, '}');
    if( construct->next )
        cs2_write_construct(writer, construct->next);
}

static void
cs2_write_construct(struct cs2_writer* writer, struct RSCache_CS2_Construct* construct)
{
    if( writer->failed || !construct )
        return;
    switch( construct->kind )
    {
    case RSCACHE_CS2_CONSTRUCT_SEQ:
        cs2_write_seq(writer, construct);
        return;
    case RSCACHE_CS2_CONSTRUCT_IF:
        cs2_write_if(writer, construct);
        return;
    case RSCACHE_CS2_CONSTRUCT_WHILE:
        cs2_write_while(writer, construct);
        return;
    case RSCACHE_CS2_CONSTRUCT_SWITCH:
        cs2_write_switch(writer, construct);
        return;
    default:
        cs2_gen_fail(writer, "unknown construct");
        return;
    }
}

/* -------------------------------------------------------------------------
 * Entry points
 * ---------------------------------------------------------------------- */

const char*
RSCache_CS2_FunctionName(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_Names* names,
    char* out,
    int out_capacity)
{
    const char* cache_name = RSCache_CS2_NamesScript(names, function->id);
    if( cache_name )
    {
        struct RSCache_CS2_ScriptName parsed;
        if( RSCache_CS2_ScriptNameParse(cache_name, &fs->arena, &parsed) )
        {
            /* Always re-formatted, never echoed: a cache name may be a bare
             * integer that encodes a trigger and a subject, and the source
             * heading wants the `[trigger,name]` form either way. */
            snprintf(
                out,
                (size_t)out_capacity,
                "%s",
                RSCache_CS2_ScriptNameFormat(&parsed, &fs->arena));
            return out;
        }
    }

    intptr_t trigger = (intptr_t)RSCache_CS2_IntMapGet(&fs->call_triggers, function->id);
    if( trigger == 0 )
        trigger = function->return_type_count > 0 ? RSCACHE_CS2_TRIGGER_PROC
                                                  : RSCACHE_CS2_TRIGGER_CLIENTSCRIPT;
    snprintf(
        out,
        (size_t)out_capacity,
        "[%s,script%d]",
        RSCache_CS2_TriggerName((enum RSCache_CS2_Trigger)trigger),
        function->id);
    return out;
}

bool
RSCache_CS2_Generate(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    struct RSCache_CS2_Construct* root,
    const char* script_name,
    const struct RSCache_CS2_Names* names,
    struct RSCache_CS2_StrBuf* out,
    char* error,
    int error_capacity)
{
    struct cs2_writer writer;
    memset(&writer, 0, sizeof(writer));
    writer.fs = fs;
    writer.function = function;
    writer.names = names;
    writer.out = out;
    writer.ending_line = true;
    writer.error = error;
    writer.error_capacity = error_capacity;
    RSCache_CS2_VecInit(&writer.defined_locals);

    /* Arguments count as already declared, so they get no `def_`. */
    for( int i = 0; i < function->arguments.count; i++ )
        RSCache_CS2_VecAddUnique(&writer.defined_locals, function->arguments.items[i]);

    struct RSCache_CS2_TypingList* returns = NULL;
    if( function->return_type_count > 0 )
        returns = RSCache_CS2_TypingsReturns(
            &fs->typings, function->id, function->return_types, function->return_type_count);

    cs2_put(&writer, "// ");
    cs2_put_int(&writer, function->id);
    cs2_next_line(&writer);

    int generated_int_args = 0;
    int generated_string_args = 0;
    for( int i = 0; i < function->arguments.count; i++ )
    {
        struct RSCache_CS2_Variable* argument =
            (struct RSCache_CS2_Variable*)function->arguments.items[i];
        if( argument->kind == RSCACHE_CS2_VAR_ARRAY ||
            RSCache_CS2_VarStackType(argument->kind) == RSCACHE_CS2_STACK_STRING )
            generated_string_args++;
        else
            generated_int_args++;
    }
    bool preserve_frame = function->generate_lossless_metadata &&
                          (function->preserve_frame_counts ||
                           generated_int_args != function->original_int_argument_count ||
                           generated_string_args != function->original_string_argument_count);
    if( preserve_frame )
    {
        cs2_put(&writer, "// @rscache-frame ");
        cs2_put_int(&writer, function->original_local_int_count);
        cs2_put_char(&writer, ' ');
        cs2_put_int(&writer, function->original_local_string_count);
        cs2_put_char(&writer, ' ');
        cs2_put_int(&writer, function->original_int_argument_count);
        cs2_put_char(&writer, ' ');
        cs2_put_int(&writer, function->original_string_argument_count);
        cs2_next_line(&writer);
    }

    bool wrote_epilogue = false;
    for( int i = 0;
         function->generate_lossless_metadata && returns && i < returns->count;
         i++ )
    {
        if( !function->return_default_is_int_constant[i] ||
            RSCache_CS2_TypeStackType(returns->items[i]->type) != RSCACHE_CS2_STACK_INT ||
            function->return_default_values[i] ==
                RSCache_CS2_TypeEpilogueDefault(returns->items[i]->type) )
            continue;
        if( !wrote_epilogue )
        {
            cs2_put(&writer, "// @rscache-epilogue ");
            wrote_epilogue = true;
        }
        else
        {
            cs2_put_char(&writer, ' ');
        }
        cs2_put_int(&writer, i);
        cs2_put_char(&writer, '=');
        cs2_put_int(&writer, function->return_default_values[i]);
    }
    if( wrote_epilogue )
        cs2_next_line(&writer);

    cs2_put(&writer, script_name);

    if( function->arguments.count > 0 )
    {
        cs2_put_char(&writer, '(');
        for( int i = 0; i < function->arguments.count; i++ )
        {
            if( i )
                cs2_put(&writer, ", ");
            struct RSCache_CS2_Variable* argument =
                (struct RSCache_CS2_Variable*)function->arguments.items[i];
            cs2_put(&writer, cs2_variable_literal(&writer, argument));
            if( argument->kind == RSCACHE_CS2_VAR_ARRAY )
                cs2_put(&writer, "array");
            cs2_put(&writer, " $");
            cs2_write_var_identifier(&writer, argument);
        }
        cs2_put_char(&writer, ')');
    }
    else if( function->return_type_count > 0 )
    {
        cs2_put(&writer, "()");
    }

    if( function->return_type_count > 0 )
    {
        cs2_put_char(&writer, '(');
        for( int i = 0; i < returns->count; i++ )
        {
            if( i )
                cs2_put(&writer, ", ");
            cs2_put(&writer, RSCache_CS2_TypeLiteral(returns->items[i]->type));
        }
        cs2_put_char(&writer, ')');
    }

    cs2_write_construct(&writer, root);
    if( writer.ending_line && !writer.failed )
        cs2_next_line(&writer);

    RSCache_CS2_VecFree(&writer.defined_locals);
    return !writer.failed;
}
