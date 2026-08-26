#include "cs2_gen_json.h"

#include "cs2_gen.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cs2_json_writer
{
    struct RSCache_CS2_FunctionSet* fs;
    struct RSCache_CS2_Function* function;
    const struct RSCache_CS2_Names* names;
    struct RSCache_CS2_StrBuf* out;

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_json_fail(struct cs2_json_writer* writer, const char* fmt, ...)
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

/* -------------------------------------------------------------------------
 * JSON primitives
 * ---------------------------------------------------------------------- */

static void
cs2_json_put(struct cs2_json_writer* writer, const char* text)
{
    RSCache_CS2_StrBufAppend(writer->out, text);
}

static void
cs2_json_put_char(struct cs2_json_writer* writer, char ch)
{
    RSCache_CS2_StrBufAppendChar(writer->out, ch);
}

static void
cs2_json_put_int(struct cs2_json_writer* writer, int value)
{
    RSCache_CS2_StrBufAppendInt(writer->out, value);
}

/**
 * A JSON string body, without the delimiting quotes.
 *
 * The bytes arrive as windows-1252 (EXCEPTIONS.md A2) and JSON is UTF-8, so
 * the conversion happens here for the same reason the source generator does
 * it: a raw 0xA0 makes the document invalid. Control bytes are escaped
 * numerically because JSON forbids them raw, and the surviving two-character
 * escapes are the ones a reader expects to see spelled.
 */
static void
cs2_json_put_string_body(struct cs2_json_writer* writer, const char* text)
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
            cs2_json_fail(writer, "out of memory converting a string constant");
            return;
        }
        RSCache_Cp1252ToUtf8(text, buffer, needed + 1);
    }
    for( const unsigned char* cursor = (const unsigned char*)buffer; *cursor; cursor++ )
    {
        unsigned char ch = *cursor;
        switch( ch )
        {
        case '"':
            cs2_json_put(writer, "\\\"");
            continue;
        case '\\':
            cs2_json_put(writer, "\\\\");
            continue;
        case '\n':
            cs2_json_put(writer, "\\n");
            continue;
        case '\r':
            cs2_json_put(writer, "\\r");
            continue;
        case '\t':
            cs2_json_put(writer, "\\t");
            continue;
        default:
            break;
        }
        if( ch < 0x20 )
        {
            char escape[8];
            snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)ch);
            cs2_json_put(writer, escape);
            continue;
        }
        RSCache_CS2_StrBufAppendChar(writer->out, (char)ch);
    }
    if( buffer != stack_buffer )
        free(buffer);
}

static void
cs2_json_put_string(struct cs2_json_writer* writer, const char* text)
{
    cs2_json_put_char(writer, '"');
    cs2_json_put_string_body(writer, text);
    cs2_json_put_char(writer, '"');
}

/** `"key":` — every field goes through this so a caller cannot forget a quote. */
static void
cs2_json_key(struct cs2_json_writer* writer, const char* key)
{
    cs2_json_put_char(writer, '"');
    cs2_json_put(writer, key);
    cs2_json_put(writer, "\":");
}

static void
cs2_json_field_string(struct cs2_json_writer* writer, const char* key, const char* value)
{
    cs2_json_key(writer, key);
    if( value )
        cs2_json_put_string(writer, value);
    else
        cs2_json_put(writer, "null");
}

static void
cs2_json_field_int(struct cs2_json_writer* writer, const char* key, int value)
{
    cs2_json_key(writer, key);
    cs2_json_put_int(writer, value);
}

static void
cs2_json_field_bool(struct cs2_json_writer* writer, const char* key, bool value)
{
    cs2_json_key(writer, key);
    cs2_json_put(writer, value ? "true" : "false");
}

/* -------------------------------------------------------------------------
 * Vocabulary
 * ---------------------------------------------------------------------- */

static const char*
cs2_json_stack_type_name(enum RSCache_CS2_StackType stack_type)
{
    return stack_type == RSCACHE_CS2_STACK_STRING ? "string" : "int";
}

