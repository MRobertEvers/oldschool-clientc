#include "cs2_interp.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Return types
 * ---------------------------------------------------------------------- */

int
RSCache_CS2_ScriptReturnTypes(
    const struct RSCache_CS2_Script* script,
    enum RSCache_CS2_StackType* out,
    int capacity)
{
    /* What a script returns is not declared anywhere: it is read off the
     * constant pushes immediately before the closing `return`, walking
     * backwards until something that is not a push. Counted first, then filled
     * forwards, so nothing here imposes a ceiling of its own. */
    int first = script->op_count - 1;
    for( int i = script->op_count - 2; i >= 0; i-- )
    {
        int opcode = script->opcodes[i];
        if( opcode != RSCACHE_CS2_OP_PUSH_CONSTANT_INT &&
            opcode != RSCACHE_CS2_OP_PUSH_CONSTANT_STRING )
            break;
        first = i;
    }

    int count = script->op_count - 1 - first;
    for( int i = 0; i < count && i < capacity; i++ )
        out[i] = script->opcodes[first + i] == RSCACHE_CS2_OP_PUSH_CONSTANT_STRING
                     ? RSCACHE_CS2_STACK_STRING
                     : RSCACHE_CS2_STACK_INT;
    return count;
}

/* -------------------------------------------------------------------------
 * Interpreter state
 * ---------------------------------------------------------------------- */

#define CS2_INTERP_MAX_STACK_TYPES 256
/* Array slots are a script-local index; the client allocates few, but nothing
 * in the format caps them, so an out-of-range slot falls back to int. */
#define CS2_INTERP_MAX_ARRAYS 256

struct cs2_interp
{
    struct RSCache_CS2_FunctionSet* fs;
    const struct RSCache_CS2_DecompileOptions* options;

    int script_id;
    const struct RSCache_CS2_Script* script;
    enum RSCache_CS2_StackType return_types[CS2_INTERP_MAX_STACK_TYPES];
    int return_type_count;

    int pc;
    /* Element ACCESS expressions, bottom-first. */
    struct RSCache_CS2_Vec stack;
    int stack_counter;

    /*
     * Which stack each array slot's elements live on, or -1 for "not said
     * here".
     *
     * `push_array_int` and `pop_array_int` are named for the int case and the
     * reference hardcodes it, but the element type is `define_array`'s operand
     * and it is not always `int`: 27 of osrs239's scripts store `join_string`
     * results into an array of strings.
     *
     * The definition is often not in this script at all — arrays outlive a
     * `gosub`, so a proc stores into one its caller defined and the decompiler,
     * which works one script at a time, never sees the `define_array`. So the
     * store falls back to the type of the value actually on the stack. That
     * cannot desynchronise anything: `pop_array_int` takes one value whichever
     * stack it is on, and popping it as an int was the only thing making it a
     * refusal.
     */
    int array_stack[CS2_INTERP_MAX_ARRAYS];

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_fail(struct cs2_interp* interp, const char* fmt, ...)
{
    if( interp->failed )
        return;
    interp->failed = true;
    if( !interp->error || interp->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(interp->error, (size_t)interp->error_capacity, fmt, args);
    va_end(args);
}

static struct RSCache_CS2_Arena*
cs2_arena(struct cs2_interp* interp)
{
    return &interp->fs->arena;
}

static struct RSCache_CS2_Typings*
cs2_typings(struct cs2_interp* interp)
{
    return &interp->fs->typings;
}

static int
cs2_opcode(struct cs2_interp* interp)
{
    return interp->script->opcodes[interp->pc];
}

/** The element stack `define_array` recorded for a slot, or -1 if it did not. */
static int
cs2_array_stack(struct cs2_interp* interp, int slot)
{
    if( slot < 0 || slot >= CS2_INTERP_MAX_ARRAYS )
        return -1;
    return interp->array_stack[slot];
}

static int
cs2_operand_int(struct cs2_interp* interp)
{
    return interp->script->int_operands[interp->pc];
}

static const char*
cs2_operand_string(struct cs2_interp* interp)
{
    const char* s = interp->script->string_operands
                        ? interp->script->string_operands[interp->pc]
                        : NULL;
    return s ? s : "";
}

static struct RSCache_CS2_Expr*
cs2_push(
    struct cs2_interp* interp,
    enum RSCache_CS2_StackType stack_type,
    const struct RSCache_CS2_Value* value)
{
    enum RSCache_CS2_VarKind kind = stack_type == RSCACHE_CS2_STACK_INT
                                        ? RSCACHE_CS2_VAR_STACKINT
                                        : RSCACHE_CS2_VAR_STACKSTRING;
    struct RSCache_CS2_Variable* variable =
        RSCache_CS2_VarIntern(interp->fs, kind, interp->script_id, interp->stack_counter++);
    struct RSCache_CS2_Expr* access = RSCache_CS2_ExprAccess(cs2_arena(interp), variable, value);
    RSCache_CS2_VecPush(&interp->stack, access);
    return access;
}

static void
cs2_push_types(
    struct cs2_interp* interp,
    const enum RSCache_CS2_StackType* stack_types,
    int count,
    struct RSCache_CS2_Expr** out)
{
    for( int i = 0; i < count; i++ )
        out[i] = cs2_push(interp, stack_types[i], NULL);
}

static struct RSCache_CS2_Expr*
cs2_pop(struct cs2_interp* interp, enum RSCache_CS2_StackType stack_type)
{
    if( interp->stack.count == 0 )
    {
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d underflowed the operand stack",
            interp->script_id,
            interp->pc,
            cs2_opcode(interp));
        return RSCache_CS2_ExprConstantInt(cs2_arena(interp), 0);
    }
    struct RSCache_CS2_Expr* top =
        (struct RSCache_CS2_Expr*)interp->stack.items[--interp->stack.count];
    if( RSCache_CS2_VarStackType(top->variable->kind) != stack_type )
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d expected a %s on the operand stack",
            interp->script_id,
            interp->pc,
            cs2_opcode(interp),
            stack_type == RSCACHE_CS2_STACK_INT ? "int" : "string");
    return top;
}

/** Pops `count` values, returning them in the order they were pushed. */
static void
cs2_pop_many(struct cs2_interp* interp, int count, struct RSCache_CS2_Expr** out)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        if( interp->stack.count == 0 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d underflowed the operand stack",
                interp->script_id,
                interp->pc,
                cs2_opcode(interp));
            out[i] = RSCache_CS2_ExprConstantInt(cs2_arena(interp), 0);
            continue;
        }
        out[i] = (struct RSCache_CS2_Expr*)interp->stack.items[--interp->stack.count];
    }
}

/** The constant `depth` slots below the top, or NULL if that slot is not one. */
static const struct RSCache_CS2_Value*
cs2_peek_value_at(struct cs2_interp* interp, int depth)
{
    if( interp->stack.count <= depth )
        return NULL;
    struct RSCache_CS2_Expr* slot =
        (struct RSCache_CS2_Expr*)interp->stack.items[interp->stack.count - 1 - depth];
    return slot->has_value ? &slot->value : NULL;
}

static const struct RSCache_CS2_Value*
cs2_peek_value(struct cs2_interp* interp)
{
    return cs2_peek_value_at(interp, 0);
}

/** The stack an operand occupies, whether or not it is a constant. */
static enum RSCache_CS2_StackType
cs2_peek_stack_type_at(struct cs2_interp* interp, int depth)
{
    if( interp->stack.count <= depth )
        return RSCACHE_CS2_STACK_INT;
    struct RSCache_CS2_Expr* slot =
        (struct RSCache_CS2_Expr*)interp->stack.items[interp->stack.count - 1 - depth];
    return RSCache_CS2_VarStackType(slot->variable->kind);
}

/* -------------------------------------------------------------------------
 * Typing helpers
 * ---------------------------------------------------------------------- */

static void
cs2_assign_expr_to_protos(
    struct cs2_interp* interp,
    struct RSCache_CS2_Expr* expr,
    const enum RSCache_CS2_ProtoId* protos,
    int count)
{
    struct RSCache_CS2_TypingList* from = RSCache_CS2_TypingsOfExpr(cs2_typings(interp), expr);
    struct RSCache_CS2_TypingList* to =
        RSCache_CS2_TypingsOfProtos(cs2_typings(interp), protos, count);
    if( from->count != to->count )
    {
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d wanted %d arguments, stack gave %d",
            interp->script_id,
            interp->pc,
            cs2_opcode(interp),
            to->count,
            from->count);
        return;
    }
    /* `CS2_TRACE_PROTOS=1` names the command behind each type constraint, which is
     * the only way to find which signature a type contradiction came from. Read
     * once: this is on the path of every argument of every command. */
    static int trace = -1;
    if( trace < 0 )
        trace = getenv("CS2_TRACE_PROTOS") != NULL;
    if( trace )
    {
        for( int i = 0; i < count; i++ )
        {
            const struct RSCache_CS2_Prototype* proto = RSCache_CS2_ProtoGet(protos[i]);
            const char* literal = proto && proto->type != RSCACHE_CS2_TYPE_NONE
                                      ? RSCache_CS2_TypeLiteral(proto->type)
                                      : "?";
            fprintf(stderr, "PROTO script=%d pc=%d op=%d (%s) arg%d=%s\n", interp->script_id,
                    interp->pc, cs2_opcode(interp),
                    RSCache_CS2_CommandName(cs2_opcode(interp))
                        ? RSCache_CS2_CommandName(cs2_opcode(interp))
                        : "?",
                    i, literal);
        }
    }
    if( !RSCache_CS2_TypingAssignLists(from, to) )
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d was given an argument of the wrong stack type",
            interp->script_id,
            interp->pc,
            cs2_opcode(interp));
}