static const char*
cs2_json_var_kind_name(enum RSCache_CS2_VarKind kind)
{
    switch( kind )
    {
    case RSCACHE_CS2_VAR_VARP:
        return "varp";
    case RSCACHE_CS2_VAR_VARBIT:
        return "varbit";
    case RSCACHE_CS2_VAR_VARCINT:
        return "varcint";
    case RSCACHE_CS2_VAR_VARCSTRING:
        return "varcstring";
    case RSCACHE_CS2_VAR_VARCLANSETTING:
        return "varclansetting";
    case RSCACHE_CS2_VAR_VARCLAN:
        return "varclan";
    case RSCACHE_CS2_VAR_INT:
        return "int";
    case RSCACHE_CS2_VAR_STRING:
        return "string";
    case RSCACHE_CS2_VAR_ARRAY:
        return "array";
    default:
        return NULL;
    }
}

static const char*
cs2_json_event_name(enum RSCache_CS2_EventProperty property)
{
    /* The source spelling minus its `event_` prefix; a consumer that wants the
     * literal can re-add it, and the bare name reads better as a JSON tag. */
    const char* literal = RSCache_CS2_EventPropertyLiteral(property);
    if( literal && strncmp(literal, "event_", 6) == 0 )
        return literal + 6;
    return literal;
}

/* -------------------------------------------------------------------------
 * Typing access
 *
 * Same accessors the source generator uses, so both descriptions of a value
 * come from the one solved inference graph.
 * ---------------------------------------------------------------------- */

static const char*
cs2_json_variable_literal(
    struct cs2_json_writer* writer,
    struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Typing* typing =
        RSCache_CS2_TypingsOfVariable(&writer->fs->typings, variable);
    if( typing->type == RSCACHE_CS2_TYPE_NONE )
        return RSCache_CS2_VarStackType(variable->kind) == RSCACHE_CS2_STACK_STRING ? "string"
                                                                                   : "int";
    return RSCache_CS2_TypeLiteral(typing->type);
}

static void
cs2_json_write_variable(struct cs2_json_writer* writer, struct RSCache_CS2_Variable* variable)
{
    const char* kind = cs2_json_var_kind_name(variable->kind);
    if( !kind )
    {
        cs2_json_fail(writer, "an operand-stack slot survived to code generation");
        return;
    }
    struct RSCache_CS2_Typing* typing =
        RSCache_CS2_TypingsOfVariable(&writer->fs->typings, variable);

    cs2_json_put_char(writer, '{');
    cs2_json_field_string(writer, "kind", kind);
    cs2_json_put_char(writer, ',');
    cs2_json_field_int(writer, "id", variable->id);
    cs2_json_put_char(writer, ',');
    cs2_json_field_bool(writer, "local", RSCache_CS2_VarIsLocal(variable->kind));
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(writer, "type", cs2_json_variable_literal(writer, variable));
    cs2_json_put_char(writer, ',');
    /* The inferred identifier is what names a local in source (`$widget3`);
     * a back end that generates its own names still wants it for readability. */
    cs2_json_field_string(writer, "identifier", typing ? typing->identifier : NULL);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "stackType",
        cs2_json_stack_type_name(RSCache_CS2_VarStackType(variable->kind)));
    cs2_json_put_char(writer, '}');
}

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

static void
cs2_json_write_expr(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr);

static void
cs2_json_write_expr_array(
    struct cs2_json_writer* writer,
    struct RSCache_CS2_Expr** items,
    int count)
{
    cs2_json_put_char(writer, '[');
    for( int i = 0; i < count; i++ )
    {
        if( i )
            cs2_json_put_char(writer, ',');
        cs2_json_write_expr(writer, items[i]);
        if( writer->failed )
            return;
    }
    cs2_json_put_char(writer, ']');
}

/** An expression's `asList` view as a JSON array; `[]` for a NULL slot. */
static void
cs2_json_write_expr_list(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    if( !expr )
    {
        cs2_json_put(writer, "[]");
        return;
    }
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(expr, &items, &count, &single);
    cs2_json_write_expr_array(writer, items, count);
}