static void
cs2_assign_element_to_proto(
    struct cs2_interp* interp,
    struct RSCache_CS2_Expr* element,
    enum RSCache_CS2_ProtoId proto)
{
    if( getenv("CS2_TRACE_PROTOS") )
    {
        const struct RSCache_CS2_Prototype* p = RSCache_CS2_ProtoGet(proto);
        fprintf(stderr, "PROTO1 script=%d pc=%d op=%d (%s) -> %s\n", interp->script_id,
                interp->pc, cs2_opcode(interp),
                RSCache_CS2_CommandName(cs2_opcode(interp))
                    ? RSCache_CS2_CommandName(cs2_opcode(interp)) : "?",
                p && p->type != RSCACHE_CS2_TYPE_NONE ? RSCache_CS2_TypeLiteral(p->type) : "?");
    }
    struct RSCache_CS2_Typing* from = RSCache_CS2_TypingsOfElement(cs2_typings(interp), element);
    struct RSCache_CS2_Typing* to = RSCache_CS2_TypingsOfProto(cs2_typings(interp), proto);
    if( from != to && !RSCache_CS2_TypingAssign(from, to) )
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d was given a %s where it wanted a %s",
            interp->script_id,
            interp->pc,
            cs2_opcode(interp),
            from->stack_type == RSCACHE_CS2_STACK_INT ? "int" : "string",
            to->stack_type == RSCACHE_CS2_STACK_INT ? "int" : "string");
}

/* -------------------------------------------------------------------------
 * Per-command translation
 * ---------------------------------------------------------------------- */

static struct RSCache_CS2_Insn*
cs2_translate_assign(struct cs2_interp* interp, int opcode)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);
    struct RSCache_CS2_Expr* definitions = NULL;
    struct RSCache_CS2_Expr* expression = NULL;
    int operand = cs2_operand_int(interp);

    switch( opcode )
    {
    case RSCACHE_CS2_OP_PUSH_CONSTANT_INT:
    {
        struct RSCache_CS2_Value value = { RSCACHE_CS2_STACK_INT, operand, NULL };
        definitions = cs2_push(interp, RSCACHE_CS2_STACK_INT, &value);
        expression = RSCache_CS2_ExprConstantInt(arena, operand);
        break;
    }
    case RSCACHE_CS2_OP_PUSH_CONSTANT_STRING:
    {
        const char* text = cs2_operand_string(interp);
        struct RSCache_CS2_Value value = { RSCACHE_CS2_STACK_STRING, 0, text };
        definitions = cs2_push(interp, RSCACHE_CS2_STACK_STRING, &value);
        expression = RSCache_CS2_ExprConstantString(arena, text);
        break;
    }
#define CS2_PUSH_VAR_CASE(op, kind, stack)                                                    \
    case op:                                                                                  \
        definitions = cs2_push(interp, stack, NULL);                                          \
        expression = RSCache_CS2_ExprAccess(                                                  \
            arena, RSCache_CS2_VarIntern(interp->fs, kind, 0, operand), NULL);                \
        break;
#define CS2_POP_VAR_CASE(op, kind, stack)                                                     \
    case op:                                                                                  \
        definitions = RSCache_CS2_ExprAccess(                                                 \
            arena, RSCache_CS2_VarIntern(interp->fs, kind, 0, operand), NULL);                \
        expression = cs2_pop(interp, stack);                                                  \
        break;

        CS2_PUSH_VAR_CASE(RSCACHE_CS2_OP_PUSH_VAR, RSCACHE_CS2_VAR_VARP, RSCACHE_CS2_STACK_INT)
        CS2_POP_VAR_CASE(RSCACHE_CS2_OP_POP_VAR, RSCACHE_CS2_VAR_VARP, RSCACHE_CS2_STACK_INT)
        CS2_PUSH_VAR_CASE(
            RSCACHE_CS2_OP_PUSH_VARBIT, RSCACHE_CS2_VAR_VARBIT, RSCACHE_CS2_STACK_INT)
        CS2_POP_VAR_CASE(RSCACHE_CS2_OP_POP_VARBIT, RSCACHE_CS2_VAR_VARBIT, RSCACHE_CS2_STACK_INT)
        CS2_PUSH_VAR_CASE(
            RSCACHE_CS2_OP_PUSH_VARC_INT, RSCACHE_CS2_VAR_VARCINT, RSCACHE_CS2_STACK_INT)
        CS2_POP_VAR_CASE(
            RSCACHE_CS2_OP_POP_VARC_INT, RSCACHE_CS2_VAR_VARCINT, RSCACHE_CS2_STACK_INT)
        CS2_PUSH_VAR_CASE(
            RSCACHE_CS2_OP_PUSH_VARC_STRING, RSCACHE_CS2_VAR_VARCSTRING, RSCACHE_CS2_STACK_STRING)
        CS2_POP_VAR_CASE(
            RSCACHE_CS2_OP_POP_VARC_STRING, RSCACHE_CS2_VAR_VARCSTRING, RSCACHE_CS2_STACK_STRING)
        CS2_PUSH_VAR_CASE(
            RSCACHE_CS2_OP_PUSH_VARCLANSETTING,
            RSCACHE_CS2_VAR_VARCLANSETTING,
            RSCACHE_CS2_STACK_INT)
        CS2_PUSH_VAR_CASE(
            RSCACHE_CS2_OP_PUSH_VARCLAN, RSCACHE_CS2_VAR_VARCLAN, RSCACHE_CS2_STACK_INT)
#undef CS2_PUSH_VAR_CASE
#undef CS2_POP_VAR_CASE

    case RSCACHE_CS2_OP_PUSH_INT_LOCAL:
        definitions = cs2_push(interp, RSCACHE_CS2_STACK_INT, NULL);
        expression = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_INT, interp->script_id, operand),
            NULL);
        break;
    case RSCACHE_CS2_OP_POP_INT_LOCAL:
        definitions = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_INT, interp->script_id, operand),
            NULL);
        expression = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        break;
    case RSCACHE_CS2_OP_PUSH_STRING_LOCAL:
        definitions = cs2_push(interp, RSCACHE_CS2_STACK_STRING, NULL);
        expression = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_STRING, interp->script_id, operand),
            NULL);
        break;
    case RSCACHE_CS2_OP_POP_STRING_LOCAL:
        definitions = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_STRING, interp->script_id, operand),
            NULL);
        expression = cs2_pop(interp, RSCACHE_CS2_STACK_STRING);
        break;
    default:
        cs2_fail(interp, "script %d pc %d: opcode %d is not an assignment",
                 interp->script_id, interp->pc, opcode);
        return NULL;
    }

    if( !RSCache_CS2_TypingAssign(
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), expression),
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), definitions)) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d assigns across stack types",
            interp->script_id,
            interp->pc,
            opcode);
        return NULL;
    }
    return RSCache_CS2_InsnAssignment(arena, definitions, expression);
}

static struct RSCache_CS2_Insn*
cs2_translate_basic(
    struct cs2_interp* interp,
    int opcode,
    const struct RSCache_CS2_CommandInfo* info)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    bool dot = false;
    if( info->dot_capable )
    {
        int operand = cs2_operand_int(interp);
        if( operand != 0 && operand != 1 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d active-form flag was %d, not a boolean",
                interp->script_id,
                interp->pc,
                opcode,
                operand);
            return NULL;
        }
        dot = operand == 1;
    }

    struct RSCache_CS2_Expr* popped[64];
    if( info->arg_count > (int)(sizeof(popped) / sizeof(popped[0])) )
    {
        cs2_fail(interp, "script %d pc %d: opcode %d takes too many arguments",
                 interp->script_id, interp->pc, opcode);
        return NULL;
    }
    cs2_pop_many(interp, info->arg_count, popped);
    struct RSCache_CS2_Expr* arguments =
        RSCache_CS2_ExprFromList(arena, popped, info->arg_count);

    enum RSCache_CS2_ProtoId arg_protos[64];
    for( int i = 0; i < info->arg_count; i++ )
        arg_protos[i] = RSCache_CS2_CommandArg(info, i);
    cs2_assign_expr_to_protos(interp, arguments, arg_protos, info->arg_count);

    enum RSCache_CS2_StackType def_stack_types[64];
    enum RSCache_CS2_ProtoId def_protos[64];
    for( int i = 0; i < info->def_count; i++ )
    {
        def_protos[i] = RSCache_CS2_CommandDef(info, i);
        def_stack_types[i] =
            RSCache_CS2_TypeStackType(RSCache_CS2_ProtoGet(def_protos[i])->type);
    }

    struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
        arena, def_stack_types, info->def_count, opcode, arguments, dot);

    struct RSCache_CS2_TypingList* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation);
    for( int i = 0; i < info->def_count; i++ )
    {
        if( RSCache_CS2_TypingFreezeProto(operation_typing->items[i], def_protos[i]) )
            continue;
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d result %d does not match its recorded type",
            interp->script_id,
            interp->pc,
            opcode,
            i);
        return NULL;
    }

    struct RSCache_CS2_Expr* pushed[64];
    cs2_push_types(interp, def_stack_types, info->def_count, pushed);
    struct RSCache_CS2_Expr* definitions =
        RSCache_CS2_ExprFromList(arena, pushed, info->def_count);

    if( !RSCache_CS2_TypingAssignLists(
            operation_typing, RSCache_CS2_TypingsOfExpr(cs2_typings(interp), definitions)) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d results do not match what it pushed",
            interp->script_id,
            interp->pc,
            opcode);
        return NULL;
    }
    return RSCache_CS2_InsnAssignment(arena, definitions, operation);
}