static void
cs2_json_write_stack_types(
    struct cs2_json_writer* writer,
    const enum RSCache_CS2_StackType* stack_types,
    int count)
{
    cs2_json_key(writer, "stackTypes");
    cs2_json_put_char(writer, '[');
    for( int i = 0; i < count; i++ )
    {
        if( i )
            cs2_json_put_char(writer, ',');
        cs2_json_put_string(writer, cs2_json_stack_type_name(stack_types[i]));
    }
    cs2_json_put_char(writer, ']');
}

static void
cs2_json_write_constant(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    cs2_json_put_char(writer, '{');
    cs2_json_field_string(writer, "kind", "constant");
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "stackType", cs2_json_stack_type_name(expr->value.stack_type));
    cs2_json_put_char(writer, ',');

    if( expr->value.stack_type == RSCACHE_CS2_STACK_STRING )
    {
        cs2_json_key(writer, "value");
        cs2_json_put_string(writer, expr->value.string_value);
        cs2_json_put_char(writer, ',');
        cs2_json_field_string(writer, "type", "string");
        cs2_json_put_char(writer, ',');
        cs2_json_field_string(writer, "identifier", NULL);
        cs2_json_put_char(writer, ',');
        cs2_json_field_string(writer, "literal", NULL);
        cs2_json_put_char(writer, '}');
        return;
    }

    struct RSCache_CS2_Typing* typing =
        RSCache_CS2_TypingsOfElement(&writer->fs->typings, expr);
    cs2_json_field_int(writer, "value", expr->value.int_value);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "type",
        typing->type == RSCACHE_CS2_TYPE_NONE ? "int" : RSCache_CS2_TypeLiteral(typing->type));
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(writer, "identifier", typing->identifier);
    cs2_json_put_char(writer, ',');

    /*
     * The source spelling, when there is one.
     *
     * Unlike the source generator, a missing spelling is not fatal here: JSON
     * can always state the integer, and a back end that emits its own literals
     * only wants the name as a comment. A constant with no legal CS2 literal
     * still describes the program exactly.
     */
    char text[512];
    if( RSCache_CS2_NamesFormatInt(
            writer->names, expr->value.int_value, typing->type, typing->identifier, text,
            (int)sizeof(text)) )
        cs2_json_field_string(writer, "literal", text);
    else
        cs2_json_field_string(writer, "literal", NULL);
    cs2_json_put_char(writer, '}');
}

/**
 * The name a call site writes for its target.
 *
 * Mirrors `cs2_write_call_target`, including its tolerance: a name table that
 * disagrees with the bytecode about a script's trigger is community data
 * lagging the cache, and the id is evidence, so the mismatch is reported as a
 * null name rather than taking the script down.
 */
static const char*
cs2_json_call_target(
    struct cs2_json_writer* writer,
    int script_id,
    enum RSCache_CS2_Trigger required)
{
    const char* cache_name = RSCache_CS2_NamesScript(writer->names, script_id);
    if( !cache_name )
        return NULL;
    struct RSCache_CS2_ScriptName parsed;
    if( !RSCache_CS2_ScriptNameParse(cache_name, &writer->fs->arena, &parsed) )
        return NULL;
    if( parsed.trigger != required )
        return NULL;
    return parsed.name;
}

static void
cs2_json_write_operation(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    const char* name = RSCache_CS2_CommandName(expr->opcode);
    const char* calc_infix = RSCache_CS2_CommandCalcInfix(expr->opcode);
    const char* branch_infix = RSCache_CS2_CommandBranchInfix(expr->opcode);
    /*
     * An operator need not have a command name.
     *
     * `RSCACHE_CS2_OP_SS_AND` / `_SS_OR` are synthesized by the short-circuit
     * pass and exist only in the IR, so the name table has nothing for them —
     * the source generator reaches its infix spelling before it ever asks for
     * a name, and asking first refused 1,757 of the cache's 9,725 scripts.
     * Having any of the three spellings is what makes an operation describable.
     */
    if( !name && !calc_infix && !branch_infix )
    {
        cs2_json_fail(writer, "opcode %d has neither a name nor an infix spelling",
                      expr->opcode);
        return;
    }

    cs2_json_put_char(writer, '{');
    cs2_json_field_string(writer, "kind", "operation");
    cs2_json_put_char(writer, ',');
    cs2_json_field_int(writer, "opcode", expr->opcode);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(writer, "name", name);
    cs2_json_put_char(writer, ',');
    cs2_json_field_bool(writer, "dot", expr->dot);
    cs2_json_put_char(writer, ',');
    /*
     * The infix spellings, so a back end knows this operation is an operator
     * without carrying its own opcode table. `calc` says the operator is
     * arithmetic and therefore subject to the language's integer rules;
     * `branch` says it is a comparison. Both are NULL for an ordinary command.
     */
    cs2_json_field_string(writer, "calcInfix", calc_infix);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(writer, "branchInfix", branch_infix);
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "arguments");
    cs2_json_write_expr_list(writer, expr->arguments);
    cs2_json_put_char(writer, ',');
    cs2_json_write_stack_types(writer, expr->stack_types, expr->stack_type_count);
    cs2_json_put_char(writer, '}');
}

static void
cs2_json_write_proc(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    cs2_json_put_char(writer, '{');
    cs2_json_field_string(writer, "kind", "proc");
    cs2_json_put_char(writer, ',');
    cs2_json_field_int(writer, "scriptId", expr->script_id);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "name", cs2_json_call_target(writer, expr->script_id, RSCACHE_CS2_TRIGGER_PROC));
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "arguments");
    cs2_json_write_expr_list(writer, expr->arguments);
    cs2_json_put_char(writer, ',');
    cs2_json_write_stack_types(writer, expr->stack_types, expr->stack_type_count);
    cs2_json_put_char(writer, '}');
}

/**
 * A hook registration (`if_setonclick` and its family).
 *
 * The source dialect folds the callee, its arguments and its trigger list into
 * one quoted string. That is a spelling, not a structure: kept apart here so a
 * back end can build a binding record without reparsing a string it just
 * printed. `scriptId == -1` is the clear-the-hook form.
 */
static void
cs2_json_write_clientscript(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    const char* name = RSCache_CS2_CommandName(expr->opcode);
    if( !name )
    {
        cs2_json_fail(writer, "opcode %d has no name", expr->opcode);
        return;
    }

    cs2_json_put_char(writer, '{');
    cs2_json_field_string(writer, "kind", "clientscript");
    cs2_json_put_char(writer, ',');
    cs2_json_field_int(writer, "opcode", expr->opcode);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(writer, "name", name);
    cs2_json_put_char(writer, ',');
    cs2_json_field_bool(writer, "dot", expr->dot);
    cs2_json_put_char(writer, ',');
    cs2_json_field_int(writer, "scriptId", expr->script_id);
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "scriptName",
        expr->script_id == -1
            ? NULL
            : cs2_json_call_target(writer, expr->script_id, RSCACHE_CS2_TRIGGER_CLIENTSCRIPT));
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "arguments");
    cs2_json_write_expr_list(writer, expr->arguments);
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "triggers");
    cs2_json_write_expr_list(writer, expr->triggers);
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "component");
    if( expr->component )
        cs2_json_write_expr(writer, expr->component);
    else
        cs2_json_put(writer, "null");
    cs2_json_put_char(writer, '}');
}