/**
 * Commands which call Statics.method6560 in the rev-239 client have one value
 * whose physical stack is selected by the final base-type argument: 0 is int,
 * 1 is long and 2 is string. The source language currently models the client's
 * int and string banks, so a long selector is reported rather than silently
 * consuming an int. LOCAL_BASIC marks the selected slot with PROTO_NONE.
 */
static struct RSCache_CS2_Insn*
cs2_translate_typed_pop(
    struct cs2_interp* interp,
    int opcode,
    const struct RSCache_CS2_CommandInfo* info)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    bool dot = false;
    if( info->dot_capable )
    {
        int operand = cs2_operand_int(interp);
        if( operand != 0 && operand != 1 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d active-form flag was %d, not a boolean",
                interp->script_id,
                interp->pc,
                opcode,
                operand);
            return NULL;
        }
        dot = operand == 1;
    }

    if( info->arg_count < 2 || info->arg_count > 64 )
    {
        cs2_fail(
            interp, "script %d pc %d: opcode %d has an invalid typed-pop signature",
            interp->script_id, interp->pc, opcode);
        return NULL;
    }

    int dynamic_index = -1;
    for( int i = 0; i < info->arg_count; i++ )
    {
        if( RSCache_CS2_CommandArg(info, i) != RSCACHE_CS2_PROTO_NONE )
            continue;
        if( dynamic_index >= 0 )
        {
            cs2_fail(
                interp, "script %d pc %d: opcode %d has multiple typed-pop arguments",
                interp->script_id, interp->pc, opcode);
            return NULL;
        }
        dynamic_index = i;
    }
    if( dynamic_index < 0 )
    {
        cs2_fail(
            interp, "script %d pc %d: opcode %d has no typed-pop argument",
            interp->script_id, interp->pc, opcode);
        return NULL;
    }

    const struct RSCache_CS2_Value* selector = cs2_peek_value(interp);
    enum RSCache_CS2_Type dynamic_type = RSCACHE_CS2_TYPE_INT;
    if( selector && selector->stack_type == RSCACHE_CS2_STACK_INT )
    {
        if( selector->int_value == 2 )
            dynamic_type = RSCACHE_CS2_TYPE_STRING;
        else if( selector->int_value != 0 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d uses unsupported base-type selector %d",
                interp->script_id,
                interp->pc,
                opcode,
                selector->int_value);
            return NULL;
        }
    }
    else if( !selector )
    {
        int depth = info->arg_count - 1 - dynamic_index;
        dynamic_type = cs2_peek_stack_type_at(interp, depth) == RSCACHE_CS2_STACK_STRING
                           ? RSCACHE_CS2_TYPE_STRING
                           : RSCACHE_CS2_TYPE_INT;
    }
    else
    {
        cs2_fail(
            interp, "script %d pc %d: opcode %d base-type selector is not an int",
            interp->script_id, interp->pc, opcode);
        return NULL;
    }

    struct RSCache_CS2_Expr* popped[64];
    cs2_pop_many(interp, info->arg_count, popped);
    struct RSCache_CS2_Expr* arguments =
        RSCache_CS2_ExprFromList(arena, popped, info->arg_count);
    for( int i = 0; i < info->arg_count && !interp->failed; i++ )
    {
        enum RSCache_CS2_ProtoId proto = RSCache_CS2_CommandArg(info, i);
        if( proto == RSCACHE_CS2_PROTO_NONE )
        {
            if( !RSCache_CS2_TypingFreezeType(
                    RSCache_CS2_TypingsOfElement(cs2_typings(interp), popped[i]), dynamic_type) )
                cs2_fail(
                    interp,
                    "script %d pc %d: opcode %d selected the wrong value stack",
                    interp->script_id,
                    interp->pc,
                    opcode);
        }
        else
        {
            cs2_assign_element_to_proto(interp, popped[i], proto);
        }
    }
    if( interp->failed )
        return NULL;

    enum RSCache_CS2_StackType def_stack_types[64];
    enum RSCache_CS2_ProtoId def_protos[64];
    for( int i = 0; i < info->def_count; i++ )
    {
        def_protos[i] = RSCache_CS2_CommandDef(info, i);
        def_stack_types[i] =
            RSCache_CS2_TypeStackType(RSCache_CS2_ProtoGet(def_protos[i])->type);
    }

    struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
        arena, def_stack_types, info->def_count, opcode, arguments, dot);
    struct RSCache_CS2_TypingList* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation);
    for( int i = 0; i < info->def_count; i++ )
    {
        if( !RSCache_CS2_TypingFreezeProto(operation_typing->items[i], def_protos[i]) )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d result %d does not match its recorded type",
                interp->script_id,
                interp->pc,
                opcode,
                i);
            return NULL;
        }
    }

    struct RSCache_CS2_Expr* pushed[64];
    cs2_push_types(interp, def_stack_types, info->def_count, pushed);
    struct RSCache_CS2_Expr* definitions =
        RSCache_CS2_ExprFromList(arena, pushed, info->def_count);
    if( !RSCache_CS2_TypingAssignLists(
            operation_typing, RSCache_CS2_TypingsOfExpr(cs2_typings(interp), definitions)) )
    {
        cs2_fail(
            interp, "script %d pc %d: opcode %d results do not match what it pushed",
            interp->script_id, interp->pc, opcode);
        return NULL;
    }
    return RSCache_CS2_InsnAssignment(arena, definitions, operation);
}

/**
 * Parse a hook's argument descriptor.
 *
 * The descriptor is the type letter of each argument in order, optionally
 * followed by 'Y' to say the hook also carries a list of things to re-fire on.
 */
struct cs2_hook_desc
{
    enum RSCache_CS2_Type types[64];
    int count;
    bool triggers;
};

static bool
cs2_parse_hook_desc(const char* desc, struct cs2_hook_desc* out)
{
    if( getenv("CS2_TRACE_PROTOS") )
        fprintf(stderr, "HOOKDESC \"%s\"\n", desc ? desc : "(null)");
    memset(out, 0, sizeof(*out));
    size_t len = desc ? strlen(desc) : 0;
    out->triggers = len > 0 && desc[len - 1] == 'Y';
    for( size_t i = 0; i < len; i++ )
    {
        if( out->triggers && i == len - 1 )
            break;
        if( out->count == (int)(sizeof(out->types) / sizeof(out->types[0])) )
            return false;
        enum RSCache_CS2_Type type = RSCache_CS2_TypeOfDesc((uint8_t)desc[i]);
        if( type == RSCACHE_CS2_TYPE_NONE )
            return false;
        out->types[out->count++] = type;
    }
    return true;
}

static struct RSCache_CS2_Insn*
cs2_translate_clientscript(struct cs2_interp* interp, int opcode)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    /* The if_* family names its target component explicitly; the cc_* family
     * acts on the active component and uses its operand for the "." form. */
    struct RSCache_CS2_Expr* component = NULL;
    bool dot = false;
    if( opcode >= RSCACHE_CS2_OP_IF_HOOK_BASE )
    {
        component = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        cs2_assign_element_to_proto(interp, component, RSCACHE_CS2_PROTO_COMPONENT);
    }
    else
    {
        int operand = cs2_operand_int(interp);
        if( operand != 0 && operand != 1 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d active-form flag was %d, not a boolean",
                interp->script_id,
                interp->pc,
                opcode,
                operand);
            return NULL;
        }
        dot = operand == 1;
    }

    const struct RSCache_CS2_Value* desc_value = cs2_peek_value(interp);
    if( !desc_value || desc_value->stack_type != RSCACHE_CS2_STACK_STRING )
    {
        cs2_fail(
            interp,
            "script %d pc %d: hook argument descriptor is not a literal string",
            interp->script_id,
            interp->pc);
        return NULL;
    }
    struct cs2_hook_desc desc;
    if( !cs2_parse_hook_desc(desc_value->string_value, &desc) )
    {
        {
            /* Name the byte, not the rendering of it: the descriptors are
             * windows-1252 and a terminal shows several of them identically. */
            const char* text = desc_value->string_value;
            int bad = 0;
            for( int i = 0; text[i]; i++ )
            {
                if( RSCache_CS2_TypeOfDesc((uint8_t)text[i]) == RSCACHE_CS2_TYPE_NONE &&
                    !(text[i + 1] == '\0' && text[i] == 'Y') )
                {
                    bad = (uint8_t)text[i];
                    break;
                }
            }
            cs2_fail(
                interp,
                "script %d pc %d: hook descriptor byte 0x%02x is not a known type",
                interp->script_id,
                interp->pc,
                bad);
        }
        return NULL;
    }
    cs2_pop(interp, RSCACHE_CS2_STACK_STRING);

    /* Sized to fit rather than capped: a vartransmit hook can watch a hundred
     * varps, and upstream has no ceiling here either. Allocated from the arena
     * once the count is read, so there is no cleanup on the error paths. */
    struct RSCache_CS2_Expr** triggers = NULL;
    int trigger_count = 0;
    if( desc.triggers )
    {
        const struct RSCache_CS2_Value* count_value = cs2_peek_value(interp);
        if( !count_value || count_value->stack_type != RSCACHE_CS2_STACK_INT )
        {
            cs2_fail(
                interp,
                "script %d pc %d: hook trigger count is not a literal int",
                interp->script_id,
                interp->pc);
            return NULL;
        }
        trigger_count = count_value->int_value;
        cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        if( trigger_count < 0 || trigger_count > interp->stack.count )
        {
            cs2_fail(
                interp,
                "script %d pc %d: hook claims %d triggers, only %d values are on the stack",
                interp->script_id,
                interp->pc,
                trigger_count,
                interp->stack.count);
            return NULL;
        }
        triggers = (struct RSCache_CS2_Expr**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)(trigger_count > 0 ? trigger_count : 1) * sizeof(*triggers));

        /* Triggers come off the stack top-first, so they are collected in
         * reverse and flipped below. */
        for( int i = 0; i < trigger_count; i++ )
        {
            struct RSCache_CS2_Expr* trigger;
            switch( opcode )
            {
            case RSCACHE_CS2_OP_CC_SETONSTATTRANSMIT:
            case RSCACHE_CS2_OP_IF_SETONSTATTRANSMIT:
                trigger = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
                cs2_assign_element_to_proto(interp, trigger, RSCACHE_CS2_PROTO_STAT);
                break;
            case RSCACHE_CS2_OP_CC_SETONINVTRANSMIT:
            case RSCACHE_CS2_OP_IF_SETONINVTRANSMIT:
                trigger = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
                cs2_assign_element_to_proto(interp, trigger, RSCACHE_CS2_PROTO_INV);
                break;
            case RSCACHE_CS2_OP_CC_SETONVARTRANSMIT:
            case RSCACHE_CS2_OP_IF_SETONVARTRANSMIT:
            {
                const struct RSCache_CS2_Value* varp = cs2_peek_value(interp);
                if( !varp || varp->stack_type != RSCACHE_CS2_STACK_INT )
                {
                    cs2_fail(
                        interp,
                        "script %d pc %d: vartransmit trigger is not a literal varp id",
                        interp->script_id,
                        interp->pc);
                    return NULL;
                }
                int varp_id = varp->int_value;
                cs2_pop(interp, RSCACHE_CS2_STACK_INT);
                trigger = RSCache_CS2_ExprPointer(
                    arena,
                    RSCache_CS2_VarIntern(interp->fs, RSCACHE_CS2_VAR_VARP, 0, varp_id));
                break;
            }
            default:
                cs2_fail(
                    interp,
                    "script %d pc %d: opcode %d carries triggers but has no trigger kind",
                    interp->script_id,
                    interp->pc,
                    opcode);
                return NULL;
            }
            triggers[trigger_count - 1 - i] = trigger;
        }
    }

    /* Hook arguments are also popped top-first. Any of them may be a magic
     * event value rather than a real one, which is recognised here because the
     * information is gone once the constant has been folded away. */
    struct RSCache_CS2_Expr* args[64];
    for( int i = desc.count - 1; i >= 0; i-- )
    {
        enum RSCache_CS2_EventProperty property =
            RSCache_CS2_EventPropertyOf(cs2_peek_value(interp));
        struct RSCache_CS2_Expr* popped =
            cs2_pop(interp, RSCache_CS2_TypeStackType(desc.types[i]));
        args[i] = property == RSCACHE_CS2_EVENT_NONE
                      ? popped
                      : RSCache_CS2_ExprEventProperty(arena, property);
    }

    const struct RSCache_CS2_Value* id_value = cs2_peek_value(interp);
    if( !id_value || id_value->stack_type != RSCACHE_CS2_STACK_INT )
    {
        cs2_fail(
            interp,
            "script %d pc %d: hook script id is not a literal int",
            interp->script_id,
            interp->pc);
        return NULL;
    }
    int hook_script_id = id_value->int_value;
    cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    RSCache_CS2_IntMapPut(
        &interp->fs->call_triggers,
        hook_script_id,
        (void*)(intptr_t)RSCACHE_CS2_TRIGGER_CLIENTSCRIPT);

    struct RSCache_CS2_Expr* args_expr = RSCache_CS2_ExprFromList(arena, args, desc.count);

    enum RSCache_CS2_StackType arg_stack_types[64];
    for( int i = 0; i < desc.count; i++ )
        arg_stack_types[i] = RSCache_CS2_TypeStackType(desc.types[i]);
    struct RSCache_CS2_TypingList* args_typing = RSCache_CS2_TypingsArgs(
        cs2_typings(interp), interp->fs, hook_script_id, arg_stack_types, desc.count);
    if( args_typing->count != desc.count )
    {
        cs2_fail(
            interp,
            "script %d pc %d: hook for script %d passes %d arguments, it takes %d",
            interp->script_id,
            interp->pc,
            hook_script_id,
            desc.count,
            args_typing->count);
        return NULL;
    }
    /*
     * Freeze the callee's parameter types from the descriptor — except `int`.
     *
     * A hook descriptor used to carry the real type of each argument, and the
     * reference freezes all of them. OldSchool has since stopped: the same script
     * 119 reads `I` (component) in the cache RuneStar decompiled and `i` (int) in
     * rev 239, with an unchanged body that hands the argument to if_sethide's
     * component slot. Freezing `int` there asserts a type the byte no longer
     * carries, and the contradiction it creates took out 1,102 of osrs239's
     * scripts — every `component/int`, `graphic/int`, `enum/int` failure.
     *
     * `int` is the one descriptor value that is now indistinguishable from "not
     * stated", so it is the one that does not freeze. The parameter is left for
     * the solver, which types it from the body's use of it, or falls back to
     * `int` at its last step if the body says nothing either. Every other
     * descriptor letter still means what it says and still freezes.
     */
    for( int i = 0; i < desc.count; i++ )
    {
        if( desc.types[i] == RSCACHE_CS2_TYPE_INT )
            continue;
        if( RSCache_CS2_TypingFreezeType(args_typing->items[i], desc.types[i]) )
            continue;
        cs2_fail(
            interp,
            "script %d pc %d: hook argument %d disagrees with script %d's own signature",
            interp->script_id,
            interp->pc,
            i,
            hook_script_id);
        return NULL;
    }

    struct RSCache_CS2_TypingList* from =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), args_expr);
    if( !RSCache_CS2_TypingAssignLists(from, args_typing) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: hook arguments do not match script %d's signature",
            interp->script_id,
            interp->pc,
            hook_script_id);
        return NULL;
    }

    struct RSCache_CS2_Expr* triggers_expr =
        RSCache_CS2_ExprFromList(arena, triggers, trigger_count);
    struct RSCache_CS2_Expr* clientscript = RSCache_CS2_ExprClientScript(
        arena, opcode, hook_script_id, args_expr, triggers_expr, dot, component);
    return RSCache_CS2_InsnAssignment(arena, RSCache_CS2_ExprEmpty(arena), clientscript);
}

/* -------------------------------------------------------------------------
 * The client-database family
 *
 * A `dbcolumn` literal is `(table << 12) | (column << 4) | (field + 1)`, and
 * field 0 means "the whole tuple". The +1 is not decorative: it is the only
 * thing that distinguishes "field 0 of this column" from "all of it", and it is
 * what makes 0xa6200 push four values where 0xa6204 pushes one.
 *
 * Established on cache.osrs239 by consumption. Every db_getfield whose column
 * literal ends in 0 is followed by exactly as many pops as the column has
 * fields (0xa6200 -> four ints at all 26 of its call sites, 0x170 -> two, 0x90
 * -> three), and every one ending in a non-zero nibble by exactly one (0xa6204
 * -> one int at all 11 of its). The two spellings name the same table and
 * column, so no fixed signature can serve both.
 * ---------------------------------------------------------------------- */

#define CS2_DB_MAX_FIELDS 32

/**
 * The field types a `dbcolumn` literal selects.
 *
 * Returns the count, or -1 when it cannot be resolved — an unknown table, or no
 * hook to ask. `-1` is a refusal, not an empty column: guessing the count is
 * guessing how many values the opcode leaves behind.
 */