static void
cs2_json_write_expr(struct cs2_json_writer* writer, struct RSCache_CS2_Expr* expr)
{
    if( writer->failed )
        return;
    if( !expr )
    {
        cs2_json_put(writer, "null");
        return;
    }
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "event");
        cs2_json_put_char(writer, ',');
        cs2_json_field_string(writer, "property", cs2_json_event_name(expr->event_property));
        cs2_json_put_char(writer, '}');
        return;

    case RSCACHE_CS2_EXPR_ACCESS:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "access");
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "variable");
        cs2_json_write_variable(writer, expr->variable);
        cs2_json_put_char(writer, '}');
        return;

    /* A POINTER names the slot rather than reading it — the `def_` target and
     * an array handle passed by name. Kept distinct so a back end does not
     * emit a read where the program passes a reference. */
    case RSCACHE_CS2_EXPR_POINTER:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "pointer");
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "variable");
        cs2_json_write_variable(writer, expr->variable);
        cs2_json_put_char(writer, '}');
        return;

    case RSCACHE_CS2_EXPR_CONSTANT:
        cs2_json_write_constant(writer, expr);
        return;

    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        cs2_json_write_clientscript(writer, expr);
        return;

    case RSCACHE_CS2_EXPR_PROC:
        cs2_json_write_proc(writer, expr);
        return;

    case RSCACHE_CS2_EXPR_OPERATION:
        cs2_json_write_operation(writer, expr);
        return;

    case RSCACHE_CS2_EXPR_COMPOUND:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "compound");
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "children");
        cs2_json_write_expr_array(
            writer, (struct RSCache_CS2_Expr**)expr->children.items, expr->children.count);
        cs2_json_put_char(writer, '}');
        return;

    default:
        cs2_json_fail(writer, "undescribable expression");
        return;
    }
}

/* -------------------------------------------------------------------------
 * Instructions
 * ---------------------------------------------------------------------- */

static void
cs2_json_write_insn(struct cs2_json_writer* writer, struct RSCache_CS2_Insn* insn)
{
    switch( insn->kind )
    {
    case RSCACHE_CS2_INSN_ASSIGNMENT:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "assignment");
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "definitions");
        cs2_json_write_expr_list(writer, insn->definitions);
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "expression");
        cs2_json_write_expr(writer, insn->expression);
        cs2_json_put_char(writer, '}');
        return;

    case RSCACHE_CS2_INSN_RETURN:
        cs2_json_put_char(writer, '{');
        cs2_json_field_string(writer, "kind", "return");
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "values");
        cs2_json_write_expr_list(writer, insn->expression);
        cs2_json_put_char(writer, '}');
        return;

    default:
        cs2_json_fail(writer, "unstructured control flow survived reconstruction");
        return;
    }
}

/* -------------------------------------------------------------------------
 * Constructs
 * ---------------------------------------------------------------------- */

static void
cs2_json_write_construct(
    struct cs2_json_writer* writer,
    struct RSCache_CS2_Construct* construct);

static void
cs2_json_write_next(struct cs2_json_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_json_key(writer, "next");
    cs2_json_write_construct(writer, construct->next);
}

static void
cs2_json_write_seq(struct cs2_json_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_json_field_string(writer, "kind", "seq");
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "instructions");
    cs2_json_put_char(writer, '[');
    for( int i = 0; i < construct->instructions.count; i++ )
    {
        if( i )
            cs2_json_put_char(writer, ',');
        cs2_json_write_insn(writer, (struct RSCache_CS2_Insn*)construct->instructions.items[i]);
        if( writer->failed )
            return;
    }
    cs2_json_put_char(writer, ']');
    cs2_json_put_char(writer, ',');
    cs2_json_write_next(writer, construct);
}

static void
cs2_json_write_if(struct cs2_json_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_json_field_string(writer, "kind", "if");
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "branches");
    cs2_json_put_char(writer, '[');
    for( int i = 0; i < construct->branch_conditions.count; i++ )
    {
        if( i )
            cs2_json_put_char(writer, ',');
        cs2_json_put_char(writer, '{');
        cs2_json_key(writer, "condition");
        cs2_json_write_expr(
            writer, (struct RSCache_CS2_Expr*)construct->branch_conditions.items[i]);
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "body");
        cs2_json_write_construct(
            writer, (struct RSCache_CS2_Construct*)construct->branch_bodies.items[i]);
        cs2_json_put_char(writer, '}');
        if( writer->failed )
            return;
    }
    cs2_json_put_char(writer, ']');
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "otherwise");
    cs2_json_write_construct(writer, construct->otherwise);
    cs2_json_put_char(writer, ',');
    cs2_json_write_next(writer, construct);
}