static int
cs2_db_column_fields(
    struct cs2_interp* interp,
    int packed,
    enum RSCache_CS2_Type* out,
    int capacity)
{
    if( !interp->options->db_columns.load )
        return -1;

    int table_id = (packed >> 12) & 0xFFFFF;
    int column_id = (packed >> 4) & 0xFF;
    int field = (packed & 0xF) - 1;

    enum RSCache_CS2_Type types[CS2_DB_MAX_FIELDS];
    int count = interp->options->db_columns.load(
        interp->options->db_columns.user, table_id, column_id, types, CS2_DB_MAX_FIELDS);
    if( count <= 0 )
        return -1;

    if( field < 0 )
    {
        /* The whole tuple, in field order. */
        if( count > capacity )
            return -1;
        for( int i = 0; i < count; i++ )
            out[i] = types[i];
        return count;
    }
    if( field >= count || capacity < 1 )
        return -1;
    out[0] = types[field];
    return 1;
}

/** db_getfield(dbrow, dbcolumn, index) -> the column's fields. */
static struct RSCache_CS2_Insn*
cs2_translate_db_getfield(struct cs2_interp* interp, int opcode)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    /* Top to bottom: index, dbcolumn, dbrow. The column has to be a literal —
     * it is the operand that says what comes back. */
    const struct RSCache_CS2_Value* column_value = cs2_peek_value_at(interp, 1);
    enum RSCache_CS2_Type field_types[CS2_DB_MAX_FIELDS];
    int field_count = -1;
    if( column_value && column_value->stack_type == RSCACHE_CS2_STACK_INT )
        field_count = cs2_db_column_fields(
            interp, column_value->int_value, field_types, CS2_DB_MAX_FIELDS);
    if( field_count < 0 )
    {
        /* One int is what the client's stack table recorded, and it is right for
         * every single-field column. Keeping it as the fallback means a cache
         * with no dbtable to hand decompiles exactly as much as it used to. */
        field_count = 1;
        field_types[0] = RSCACHE_CS2_TYPE_INT;
    }

    struct RSCache_CS2_Expr* popped[3];
    cs2_pop_many(interp, 3, popped);
    struct RSCache_CS2_Expr* arguments = RSCache_CS2_ExprFromList(arena, popped, 3);
    /* The row argument is deliberately left untyped. Calling it a `dbrow` reads
     * well and is even true, but nothing else in the family claims that type —
     * db_findnext's result is a plain int in the recorded table — so freezing it
     * here contradicts wherever the row came from and refuses the script. It
     * bought a nicer identifier and cost 40 decompiles. */

    enum RSCache_CS2_StackType stack_types[CS2_DB_MAX_FIELDS];
    for( int i = 0; i < field_count; i++ )
        stack_types[i] = RSCache_CS2_TypeStackType(field_types[i]);

    struct RSCache_CS2_Expr* operation =
        RSCache_CS2_ExprOperation(arena, stack_types, field_count, opcode, arguments, false);
    struct RSCache_CS2_TypingList* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation);
    for( int i = 0; i < field_count; i++ )
    {
        if( RSCache_CS2_TypingFreezeType(operation_typing->items[i], field_types[i]) )
            continue;
        cs2_fail(
            interp,
            "script %d pc %d: db_getfield field %d's type does not match the stack slot",
            interp->script_id,
            interp->pc,
            i);
        return NULL;
    }

    struct RSCache_CS2_Expr* pushed[CS2_DB_MAX_FIELDS];
    cs2_push_types(interp, stack_types, field_count, pushed);
    struct RSCache_CS2_Expr* definitions =
        RSCache_CS2_ExprFromList(arena, pushed, field_count);
    RSCache_CS2_TypingAssignLists(
        operation_typing, RSCache_CS2_TypingsOfExpr(cs2_typings(interp), definitions));
    return RSCache_CS2_InsnAssignment(arena, definitions, operation);
}

/**
 * db_find(dbcolumn, value, basetype) and its three siblings.
 *
 * Three arguments, not the two the client's stack table recorded, and the third
 * is not a mystery: it is a **base-type selector** saying which stack the search
 * value came off. The client pops it first and runs `popValueOfType` on it — 0
 * is int, 1 long, 2 string — then pops the value from that stack, then the
 * dbcolumn. All 142 call sites in cache.osrs239 push it as the literal 0.
 *
 * The `_with_count` pair leaves the match count behind; the other two leave
 * nothing and are read with `db_findnext`.
 */
static struct RSCache_CS2_Insn*
cs2_translate_db_find(struct cs2_interp* interp, int opcode)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    bool with_count = opcode == RSCACHE_CS2_OP_DB_FIND_WITH_COUNT ||
                      opcode == RSCACHE_CS2_OP_DB_FIND_FILTER_WITH_COUNT;

    /* Top to bottom: the base-type selector, the search value, the dbcolumn. */
    const struct RSCache_CS2_Value* selector = cs2_peek_value_at(interp, 0);
    enum RSCache_CS2_StackType value_stack = RSCACHE_CS2_STACK_INT;
    if( selector && selector->stack_type == RSCACHE_CS2_STACK_INT &&
        selector->int_value == 2 )
        value_stack = RSCACHE_CS2_STACK_STRING;
    else if( !selector )
        /* Not a literal, so the selector cannot be read. The slot's own type is
         * then the best available answer and cannot desynchronise anything —
         * the value is one slot whichever stack it is on. */
        value_stack = cs2_peek_stack_type_at(interp, 1);

    struct RSCache_CS2_Expr* trailing = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    struct RSCache_CS2_Expr* value = cs2_pop(interp, value_stack);
    struct RSCache_CS2_Expr* column = cs2_pop(interp, RSCACHE_CS2_STACK_INT);

    struct RSCache_CS2_Expr* items[3] = { column, value, trailing };
    struct RSCache_CS2_Expr* arguments = RSCache_CS2_ExprFromList(arena, items, 3);

    enum RSCache_CS2_StackType count_stack = RSCACHE_CS2_STACK_INT;
    struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
        arena, &count_stack, with_count ? 1 : 0, opcode, arguments, false);
    if( !with_count )
        return RSCache_CS2_InsnAssignment(arena, RSCache_CS2_ExprEmpty(arena), operation);

    struct RSCache_CS2_Expr* pushed = cs2_push(interp, RSCACHE_CS2_STACK_INT, NULL);
    struct RSCache_CS2_Typing* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation)->items[0];
    RSCache_CS2_TypingAssign(
        operation_typing, RSCache_CS2_TypingsOfElement(cs2_typings(interp), pushed));
    return RSCache_CS2_InsnAssignment(arena, pushed, operation);
}

static struct RSCache_CS2_Insn*
cs2_translate_param(struct cs2_interp* interp, int opcode, enum RSCache_CS2_ProtoId receiver)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    const struct RSCache_CS2_Value* param_value = cs2_peek_value(interp);
    if( !param_value || param_value->stack_type != RSCACHE_CS2_STACK_INT )
    {
        cs2_fail(
            interp,
            "script %d pc %d: param id is not a literal int",
            interp->script_id,
            interp->pc);
        return NULL;
    }
    int param_id = param_value->int_value;

    enum RSCache_CS2_Type param_type = RSCACHE_CS2_TYPE_NONE;
    if( interp->options->param_types.load )
        param_type = interp->options->param_types.load(
            interp->options->param_types.user, param_id);
    if( param_type == RSCACHE_CS2_TYPE_NONE )
    {
        /* Not recoverable: the param's type decides whether this pushes an int
         * or a string, so continuing would desynchronise the stack. */
        cs2_fail(
            interp,
            "script %d pc %d: no type known for param %d",
            interp->script_id,
            interp->pc,
            param_id);
        return NULL;
    }

    struct RSCache_CS2_Expr* param = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    cs2_assign_element_to_proto(interp, param, RSCACHE_CS2_PROTO_PARAM);
    struct RSCache_CS2_Expr* target = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    cs2_assign_element_to_proto(interp, target, receiver);

    struct RSCache_CS2_Expr* pair[2] = { target, param };
    struct RSCache_CS2_Expr* arguments = RSCache_CS2_ExprFromList(arena, pair, 2);

    enum RSCache_CS2_StackType stack_type = RSCache_CS2_TypeStackType(param_type);
    struct RSCache_CS2_Expr* operation =
        RSCache_CS2_ExprOperation(arena, &stack_type, 1, opcode, arguments, false);
    struct RSCache_CS2_Typing* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation)->items[0];
    if( !RSCache_CS2_TypingFreezeType(operation_typing, param_type) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: param %d's type does not match the stack slot",
            interp->script_id,
            interp->pc,
            param_id);
        return NULL;
    }

    struct RSCache_CS2_Expr* definition = cs2_push(interp, stack_type, NULL);
    RSCache_CS2_TypingAssign(
        operation_typing, RSCache_CS2_TypingsOfElement(cs2_typings(interp), definition));
    return RSCache_CS2_InsnAssignment(arena, definition, operation);
}