static void
cs2_json_write_while(struct cs2_json_writer* writer, struct RSCache_CS2_Construct* construct)
{
    cs2_json_field_string(writer, "kind", "while");
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "condition");
    cs2_json_write_expr(writer, construct->condition);
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "body");
    cs2_json_write_construct(writer, construct->body);
    cs2_json_put_char(writer, ',');
    cs2_json_write_next(writer, construct);
}

static void
cs2_json_write_switch(struct cs2_json_writer* writer, struct RSCache_CS2_Construct* construct)
{
    /* The subject's solved type decides how the case keys are spelled, exactly
     * as it decides `switch_<type>` in source. */
    struct RSCache_CS2_TypingList* typings =
        RSCache_CS2_TypingsOfExpr(&writer->fs->typings, construct->expression);
    if( typings->count != 1 )
    {
        cs2_json_fail(writer, "switch subject does not produce exactly one value");
        return;
    }
    struct RSCache_CS2_Typing* typing = typings->items[0];

    cs2_json_field_string(writer, "kind", "switch");
    cs2_json_put_char(writer, ',');
    cs2_json_field_string(
        writer, "type",
        typing->type == RSCACHE_CS2_TYPE_NONE ? "int" : RSCache_CS2_TypeLiteral(typing->type));
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "expression");
    cs2_json_write_expr(writer, construct->expression);
    cs2_json_put_char(writer, ',');

    cs2_json_key(writer, "cases");
    cs2_json_put_char(writer, '[');
    for( int i = 0; i < construct->case_keys.count; i++ )
    {
        struct RSCache_CS2_IntVec* keys =
            (struct RSCache_CS2_IntVec*)construct->case_keys.items[i];
        if( i )
            cs2_json_put_char(writer, ',');
        cs2_json_put_char(writer, '{');
        cs2_json_key(writer, "keys");
        cs2_json_put_char(writer, '[');
        for( int j = 0; j < keys->count; j++ )
        {
            if( j )
                cs2_json_put_char(writer, ',');
            cs2_json_put_int(writer, keys->items[j]);
        }
        cs2_json_put_char(writer, ']');
        cs2_json_put_char(writer, ',');
        /* The source spellings alongside the raw keys, so a generated listing
         * can label a case without a names table of its own. */
        cs2_json_key(writer, "literals");
        cs2_json_put_char(writer, '[');
        for( int j = 0; j < keys->count; j++ )
        {
            if( j )
                cs2_json_put_char(writer, ',');
            char text[512];
            if( RSCache_CS2_NamesFormatInt(
                    writer->names, keys->items[j], typing->type, typing->identifier, text,
                    (int)sizeof(text)) )
                cs2_json_put_string(writer, text);
            else
                cs2_json_put(writer, "null");
        }
        cs2_json_put_char(writer, ']');
        cs2_json_put_char(writer, ',');
        cs2_json_key(writer, "body");
        cs2_json_write_construct(
            writer, (struct RSCache_CS2_Construct*)construct->case_bodies.items[i]);
        cs2_json_put_char(writer, '}');
        if( writer->failed )
            return;
    }
    cs2_json_put_char(writer, ']');
    cs2_json_put_char(writer, ',');
    cs2_json_key(writer, "default");
    cs2_json_write_construct(writer, construct->default_case);
    cs2_json_put_char(writer, ',');
    cs2_json_write_next(writer, construct);
}