static struct RSCache_CS2_Insn*
cs2_translate_enum(struct cs2_interp* interp)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    /* enum(key type, value type, enum id, key) -> value. Both type arguments
     * must be literal descriptor bytes, since the value type decides what this
     * leaves on the stack. */
    struct RSCache_CS2_Expr* key = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    struct RSCache_CS2_Expr* enum_id = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    cs2_assign_element_to_proto(interp, enum_id, RSCACHE_CS2_PROTO_ENUM);

    const struct RSCache_CS2_Value* value_desc = cs2_peek_value(interp);
    if( !value_desc || value_desc->stack_type != RSCACHE_CS2_STACK_INT )
    {
        cs2_fail(
            interp, "script %d pc %d: enum value type is not literal",
            interp->script_id, interp->pc);
        return NULL;
    }
    enum RSCache_CS2_Type value_type = RSCache_CS2_TypeOfDesc((uint8_t)value_desc->int_value);
    struct RSCache_CS2_Expr* value_type_var = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    cs2_assign_element_to_proto(interp, value_type_var, RSCACHE_CS2_PROTO_TYPE);

    const struct RSCache_CS2_Value* key_desc = cs2_peek_value(interp);
    if( !key_desc || key_desc->stack_type != RSCACHE_CS2_STACK_INT )
    {
        cs2_fail(
            interp, "script %d pc %d: enum key type is not literal",
            interp->script_id, interp->pc);
        return NULL;
    }
    enum RSCache_CS2_Type key_type = RSCache_CS2_TypeOfDesc((uint8_t)key_desc->int_value);
    if( value_type == RSCACHE_CS2_TYPE_NONE || key_type == RSCACHE_CS2_TYPE_NONE )
    {
        cs2_fail(
            interp,
            "script %d pc %d: enum type descriptor 0x%02x / 0x%02x is not known",
            interp->script_id,
            interp->pc,
            (unsigned)(key_desc->int_value & 0xFF),
            (unsigned)(value_desc->int_value & 0xFF));
        return NULL;
    }
    enum RSCache_CS2_ProtoId key_proto = RSCache_CS2_ProtoForType(key_type);
    if( key_proto != RSCACHE_CS2_PROTO_NONE )
        cs2_assign_element_to_proto(interp, key, key_proto);
    struct RSCache_CS2_Expr* key_type_var = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
    cs2_assign_element_to_proto(interp, key_type_var, RSCACHE_CS2_PROTO_TYPE);

    struct RSCache_CS2_Expr* items[4] = { key_type_var, value_type_var, enum_id, key };
    struct RSCache_CS2_Expr* arguments = RSCache_CS2_ExprFromList(arena, items, 4);

    enum RSCache_CS2_StackType stack_type = RSCache_CS2_TypeStackType(value_type);
    struct RSCache_CS2_Expr* value = cs2_push(interp, stack_type, NULL);
    struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
        arena, &stack_type, 1, RSCACHE_CS2_OP_ENUM, arguments, false);
    struct RSCache_CS2_Typing* operation_typing =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation)->items[0];
    if( !RSCache_CS2_TypingFreezeType(operation_typing, value_type) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: enum value type does not match the stack slot",
            interp->script_id,
            interp->pc);
        return NULL;
    }
    RSCache_CS2_TypingAssign(
        operation_typing, RSCache_CS2_TypingsOfElement(cs2_typings(interp), value));
    return RSCache_CS2_InsnAssignment(arena, value, operation);
}

static struct RSCache_CS2_Insn*
cs2_translate_proc(struct cs2_interp* interp)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);
    int callee_id = cs2_operand_int(interp);
    RSCache_CS2_IntMapPut(
        &interp->fs->call_triggers, callee_id, (void*)(intptr_t)RSCACHE_CS2_TRIGGER_PROC);

    const struct RSCache_CS2_Script* callee =
        interp->options->scripts.load ? interp->options->scripts.load(
                                            interp->options->scripts.user, callee_id)
                                      : NULL;
    if( !callee )
    {
        cs2_fail(
            interp,
            "script %d pc %d: gosub target %d is not in this cache",
            interp->script_id,
            interp->pc,
            callee_id);
        return NULL;
    }

    int arg_count = callee->int_argument_count + callee->string_argument_count;
    struct RSCache_CS2_Expr* popped[128];
    if( arg_count < 0 || arg_count > (int)(sizeof(popped) / sizeof(popped[0])) )
    {
        cs2_fail(interp, "script %d pc %d: gosub target %d takes %d arguments",
                 interp->script_id, interp->pc, callee_id, arg_count);
        return NULL;
    }
    cs2_pop_many(interp, arg_count, popped);
    struct RSCache_CS2_Expr* arguments = RSCache_CS2_ExprFromList(arena, popped, arg_count);

    enum RSCache_CS2_StackType arg_stack_types[128];
    int arg_stack_count = RSCache_CS2_ExprStackTypeCount(arguments);
    if( arg_stack_count > (int)(sizeof(arg_stack_types) / sizeof(arg_stack_types[0])) )
    {
        cs2_fail(interp, "script %d pc %d: gosub argument list too long",
                 interp->script_id, interp->pc);
        return NULL;
    }
    for( int i = 0; i < arg_stack_count; i++ )
        arg_stack_types[i] = RSCache_CS2_ExprStackTypeAt(arguments, i);

    struct RSCache_CS2_TypingList* args_typing = RSCache_CS2_TypingsArgs(
        cs2_typings(interp), interp->fs, callee_id, arg_stack_types, arg_stack_count);
    struct RSCache_CS2_TypingList* from =
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), arguments);
    if( from->count != args_typing->count )
    {
        cs2_fail(
            interp,
            "script %d pc %d: gosub to %d passes %d values, its signature wants %d",
            interp->script_id,
            interp->pc,
            callee_id,
            from->count,
            args_typing->count);
        return NULL;
    }
    if( !RSCache_CS2_TypingAssignLists(from, args_typing) )
    {
        cs2_fail(
            interp,
            "script %d pc %d: gosub to %d passes an argument of the wrong stack type",
            interp->script_id,
            interp->pc,
            callee_id);
        return NULL;
    }

    enum RSCache_CS2_StackType return_types[CS2_INTERP_MAX_STACK_TYPES];
    int return_count =
        RSCache_CS2_ScriptReturnTypes(callee, return_types, CS2_INTERP_MAX_STACK_TYPES);
    if( return_count > CS2_INTERP_MAX_STACK_TYPES )
    {
        cs2_fail(interp, "script %d pc %d: gosub target %d returns too many values",
                 interp->script_id, interp->pc, callee_id);
        return NULL;
    }

    struct RSCache_CS2_Expr* proc =
        RSCache_CS2_ExprProc(arena, return_types, return_count, callee_id, arguments);
    struct RSCache_CS2_Expr* pushed[CS2_INTERP_MAX_STACK_TYPES];
    cs2_push_types(interp, return_types, return_count, pushed);
    struct RSCache_CS2_Expr* definitions =
        RSCache_CS2_ExprFromList(arena, pushed, return_count);
    RSCache_CS2_TypingAssignLists(
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), proc),
        RSCache_CS2_TypingsOfExpr(cs2_typings(interp), definitions));
    return RSCache_CS2_InsnAssignment(arena, definitions, proc);
}


static struct RSCache_CS2_Insn*
cs2_translate(struct cs2_interp* interp)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);
    int opcode = cs2_opcode(interp);
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
    if( !info || info->kind == RSCACHE_CS2_CMD_UNKNOWN )
    {
        cs2_fail(
            interp,
            "script %d pc %d: opcode %d has no recorded signature",
            interp->script_id,
            interp->pc,
            opcode);
        return NULL;
    }

    switch( info->kind )
    {
    case RSCACHE_CS2_CMD_ASSIGN:
        return cs2_translate_assign(interp, opcode);

    case RSCACHE_CS2_CMD_BASIC:
        return cs2_translate_basic(interp, opcode, info);

    case RSCACHE_CS2_CMD_TYPED_POP:
        return cs2_translate_typed_pop(interp, opcode, info);

    case RSCACHE_CS2_CMD_CLIENTSCRIPT:
        return cs2_translate_clientscript(interp, opcode);

    case RSCACHE_CS2_CMD_PARAM:
        return cs2_translate_param(interp, opcode, (enum RSCache_CS2_ProtoId)info->extra);

    case RSCACHE_CS2_CMD_ENUM:
        return cs2_translate_enum(interp);

    case RSCACHE_CS2_CMD_DB_GETFIELD:
        return cs2_translate_db_getfield(interp, opcode);

    case RSCACHE_CS2_CMD_DB_FIND:
        return cs2_translate_db_find(interp, opcode);

    case RSCACHE_CS2_CMD_PROC:
        return cs2_translate_proc(interp);

    case RSCACHE_CS2_CMD_RETURN:
    {
        int count = interp->stack.count;
        struct RSCache_CS2_Expr** popped = (struct RSCache_CS2_Expr**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)(count > 0 ? count : 1) * sizeof(*popped));
        cs2_pop_many(interp, count, popped);
        struct RSCache_CS2_Expr* expression = RSCache_CS2_ExprFromList(arena, popped, count);

        int stack_count = RSCache_CS2_ExprStackTypeCount(expression);
        enum RSCache_CS2_StackType* stack_types =
            (enum RSCache_CS2_StackType*)RSCache_CS2_ArenaAlloc(
                arena, (size_t)(stack_count > 0 ? stack_count : 1) * sizeof(*stack_types));
        for( int i = 0; i < stack_count; i++ )
            stack_types[i] = RSCache_CS2_ExprStackTypeAt(expression, i);
        /* A script returns exactly what its epilogue declares, at every return
         * — the language has no varying-arity return. So a mismatch here is not
         * a property of this statement, it is evidence that some opcode above
         * popped the wrong number of values. Checking it turns a whole class of
         * silent mis-decompiles into refusals, and is what lets `infer-arity`
         * pin an unknown opcode's pop count. */
        if( stack_count != interp->return_type_count )
        {
            cs2_fail(
                interp,
                "script %d pc %d: return leaves %d values, the script declares %d",
                interp->script_id,
                interp->pc,
                stack_count,
                interp->return_type_count);
            return NULL;
        }
        for( int i = 0; i < stack_count; i++ )
        {
            if( stack_types[i] == interp->return_types[i] )
                continue;
            cs2_fail(
                interp,
                "script %d pc %d: return value %d is a %s, the script declares a %s",
                interp->script_id,
                interp->pc,
                i,
                stack_types[i] == RSCACHE_CS2_STACK_INT ? "int" : "string",
                interp->return_types[i] == RSCACHE_CS2_STACK_INT ? "int" : "string");
            return NULL;
        }

        struct RSCache_CS2_TypingList* returns = RSCache_CS2_TypingsReturns(
            cs2_typings(interp), interp->script_id, stack_types, stack_count);
        struct RSCache_CS2_TypingList* from =
            RSCache_CS2_TypingsOfExpr(cs2_typings(interp), expression);
        if( from->count == returns->count )
            RSCache_CS2_TypingAssignLists(from, returns);
        return RSCache_CS2_InsnReturn(arena, expression);
    }

    case RSCACHE_CS2_CMD_BRANCH:
        return RSCache_CS2_InsnGoto(arena, interp->pc + cs2_operand_int(interp) + 1);

    case RSCACHE_CS2_CMD_BRANCH_COMPARE:
    {
        struct RSCache_CS2_Expr* right = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        struct RSCache_CS2_Expr* left = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        RSCache_CS2_TypingCompare(
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), left),
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), right));
        struct RSCache_CS2_Expr* pair[2] = { left, right };
        struct RSCache_CS2_Expr* expression = RSCache_CS2_ExprOperation(
            arena, NULL, 0, opcode, RSCache_CS2_ExprFromList(arena, pair, 2), false);
        return RSCache_CS2_InsnBranch(
            arena, expression, interp->pc + cs2_operand_int(interp) + 1);
    }

    case RSCACHE_CS2_CMD_SWITCH:
    {
        struct RSCache_CS2_Expr* expression = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        int table = cs2_operand_int(interp);
        if( table < 0 || table >= interp->script->switch_table_count )
        {
            cs2_fail(interp, "script %d pc %d: switch table %d does not exist",
                     interp->script_id, interp->pc, table);
            return NULL;
        }
        const struct RSCache_CS2_ScriptSwitch* sw = &interp->script->switch_tables[table];
        int* keys = (int*)malloc((size_t)(sw->case_count > 0 ? sw->case_count : 1) * sizeof(int));
        int* targets =
            (int*)malloc((size_t)(sw->case_count > 0 ? sw->case_count : 1) * sizeof(int));
        if( !keys || !targets )
        {
            free(keys);
            free(targets);
            cs2_fail(interp, "script %d pc %d: out of memory reading a switch table",
                     interp->script_id, interp->pc);
            return NULL;
        }
        for( int i = 0; i < sw->case_count; i++ )
        {
            keys[i] = sw->cases[i].key;
            /* Case targets are relative to the instruction after the switch. */
            targets[i] = sw->cases[i].target_pc + 1 + interp->pc;
        }
        struct RSCache_CS2_Insn* insn =
            RSCache_CS2_InsnSwitch(arena, expression, keys, targets, sw->case_count);
        free(keys);
        free(targets);
        return insn;
    }

    case RSCACHE_CS2_CMD_DISCARD:
        return RSCache_CS2_InsnAssignment(
            arena,
            RSCache_CS2_ExprEmpty(arena),
            cs2_pop(interp, (enum RSCache_CS2_StackType)info->extra));

    case RSCACHE_CS2_CMD_JOIN_STRING:
    {
        int count = cs2_operand_int(interp);
        if( count < 0 || count > interp->stack.count )
        {
            cs2_fail(
                interp,
                "script %d pc %d: join claims %d parts, only %d values are on the stack",
                interp->script_id,
                interp->pc,
                count,
                interp->stack.count);
            return NULL;
        }
        struct RSCache_CS2_Expr** popped = (struct RSCache_CS2_Expr**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)(count > 0 ? count : 1) * sizeof(*popped));
        cs2_pop_many(interp, count, popped);
        enum RSCache_CS2_StackType stack_type = RSCACHE_CS2_STACK_STRING;
        struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
            arena,
            &stack_type,
            1,
            RSCACHE_CS2_OP_JOIN_STRING,
            RSCache_CS2_ExprFromList(arena, popped, count),
            false);
        struct RSCache_CS2_Expr* definition =
            cs2_push(interp, RSCACHE_CS2_STACK_STRING, NULL);
        return RSCache_CS2_InsnAssignment(arena, definition, operation);
    }

    case RSCACHE_CS2_CMD_DEFINE_ARRAY:
    {
        /* The operand packs the array slot in its high half and the element
         * type descriptor in its low byte. */
        int operand = cs2_operand_int(interp);
        struct RSCache_CS2_Expr* length = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        struct RSCache_CS2_Expr* array = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_ARRAY, interp->script_id, operand >> 16),
            NULL);
        enum RSCache_CS2_Type element_type = RSCache_CS2_TypeOfDesc((uint8_t)(operand & 0xFF));
        if( element_type == RSCACHE_CS2_TYPE_NONE )
        {
            cs2_fail(
                interp,
                "script %d pc %d: array element descriptor 0x%02x is unknown",
                interp->script_id,
                interp->pc,
                operand & 0xFF);
            return NULL;
        }
        int slot = operand >> 16;
        if( slot >= 0 && slot < CS2_INTERP_MAX_ARRAYS )
            interp->array_stack[slot] = RSCache_CS2_TypeStackType(element_type);
        cs2_assign_element_to_proto(interp, length, RSCACHE_CS2_PROTO_LENGTH);
        if( !RSCache_CS2_TypingFreezeType(
                RSCache_CS2_TypingsOfElement(cs2_typings(interp), array), element_type) )
        {
            cs2_fail(
                interp,
                "script %d pc %d: array element type does not match the stack slot",
                interp->script_id,
                interp->pc);
            return NULL;
        }
        struct RSCache_CS2_Expr* pair[2] = { array, length };
        return RSCache_CS2_InsnAssignment(
            arena,
            RSCache_CS2_ExprEmpty(arena),
            RSCache_CS2_ExprOperation(
                arena,
                NULL,
                0,
                RSCACHE_CS2_OP_DEFINE_ARRAY,
                RSCache_CS2_ExprFromList(arena, pair, 2),
                false));
    }

    case RSCACHE_CS2_CMD_PUSH_ARRAY_INT:
    {
        struct RSCache_CS2_Expr* index = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        struct RSCache_CS2_Expr* array = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_ARRAY, interp->script_id, cs2_operand_int(interp)),
            NULL);
        struct RSCache_CS2_Expr* pair[2] = { array, index };
        int declared = cs2_array_stack(interp, cs2_operand_int(interp));
        enum RSCache_CS2_StackType stack_type = declared < 0
                                                    ? RSCACHE_CS2_STACK_INT
                                                    : (enum RSCache_CS2_StackType)declared;
        struct RSCache_CS2_Expr* operation = RSCache_CS2_ExprOperation(
            arena,
            &stack_type,
            1,
            RSCACHE_CS2_OP_PUSH_ARRAY_INT,
            RSCache_CS2_ExprFromList(arena, pair, 2),
            false);
        struct RSCache_CS2_Expr* definition = cs2_push(interp, stack_type, NULL);
        cs2_assign_element_to_proto(interp, index, RSCACHE_CS2_PROTO_INDEX);
        struct RSCache_CS2_Typing* operation_typing =
            RSCache_CS2_TypingsOfExpr(cs2_typings(interp), operation)->items[0];
        RSCache_CS2_TypingAssign(
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), array), operation_typing);
        RSCache_CS2_TypingAssign(
            operation_typing, RSCache_CS2_TypingsOfElement(cs2_typings(interp), definition));
        return RSCache_CS2_InsnAssignment(arena, definition, operation);
    }

    case RSCACHE_CS2_CMD_POP_ARRAY_INT:
    {
        /* This script's own `define_array` where it has one, and otherwise the
         * stack the value is actually on.
         *
         * Arrays outlive a `gosub`, so a proc stores into one its caller
         * defined and the definition is not in this script at all. Falling back
         * to the value's own stack cannot desynchronise anything — the op takes
         * one value whichever bank it comes from — and 27 scripts need it,
         * every one storing a `join_string` result, which can only be a string.
         *
         * A client old enough to predate opcodes 105 and 210 has an `int[][]`
         * array bank and pops two ints here unconditionally, so string arrays
         * arrived with the same batch of changes as the 7600+ opcodes this
         * cache uses and that client does not implement. `define_array` already
         * carried an element *type* descriptor there, which would have had
         * nothing to select between. */
        int declared = cs2_array_stack(interp, cs2_operand_int(interp));
        struct RSCache_CS2_Expr* value = cs2_pop(
            interp, declared < 0 ? cs2_peek_stack_type_at(interp, 0)
                                 : (enum RSCache_CS2_StackType)declared);
        struct RSCache_CS2_Expr* index = cs2_pop(interp, RSCACHE_CS2_STACK_INT);
        struct RSCache_CS2_Expr* array = RSCache_CS2_ExprAccess(
            arena,
            RSCache_CS2_VarIntern(
                interp->fs, RSCACHE_CS2_VAR_ARRAY, interp->script_id, cs2_operand_int(interp)),
            NULL);
        cs2_assign_element_to_proto(interp, index, RSCACHE_CS2_PROTO_INDEX);
        RSCache_CS2_TypingAssign(
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), value),
            RSCache_CS2_TypingsOfElement(cs2_typings(interp), array));
        struct RSCache_CS2_Expr* triple[3] = { array, index, value };
        return RSCache_CS2_InsnAssignment(
            arena,
            RSCache_CS2_ExprEmpty(arena),
            RSCache_CS2_ExprOperation(
                arena,
                NULL,
                0,
                RSCACHE_CS2_OP_POP_ARRAY_INT,
                RSCache_CS2_ExprFromList(arena, triple, 3),
                false));
    }

    default:
        cs2_fail(interp, "script %d pc %d: opcode %d has an unhandled command kind",
                 interp->script_id, interp->pc, opcode);
        return NULL;
    }
}