static void
cs2_json_write_construct(
    struct cs2_json_writer* writer,
    struct RSCache_CS2_Construct* construct)
{
    if( writer->failed )
        return;
    if( !construct )
    {
        cs2_json_put(writer, "null");
        return;
    }
    cs2_json_put_char(writer, '{');
    switch( construct->kind )
    {
    case RSCACHE_CS2_CONSTRUCT_SEQ:
        cs2_json_write_seq(writer, construct);
        break;
    case RSCACHE_CS2_CONSTRUCT_IF:
        cs2_json_write_if(writer, construct);
        break;
    case RSCACHE_CS2_CONSTRUCT_WHILE:
        cs2_json_write_while(writer, construct);
        break;
    case RSCACHE_CS2_CONSTRUCT_SWITCH:
        cs2_json_write_switch(writer, construct);
        break;
    default:
        cs2_json_fail(writer, "unknown construct");
        return;
    }
    cs2_json_put_char(writer, '}');
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_GenerateJson(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    struct RSCache_CS2_Construct* root,
    const char* script_name,
    const struct RSCache_CS2_Names* names,
    struct RSCache_CS2_StrBuf* out,
    char* error,
    int error_capacity)
{
    assert(fs);
    assert(function);
    assert(script_name);
    assert(out);

    struct cs2_json_writer writer;
    memset(&writer, 0, sizeof(writer));
    writer.fs = fs;
    writer.function = function;
    writer.names = names;
    writer.out = out;
    writer.error = error;
    writer.error_capacity = error_capacity;

    struct RSCache_CS2_TypingList* returns = NULL;
    if( function->return_type_count > 0 )
        returns = RSCache_CS2_TypingsReturns(
            &fs->typings, function->id, function->return_types, function->return_type_count);

    cs2_json_put_char(&writer, '{');
    cs2_json_field_string(&writer, "schema", RSCACHE_CS2_AST_SCHEMA);
    cs2_json_put_char(&writer, ',');
    cs2_json_field_int(&writer, "id", function->id);
    cs2_json_put_char(&writer, ',');
    /* The full `[trigger,name]` heading, unsplit: it is the identity a content
     * tree files the script under. */
    cs2_json_field_string(&writer, "name", script_name);
    cs2_json_put_char(&writer, ',');

    cs2_json_key(&writer, "arguments");
    cs2_json_put_char(&writer, '[');
    for( int i = 0; i < function->arguments.count; i++ )
    {
        if( i )
            cs2_json_put_char(&writer, ',');
        cs2_json_write_variable(
            &writer, (struct RSCache_CS2_Variable*)function->arguments.items[i]);
        if( writer.failed )
            return false;
    }
    cs2_json_put_char(&writer, ']');
    cs2_json_put_char(&writer, ',');

    cs2_json_key(&writer, "returns");
    cs2_json_put_char(&writer, '[');
    for( int i = 0; returns && i < returns->count; i++ )
    {
        if( i )
            cs2_json_put_char(&writer, ',');
        cs2_json_put_char(&writer, '{');
        cs2_json_field_string(
            &writer, "type",
            returns->items[i]->type == RSCACHE_CS2_TYPE_NONE
                ? cs2_json_stack_type_name(returns->items[i]->stack_type)
                : RSCache_CS2_TypeLiteral(returns->items[i]->type));
        cs2_json_put_char(&writer, ',');
        cs2_json_field_string(
            &writer, "stackType", cs2_json_stack_type_name(returns->items[i]->stack_type));
        cs2_json_put_char(&writer, '}');
    }
    cs2_json_put_char(&writer, ']');
    cs2_json_put_char(&writer, ',');

    /*
     * The original frame shape.
     *
     * Structured source can drop it (the generator re-derives locals from
     * declarations), but a back end that has to interoperate with the packed
     * script — a differential run against the bytecode interpreter, say —
     * needs the counts the cache actually holds.
     */
    cs2_json_key(&writer, "frame");
    cs2_json_put_char(&writer, '{');
    cs2_json_field_int(&writer, "localInts", function->original_local_int_count);
    cs2_json_put_char(&writer, ',');
    cs2_json_field_int(&writer, "localStrings", function->original_local_string_count);
    cs2_json_put_char(&writer, ',');
    cs2_json_field_int(&writer, "intArguments", function->original_int_argument_count);
    cs2_json_put_char(&writer, ',');
    cs2_json_field_int(&writer, "stringArguments", function->original_string_argument_count);
    cs2_json_put_char(&writer, '}');
    cs2_json_put_char(&writer, ',');

    cs2_json_key(&writer, "body");
    cs2_json_write_construct(&writer, root);
    cs2_json_put_char(&writer, '}');

    return !writer.failed;
}