/* -------------------------------------------------------------------------
 * Labels
 * ---------------------------------------------------------------------- */

static bool
cs2_add_labels(
    struct cs2_interp* interp,
    struct RSCache_CS2_Insn** instructions,
    int count,
    struct RSCache_CS2_Chain* chain)
{
    struct RSCache_CS2_Arena* arena = cs2_arena(interp);

    bool* is_target = (bool*)calloc((size_t)count + 1, sizeof(bool));
    if( !is_target )
        return false;

    for( int i = 0; i < count; i++ )
    {
        struct RSCache_CS2_Insn* insn = instructions[i];
        switch( insn->kind )
        {
        case RSCACHE_CS2_INSN_BRANCH:
        case RSCACHE_CS2_INSN_GOTO:
            if( insn->target_id < 0 || insn->target_id >= count )
            {
                cs2_fail(
                    interp,
                    "script %d: branch target %d is outside the script",
                    interp->script_id,
                    insn->target_id);
                free(is_target);
                return false;
            }
            is_target[insn->target_id] = true;
            break;
        case RSCACHE_CS2_INSN_SWITCH:
            for( int j = 0; j < insn->case_count; j++ )
            {
                int target = insn->case_target_ids[j];
                if( target < 0 || target >= count )
                {
                    cs2_fail(
                        interp,
                        "script %d: switch target %d is outside the script",
                        interp->script_id,
                        target);
                    free(is_target);
                    return false;
                }
                is_target[target] = true;
            }
            break;
        default:
            break;
        }
    }

    struct RSCache_CS2_Insn** labels = (struct RSCache_CS2_Insn**)calloc(
        (size_t)count + 1, sizeof(struct RSCache_CS2_Insn*));
    if( !labels )
    {
        free(is_target);
        return false;
    }

    for( int i = 0; i < count; i++ )
    {
        if( is_target[i] )
        {
            labels[i] = RSCache_CS2_InsnLabel(arena, i);
            RSCache_CS2_ChainAddLast(chain, labels[i]);
        }
        RSCache_CS2_ChainAddLast(chain, instructions[i]);
    }

    /* The reference relies on Label being a value class so a branch's own Label
     * instance resolves to the chain's; here the pointer is bound once. */
    for( int i = 0; i < count; i++ )
    {
        struct RSCache_CS2_Insn* insn = instructions[i];
        switch( insn->kind )
        {
        case RSCACHE_CS2_INSN_BRANCH:
            insn->pass = labels[insn->target_id];
            break;
        case RSCACHE_CS2_INSN_GOTO:
            insn->label = labels[insn->target_id];
            break;
        case RSCACHE_CS2_INSN_SWITCH:
            for( int j = 0; j < insn->case_count; j++ )
                insn->case_labels[j] = labels[insn->case_target_ids[j]];
            break;
        default:
            break;
        }
    }

    free(is_target);
    free(labels);
    return true;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

static bool
cs2_interpret_one(struct cs2_interp* interp, int script_id)
{
    const struct RSCache_CS2_Script* script =
        interp->options->scripts.load
            ? interp->options->scripts.load(interp->options->scripts.user, script_id)
            : NULL;
    if( !script )
    {
        cs2_fail(interp, "script %d is not in this cache", script_id);
        return false;
    }

    interp->script_id = script_id;
    interp->script = script;
    interp->pc = 0;
    interp->stack_counter = 0;
    RSCache_CS2_VecClear(&interp->stack);
    /* Array slots are script-local, so the recorded element stacks are too:
     * carrying one script's `def_stringarray0` into the next would pop the
     * wrong stack in a script that never defined slot 0. */
    for( int i = 0; i < CS2_INTERP_MAX_ARRAYS; i++ )
        interp->array_stack[i] = -1;
    interp->return_type_count = RSCache_CS2_ScriptReturnTypes(
        script, interp->return_types, CS2_INTERP_MAX_STACK_TYPES);

    struct RSCache_CS2_Insn** instructions = (struct RSCache_CS2_Insn**)calloc(
        (size_t)(script->op_count > 0 ? script->op_count : 1),
        sizeof(struct RSCache_CS2_Insn*));
    if( !instructions )
        return false;

    for( interp->pc = 0; interp->pc < script->op_count; interp->pc++ )
    {
        int opcode = cs2_opcode(interp);
        struct RSCache_CS2_Insn* insn = cs2_translate(interp);
        if( !insn || interp->failed )
        {
            free(instructions);
            return false;
        }
        instructions[interp->pc] = insn;

        /* Only an assignment may leave values behind — anything else running
         * with a non-empty stack means an arity is wrong somewhere above. */
        if( insn->kind != RSCACHE_CS2_INSN_ASSIGNMENT && interp->stack.count != 0 )
        {
            cs2_fail(
                interp,
                "script %d pc %d: opcode %d left %d values on the operand stack",
                script_id,
                interp->pc,
                opcode,
                interp->stack.count);
            free(instructions);
            return false;
        }
    }

    struct RSCache_CS2_Function* function =
        (struct RSCache_CS2_Function*)RSCache_CS2_ArenaAlloc(
            cs2_arena(interp), sizeof(*function));
    function->id = script_id;
    RSCache_CS2_VecInit(&function->arguments);
    RSCache_CS2_ArenaTrackVec(cs2_arena(interp), &function->arguments);
    RSCache_CS2_ChainInit(&function->instructions);
    function->return_types = (enum RSCache_CS2_StackType*)RSCache_CS2_ArenaAlloc(
        cs2_arena(interp),
        (size_t)(interp->return_type_count > 0 ? interp->return_type_count : 1) *
            sizeof(enum RSCache_CS2_StackType));
    memcpy(
        function->return_types,
        interp->return_types,
        (size_t)interp->return_type_count * sizeof(enum RSCache_CS2_StackType));
    function->return_type_count = interp->return_type_count;

    for( int i = 0; i < script->int_argument_count; i++ )
        RSCache_CS2_VecPush(
            &function->arguments,
            RSCache_CS2_VarIntern(interp->fs, RSCACHE_CS2_VAR_INT, script_id, i));
    for( int i = 0; i < script->string_argument_count; i++ )
        RSCache_CS2_VecPush(
            &function->arguments,
            RSCache_CS2_VarIntern(interp->fs, RSCACHE_CS2_VAR_STRING, script_id, i));

    bool ok = cs2_add_labels(interp, instructions, script->op_count, &function->instructions);
    free(instructions);
    if( !ok )
        return false;

    RSCache_CS2_IntMapPut(&interp->fs->functions, script_id, function);
    RSCache_CS2_IntVecPush(&interp->fs->function_ids, script_id);
    return true;
}

bool
RSCache_CS2_Interpret(
    struct RSCache_CS2_FunctionSet* fs,
    const int* script_ids,
    int script_id_count,
    const struct RSCache_CS2_DecompileOptions* options,
    char* error,
    int error_capacity)
{
    struct cs2_interp interp;
    memset(&interp, 0, sizeof(interp));
    interp.fs = fs;
    interp.options = options;
    interp.error = error;
    interp.error_capacity = error_capacity;
    RSCache_CS2_VecInit(&interp.stack);

    bool ok = true;
    for( int i = 0; i < script_id_count && ok; i++ )
        ok = cs2_interpret_one(&interp, script_ids[i]);

    RSCache_CS2_VecFree(&interp.stack);
    return ok && !interp.failed;
}
