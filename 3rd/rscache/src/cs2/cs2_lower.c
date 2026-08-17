/*
 * IR -> bytecode. The inverse of cs2_interp.c, function by function.
 *
 * Every rule here is read off a `cs2_translate_*` body rather than invented, so
 * the two files are meant to be read side by side. Where the interpreter pops
 * three values top-first and stores them into `items[0..2]` bottom-first, this
 * emits `items[0..2]` in list order; where it reads an operand to decide a
 * shape, this reconstructs that operand from the shape.
 *
 * Two things the IR does not carry had to be added to it rather than guessed
 * (see cs2_ir.h): a hook's argument descriptor, whose letters distinguish types
 * that share a stack, and the target of the unreachable `goto` the dead-code
 * pass deletes.
 */
#include "cs2_lower.h"

#include "cs2_command.h"
#include "cs2_support.h"
#include "cs2_types.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Emit buffer
 * ---------------------------------------------------------------------- */

struct cs2_lower_switch
{
    int* keys;
    struct RSCache_CS2_Insn** labels;
    int count;
};

struct cs2_lower_fixup
{
    int op_index;
    struct RSCache_CS2_Insn* label;
};

struct cs2_lower
{
    struct RSCache_CS2_FunctionSet* fs;
    struct RSCache_CS2_Function* function;
    const struct RSCache_CS2_LowerOptions* options;

    uint16_t* opcodes;
    int* int_operands;
    char** string_operands;
    int count;
    int capacity;

    struct cs2_lower_fixup* fixups;
    int fixup_count;
    int fixup_capacity;

    struct cs2_lower_switch* switches;
    int switch_count;
    int switch_capacity;

    /* Insn* (LABEL) -> (intptr_t)(op index + 1); 0 means "not emitted yet". */
    struct RSCache_CS2_Map label_index;

    /* Highest slot each bank is asked for, -1 when the bank is untouched. */
    int max_int_local;
    int max_string_local;

    /*
     * Spill slots for operand-stack values the inlining pass could not fold.
     *
     * A stack variable that survives `cs2_inline_stack_definitions` is one
     * whose value crosses a control-flow merge, so it has no expression to be
     * folded into. It is still an ordinary value; it just needs somewhere to
     * live that is not the operand stack. Variable* -> (intptr_t)(slot + 1).
     */
    struct RSCache_CS2_Map spill_slots;
    int spill_int_next;
    int spill_string_next;

    /** Set when the chain still held the script's closing return. */
    bool emitted_epilogue;

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_lower_fail(struct cs2_lower* lo, const char* fmt, ...)
{
    assert(lo);
    assert(fmt);
    if( lo->failed )
        return;
    lo->failed = true;
    if( !lo->error || lo->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(lo->error, (size_t)lo->error_capacity, fmt, args);
    va_end(args);
}

static void
cs2_lower_reserve(struct cs2_lower* lo, int needed)
{
    assert(lo);
    if( lo->count + needed <= lo->capacity )
        return;
    int capacity = lo->capacity ? lo->capacity * 2 : 256;
    while( capacity < lo->count + needed )
        capacity *= 2;
    uint16_t* opcodes = (uint16_t*)realloc(lo->opcodes, (size_t)capacity * sizeof(*opcodes));
    assert(opcodes);
    int* int_operands =
        (int*)realloc(lo->int_operands, (size_t)capacity * sizeof(*int_operands));
    assert(int_operands);
    char** string_operands =
        (char**)realloc(lo->string_operands, (size_t)capacity * sizeof(*string_operands));
    assert(string_operands);
    memset(string_operands + lo->count, 0,
           (size_t)(capacity - lo->count) * sizeof(*string_operands));
    lo->opcodes = opcodes;
    lo->int_operands = int_operands;
    lo->string_operands = string_operands;
    lo->capacity = capacity;
}

/** Append one instruction; returns its index. */
static int
cs2_emit(struct cs2_lower* lo, int opcode, int operand)
{
    assert(lo);
    if( lo->failed )
        return lo->count;
    cs2_lower_reserve(lo, 1);
    int index = lo->count++;
    lo->opcodes[index] = (uint16_t)opcode;
    lo->int_operands[index] = operand;
    lo->string_operands[index] = NULL;
    return index;
}

static void
cs2_emit_string(struct cs2_lower* lo, const char* text)
{
    assert(lo);
    int index = cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_STRING, 0);
    if( lo->failed )
        return;
    const char* source = text ? text : "";
    size_t length = strlen(source);
    char* copy = (char*)malloc(length + 1);
    assert(copy);
    memcpy(copy, source, length + 1);
    lo->string_operands[index] = copy;
}

/** Emit a jump whose operand is patched once its label's position is known. */
static void
cs2_emit_jump(struct cs2_lower* lo, int opcode, struct RSCache_CS2_Insn* label)
{
    assert(lo);
    if( lo->failed )
        return;
    if( !label )
    {
        cs2_lower_fail(lo, "script %d: a jump has no destination", lo->function->id);
        return;
    }
    int index = cs2_emit(lo, opcode, 0);
    if( lo->fixup_count == lo->fixup_capacity )
    {
        int capacity = lo->fixup_capacity ? lo->fixup_capacity * 2 : 64;
        struct cs2_lower_fixup* fixups =
            (struct cs2_lower_fixup*)realloc(lo->fixups, (size_t)capacity * sizeof(*fixups));
        assert(fixups);
        lo->fixups = fixups;
        lo->fixup_capacity = capacity;
    }
    lo->fixups[lo->fixup_count].op_index = index;
    lo->fixups[lo->fixup_count].label = label;
    lo->fixup_count++;
}

/* -------------------------------------------------------------------------
 * Locals
 * ---------------------------------------------------------------------- */

static void
cs2_note_int_local(struct cs2_lower* lo, int slot)
{
    assert(lo);
    if( slot > lo->max_int_local )
        lo->max_int_local = slot;
}

static void
cs2_note_string_local(struct cs2_lower* lo, int slot)
{
    assert(lo);
    if( slot > lo->max_string_local )
        lo->max_string_local = slot;
}

/**
 * The frame slot a leftover operand-stack variable is spilled to.
 *
 * Allocated above every slot the body already uses, and stable per variable so
 * a value defined once and read twice lands in one place. The counters start
 * past the function's own high-water mark, which is why the first spill cannot
 * be handed out until the body has been walked — see the two-pass note in
 * RSCache_CS2_Lower.
 */
static int
cs2_spill_slot(struct cs2_lower* lo, struct RSCache_CS2_Variable* variable)
{
    assert(lo);
    assert(variable);
    void* existing = RSCache_CS2_MapGet(&lo->spill_slots, variable);
    if( existing )
        return (int)(intptr_t)existing - 1;
    bool is_string = RSCache_CS2_VarStackType(variable->kind) == RSCACHE_CS2_STACK_STRING;
    int slot = is_string ? lo->spill_string_next++ : lo->spill_int_next++;
    RSCache_CS2_MapPut(&lo->spill_slots, variable, (void*)(intptr_t)(slot + 1));
    if( is_string )
        cs2_note_string_local(lo, slot);
    else
        cs2_note_int_local(lo, slot);
    return slot;
}

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

static void
cs2_emit_expr(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr);

/** Emit each item of a compound (or the single expression) in list order. */
static void
cs2_emit_list(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr)
{
    assert(lo);
    if( !expr || lo->failed )
        return;
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(expr, &items, &count, &single);
    for( int i = 0; i < count && !lo->failed; i++ )
        cs2_emit_expr(lo, items[i]);
}

/** The push opcode for a global, or -1 when the kind is not a global. */
static int
cs2_global_push_opcode(enum RSCache_CS2_VarKind kind)
{
    switch( kind )
    {
    case RSCACHE_CS2_VAR_VARP:
        return RSCACHE_CS2_OP_PUSH_VAR;
    case RSCACHE_CS2_VAR_VARBIT:
        return RSCACHE_CS2_OP_PUSH_VARBIT;
    case RSCACHE_CS2_VAR_VARCINT:
        return RSCACHE_CS2_OP_PUSH_VARC_INT;
    case RSCACHE_CS2_VAR_VARCSTRING:
        return RSCACHE_CS2_OP_PUSH_VARC_STRING;
    case RSCACHE_CS2_VAR_VARCLANSETTING:
        return RSCACHE_CS2_OP_PUSH_VARCLANSETTING;
    case RSCACHE_CS2_VAR_VARCLAN:
        return RSCACHE_CS2_OP_PUSH_VARCLAN;
    default:
        return -1;
    }
}

static int
cs2_global_pop_opcode(enum RSCache_CS2_VarKind kind)
{
    switch( kind )
    {
    case RSCACHE_CS2_VAR_VARP:
        return RSCACHE_CS2_OP_POP_VAR;
    case RSCACHE_CS2_VAR_VARBIT:
        return RSCACHE_CS2_OP_POP_VARBIT;
    case RSCACHE_CS2_VAR_VARCINT:
        return RSCACHE_CS2_OP_POP_VARC_INT;
    case RSCACHE_CS2_VAR_VARCSTRING:
        return RSCACHE_CS2_OP_POP_VARC_STRING;
    default:
        /* varclan and varclansetting are read-only; the bytecode has no pop. */
        return -1;
    }
}

/** Read a variable. */
static void
cs2_emit_access(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr)
{
    assert(lo);
    assert(expr);
    struct RSCache_CS2_Variable* variable = expr->variable;
    if( !variable )
    {
        cs2_lower_fail(lo, "script %d: an access has no variable", lo->function->id);
        return;
    }
    switch( variable->kind )
    {
    case RSCACHE_CS2_VAR_INT:
        cs2_note_int_local(lo, variable->id);
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_INT_LOCAL, variable->id);
        return;
    case RSCACHE_CS2_VAR_STRING:
        cs2_note_string_local(lo, variable->id);
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_STRING_LOCAL, variable->id);
        return;
    case RSCACHE_CS2_VAR_STACKINT:
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_INT_LOCAL, cs2_spill_slot(lo, variable));
        return;
    case RSCACHE_CS2_VAR_STACKSTRING:
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_STRING_LOCAL, cs2_spill_slot(lo, variable));
        return;
    case RSCACHE_CS2_VAR_ARRAY:
        /* An array is named by the operand of the op that touches it, so the
         * access itself pushes nothing. Reaching here means an array was used
         * as a value, which the bytecode has no way to say. */
        cs2_lower_fail(
            lo, "script %d: array %d was used as a value", lo->function->id, variable->id);
        return;
    default:
        break;
    }
    int opcode = cs2_global_push_opcode(variable->kind);
    if( opcode < 0 )
    {
        cs2_lower_fail(
            lo, "script %d: variable kind %d cannot be read", lo->function->id,
            (int)variable->kind);
        return;
    }
    cs2_emit(lo, opcode, variable->id);
}

/** Write the value on top of the stack into a variable. */
static void
cs2_emit_store(struct cs2_lower* lo, struct RSCache_CS2_Expr* target)
{
    assert(lo);
    assert(target);
    if( target->kind != RSCACHE_CS2_EXPR_ACCESS )
    {
        cs2_lower_fail(
            lo, "script %d: assignment target is expression kind %d, not a variable",
            lo->function->id, (int)target->kind);
        return;
    }
    struct RSCache_CS2_Variable* variable = target->variable;
    assert(variable);
    switch( variable->kind )
    {
    case RSCACHE_CS2_VAR_INT:
        cs2_note_int_local(lo, variable->id);
        cs2_emit(lo, RSCACHE_CS2_OP_POP_INT_LOCAL, variable->id);
        return;
    case RSCACHE_CS2_VAR_STRING:
        cs2_note_string_local(lo, variable->id);
        cs2_emit(lo, RSCACHE_CS2_OP_POP_STRING_LOCAL, variable->id);
        return;
    case RSCACHE_CS2_VAR_STACKINT:
        cs2_emit(lo, RSCACHE_CS2_OP_POP_INT_LOCAL, cs2_spill_slot(lo, variable));
        return;
    case RSCACHE_CS2_VAR_STACKSTRING:
        cs2_emit(lo, RSCACHE_CS2_OP_POP_STRING_LOCAL, cs2_spill_slot(lo, variable));
        return;
    default:
        break;
    }
    int opcode = cs2_global_pop_opcode(variable->kind);
    if( opcode < 0 )
    {
        cs2_lower_fail(
            lo, "script %d: variable kind %d cannot be written", lo->function->id,
            (int)variable->kind);
        return;
    }
    cs2_emit(lo, opcode, variable->id);
}

/** The array variable an array operation's first argument names. */
static struct RSCache_CS2_Variable*
cs2_array_operand(struct cs2_lower* lo, struct RSCache_CS2_Expr** items, int count)
{
    assert(lo);
    if( count < 1 || !items[0] || items[0]->kind != RSCACHE_CS2_EXPR_ACCESS ||
        !items[0]->variable || items[0]->variable->kind != RSCACHE_CS2_VAR_ARRAY )
    {
        cs2_lower_fail(
            lo, "script %d: an array operation's first argument is not an array",
            lo->function->id);
        return NULL;
    }
    return items[0]->variable;
}

/**
 * The element-type descriptor byte `define_array` needs.
 *
 * The operand packs it, and the IR keeps it only in the array variable's
 * typing, which the interpreter froze when it read the definition. Falling back
 * to the bank's own letter keeps a lowering possible for an array whose typing
 * was dropped — `i`/`s` are the two the client itself would derive.
 */
static int
cs2_array_type_desc(struct cs2_lower* lo, struct RSCache_CS2_Variable* array)
{
    assert(lo);
    assert(array);
    struct RSCache_CS2_Typing* typing =
        RSCache_CS2_TypingsOfArray(&lo->fs->typings, array, RSCACHE_CS2_STACK_INT);
    if( typing && typing->type != RSCACHE_CS2_TYPE_NONE )
    {
        int desc = RSCache_CS2_TypeDesc(typing->type);
        if( desc > 0 )
            return desc;
    }
    if( typing && typing->stack_type == RSCACHE_CS2_STACK_STRING )
        return RSCache_CS2_TypeDesc(RSCACHE_CS2_TYPE_STRING);
    return RSCache_CS2_TypeDesc(RSCACHE_CS2_TYPE_INT);
}

/**
 * A hook registration: `cc_setonop`, `if_setonvartransmit` and the rest.
 *
 * Inverse of cs2_translate_clientscript, whose pops run top-first. Reading them
 * bottom-up gives the push order below: id, arguments, triggers, trigger count,
 * descriptor, and — for the `if_*` family only — the target component last.
 */
static void
cs2_emit_clientscript(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr)
{
    assert(lo);
    assert(expr);
    if( !expr->hook_descriptor )
    {
        cs2_lower_fail(
            lo, "script %d: hook for script %d has no argument descriptor",
            lo->function->id, expr->script_id);
        return;
    }

    cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, expr->script_id);
    cs2_emit_list(lo, expr->arguments);

    struct RSCache_CS2_Expr* trigger_single = NULL;
    struct RSCache_CS2_Expr** triggers = NULL;
    int trigger_count = 0;
    if( expr->triggers )
        RSCache_CS2_ExprAsList(expr->triggers, &triggers, &trigger_count, &trigger_single);
    /* A trigger is a varp *pointer* for the vartransmit family and an ordinary
     * value for the stat and inv ones; both are pushed as a plain int. */
    for( int i = 0; i < trigger_count && !lo->failed; i++ )
    {
        struct RSCache_CS2_Expr* trigger = triggers[i];
        if( trigger && trigger->kind == RSCACHE_CS2_EXPR_POINTER )
        {
            assert(trigger->variable);
            cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, trigger->variable->id);
            continue;
        }
        cs2_emit_expr(lo, trigger);
    }

    size_t descriptor_length = strlen(expr->hook_descriptor);
    bool has_trigger_list = descriptor_length > 0 &&
                            expr->hook_descriptor[descriptor_length - 1] == 'Y';
    if( has_trigger_list )
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, trigger_count);
    else if( trigger_count > 0 )
    {
        cs2_lower_fail(
            lo, "script %d: hook for script %d carries %d triggers its descriptor does not",
            lo->function->id, expr->script_id, trigger_count);
        return;
    }

    cs2_emit_string(lo, expr->hook_descriptor);
    if( expr->component )
        cs2_emit_expr(lo, expr->component);
    cs2_emit(lo, expr->opcode, expr->dot ? 1 : 0);
}

static void
cs2_emit_operation(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr)
{
    assert(lo);
    assert(expr);
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(expr->arguments, &items, &count, &single);

    switch( expr->opcode )
    {
    case RSCACHE_CS2_OP_PUSH_ARRAY_INT:
    {
        /* [array, index] — only the index is a value; the array is the operand. */
        struct RSCache_CS2_Variable* array = cs2_array_operand(lo, items, count);
        if( !array )
            return;
        if( count != 2 )
        {
            cs2_lower_fail(lo, "script %d: array read takes 2 arguments, not %d",
                           lo->function->id, count);
            return;
        }
        cs2_emit_expr(lo, items[1]);
        cs2_note_string_local(lo, array->id);
        cs2_emit(lo, RSCACHE_CS2_OP_PUSH_ARRAY_INT, array->id);
        return;
    }
    case RSCACHE_CS2_OP_POP_ARRAY_INT:
    {
        /* [array, index, value] */
        struct RSCache_CS2_Variable* array = cs2_array_operand(lo, items, count);
        if( !array )
            return;
        if( count != 3 )
        {
            cs2_lower_fail(lo, "script %d: array write takes 3 arguments, not %d",
                           lo->function->id, count);
            return;
        }
        cs2_emit_expr(lo, items[1]);
        cs2_emit_expr(lo, items[2]);
        cs2_note_string_local(lo, array->id);
        cs2_emit(lo, RSCACHE_CS2_OP_POP_ARRAY_INT, array->id);
        return;
    }
    case RSCACHE_CS2_OP_DEFINE_ARRAY:
    {
        /* [array, length] */
        struct RSCache_CS2_Variable* array = cs2_array_operand(lo, items, count);
        if( !array )
            return;
        if( count != 2 )
        {
            cs2_lower_fail(lo, "script %d: define_array takes 2 arguments, not %d",
                           lo->function->id, count);
            return;
        }
        cs2_emit_expr(lo, items[1]);
        cs2_note_string_local(lo, array->id);
        cs2_emit(lo, RSCACHE_CS2_OP_DEFINE_ARRAY,
                 (array->id << 16) | cs2_array_type_desc(lo, array));
        return;
    }
    case RSCACHE_CS2_OP_JOIN_STRING:
        cs2_emit_list(lo, expr->arguments);
        cs2_emit(lo, RSCACHE_CS2_OP_JOIN_STRING, count);
        return;
    case RSCACHE_CS2_OP_SS_AND:
    case RSCACHE_CS2_OP_SS_OR:
        /* Synthesised by a source-only pass; there is no such instruction. */
        cs2_lower_fail(
            lo, "script %d: a short-circuit operator reached the lowerer", lo->function->id);
        return;
    default:
        break;
    }

    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(expr->opcode);
    if( !info )
    {
        cs2_lower_fail(lo, "script %d: opcode %d has no recorded signature",
                       lo->function->id, expr->opcode);
        return;
    }
    cs2_emit_list(lo, expr->arguments);
    /*
     * The decoded operand where there was one, and the active-form flag where
     * the node was built by a pass instead.
     *
     * Assuming the operand *is* the flag is wrong for the commands that carry a
     * selector in it — 4123 and 4124 take no arguments at all and choose what
     * they push from the operand — so the decoded value is what gets replayed.
     */
    int operand = expr->has_raw_operand ? expr->raw_operand
                                        : (info->dot_capable && expr->dot ? 1 : 0);
    cs2_emit(lo, expr->opcode, operand);
}

static void
cs2_emit_expr(struct cs2_lower* lo, struct RSCache_CS2_Expr* expr)
{
    assert(lo);
    if( !expr || lo->failed )
        return;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
        if( expr->value.stack_type == RSCACHE_CS2_STACK_STRING )
            cs2_emit_string(lo, expr->value.string_value);
        else
            cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, expr->value.int_value);
        return;
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
    {
        /* The magic operand the client substitutes at hook time; it is an
         * ordinary constant in the stream, and `event_opbase` is a string
         * where the other nine are ints. */
        struct RSCache_CS2_Value magic;
        if( !RSCache_CS2_EventPropertyMagic(expr->event_property, &magic) )
        {
            cs2_lower_fail(lo, "script %d: event property %d has no constant",
                           lo->function->id, (int)expr->event_property);
            return;
        }
        if( magic.stack_type == RSCACHE_CS2_STACK_STRING )
            cs2_emit_string(lo, magic.string_value);
        else
            cs2_emit(lo, RSCACHE_CS2_OP_PUSH_CONSTANT_INT, magic.int_value);
        return;
    }
    case RSCACHE_CS2_EXPR_ACCESS:
        cs2_emit_access(lo, expr);
        return;
    case RSCACHE_CS2_EXPR_POINTER:
        /* Two shapes reach here. A hook's trigger list is handled by
         * cs2_emit_clientscript and never arrives. An array passed to a proc
         * does, and what the call pushed is the handle the caller was holding
         * — recorded on the node because the rewrite that created the pointer
         * discarded it (Expr::pointer_source). */
        if( expr->pointer_source )
        {
            cs2_emit_expr(lo, expr->pointer_source);
            return;
        }
        if( expr->variable && expr->variable->kind == RSCACHE_CS2_VAR_ARRAY )
        {
            cs2_note_string_local(lo, expr->variable->id);
            cs2_emit(lo, RSCACHE_CS2_OP_PUSH_STRING_LOCAL, expr->variable->id);
            return;
        }
        cs2_lower_fail(lo, "script %d: a variable pointer is not a value",
                       lo->function->id);
        return;
    case RSCACHE_CS2_EXPR_COMPOUND:
        cs2_emit_list(lo, expr);
        return;
    case RSCACHE_CS2_EXPR_OPERATION:
        cs2_emit_operation(lo, expr);
        return;
    case RSCACHE_CS2_EXPR_PROC:
        cs2_emit_list(lo, expr->arguments);
        cs2_emit(lo, RSCACHE_CS2_OP_GOSUB_WITH_PARAMS, expr->script_id);
        return;
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        cs2_emit_clientscript(lo, expr);
        return;
    }
    cs2_lower_fail(lo, "script %d: expression kind %d has no bytecode",
                   lo->function->id, (int)expr->kind);
}

/* -------------------------------------------------------------------------
 * Instructions
 * ---------------------------------------------------------------------- */

static void
cs2_emit_assignment(struct cs2_lower* lo, struct RSCache_CS2_Insn* insn)
{
    assert(lo);
    assert(insn);
    cs2_emit_expr(lo, insn->expression);
    if( lo->failed )
        return;

    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** targets = NULL;
    int target_count = 0;
    RSCache_CS2_ExprAsList(insn->definitions, &targets, &target_count, &single);

    int produced = RSCache_CS2_ExprStackTypeCount(insn->expression);
    if( target_count > produced )
    {
        cs2_lower_fail(
            lo, "script %d: an assignment writes %d values from an expression producing %d",
            lo->function->id, target_count, produced);
        return;
    }

    /*
     * Values come off the top, so the last one written is popped first.
     *
     * Anything the definitions do not claim is discarded, in the same
     * top-first order — the shape the interpreter's DISCARD kind produced and
     * the shape a command called for its effect alone leaves behind.
     */
    for( int i = produced - 1; i >= 0 && !lo->failed; i-- )
    {
        if( i < target_count )
        {
            cs2_emit_store(lo, targets[i]);
            continue;
        }
        enum RSCache_CS2_StackType stack = RSCache_CS2_ExprStackTypeAt(insn->expression, i);
        cs2_emit(lo,
                 stack == RSCACHE_CS2_STACK_STRING ? RSCACHE_CS2_OP_POP_STRING_DISCARD
                                                   : RSCACHE_CS2_OP_POP_INT_DISCARD,
                 0);
    }
}

static void
cs2_emit_branch(struct cs2_lower* lo, struct RSCache_CS2_Insn* insn)
{
    assert(lo);
    assert(insn);
    struct RSCache_CS2_Expr* test = insn->expression;
    if( !test || test->kind != RSCACHE_CS2_EXPR_OPERATION )
    {
        cs2_lower_fail(lo, "script %d: a branch test is not a comparison",
                       lo->function->id);
        return;
    }
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(test->opcode);
    if( !info || info->kind != RSCACHE_CS2_CMD_BRANCH_COMPARE )
    {
        cs2_lower_fail(lo, "script %d: opcode %d is not a conditional branch",
                       lo->function->id, test->opcode);
        return;
    }
    cs2_emit_list(lo, test->arguments);
    cs2_emit_jump(lo, test->opcode, insn->pass);
}

static void
cs2_emit_switch(struct cs2_lower* lo, struct RSCache_CS2_Insn* insn)
{
    assert(lo);
    assert(insn);
    cs2_emit_expr(lo, insn->expression);
    if( lo->failed )
        return;

    if( lo->switch_count == lo->switch_capacity )
    {
        int capacity = lo->switch_capacity ? lo->switch_capacity * 2 : 8;
        struct cs2_lower_switch* switches = (struct cs2_lower_switch*)realloc(
            lo->switches, (size_t)capacity * sizeof(*switches));
        assert(switches);
        lo->switches = switches;
        lo->switch_capacity = capacity;
    }
    struct cs2_lower_switch* table = &lo->switches[lo->switch_count];
    memset(table, 0, sizeof(*table));
    table->count = insn->case_count;
    if( insn->case_count > 0 )
    {
        table->keys = (int*)malloc((size_t)insn->case_count * sizeof(int));
        assert(table->keys);
        table->labels = (struct RSCache_CS2_Insn**)malloc(
            (size_t)insn->case_count * sizeof(*table->labels));
        assert(table->labels);
        for( int i = 0; i < insn->case_count; i++ )
        {
            table->keys[i] = insn->case_keys[i];
            table->labels[i] = insn->case_labels[i];
        }
    }
    cs2_emit(lo, RSCACHE_CS2_OP_SWITCH, lo->switch_count);
    lo->switch_count++;
}

static void
cs2_emit_insn(struct cs2_lower* lo, struct RSCache_CS2_Insn* insn)
{
    assert(lo);
    assert(insn);
    switch( insn->kind )
    {
    case RSCACHE_CS2_INSN_LABEL:
        RSCache_CS2_MapPut(&lo->label_index, insn, (void*)(intptr_t)(lo->count + 1));
        return;
    case RSCACHE_CS2_INSN_ASSIGNMENT:
        cs2_emit_assignment(lo, insn);
        return;
    case RSCACHE_CS2_INSN_RETURN:
        cs2_emit_expr(lo, insn->expression);
        cs2_emit(lo, RSCACHE_CS2_OP_RETURN, 0);
        if( insn->is_epilogue_return )
            lo->emitted_epilogue = true;
        if( insn->dead_goto_follows && lo->options->keep_dead_gotos &&
            insn->dead_goto_target )
            cs2_emit_jump(lo, RSCACHE_CS2_OP_BRANCH, insn->dead_goto_target);
        return;
    case RSCACHE_CS2_INSN_BRANCH:
        cs2_emit_branch(lo, insn);
        return;
    case RSCACHE_CS2_INSN_SWITCH:
        cs2_emit_switch(lo, insn);
        return;
    case RSCACHE_CS2_INSN_GOTO:
        cs2_emit_jump(lo, RSCACHE_CS2_OP_BRANCH, insn->label);
        return;
    }
    cs2_lower_fail(lo, "script %d: instruction kind %d has no bytecode", lo->function->id,
                   (int)insn->kind);
}

/* -------------------------------------------------------------------------
 * Driver
 * ---------------------------------------------------------------------- */

/** The op index a label sits at, or -1 when it never reached the stream. */
static int
cs2_label_position(struct cs2_lower* lo, struct RSCache_CS2_Insn* label)
{
    assert(lo);
    assert(label);
    void* stored = RSCache_CS2_MapGet(&lo->label_index, label);
    if( !stored )
        return -1;
    return (int)(intptr_t)stored - 1;
}

static void
cs2_lower_free(struct cs2_lower* lo)
{
    assert(lo);
    for( int i = 0; i < lo->count; i++ )
        free(lo->string_operands ? lo->string_operands[i] : NULL);
    free(lo->opcodes);
    free(lo->int_operands);
    free(lo->string_operands);
    free(lo->fixups);
    for( int i = 0; i < lo->switch_count; i++ )
    {
        free(lo->switches[i].keys);
        free(lo->switches[i].labels);
    }
    free(lo->switches);
    RSCache_CS2_MapFree(&lo->label_index);
    RSCache_CS2_MapFree(&lo->spill_slots);
}

/** Highest argument slot per bank, so the trailer covers arguments it never reads. */
static void
cs2_argument_extents(
    struct RSCache_CS2_Function* function,
    int* out_int_count,
    int* out_string_count)
{
    assert(function);
    assert(out_int_count);
    assert(out_string_count);
    int ints = 0;
    int strings = 0;
    for( int i = 0; i < function->arguments.count; i++ )
    {
        struct RSCache_CS2_Variable* variable =
            (struct RSCache_CS2_Variable*)function->arguments.items[i];
        assert(variable);
        /*
         * An array argument is a string-bank argument.
         *
         * `VarStackType` calls an array int-typed, which is what its *elements*
         * mostly are; the handle itself rides a string local at this revision,
         * which is why `cs2_find_array_args` will only claim an argument whose
         * bank is already string. Counting it as an int rewrote six scripts'
         * trailers from 0i/1s to 1i/1s.
         */
        if( variable->kind == RSCACHE_CS2_VAR_ARRAY ||
            RSCache_CS2_VarStackType(variable->kind) == RSCACHE_CS2_STACK_STRING )
            strings++;
        else
            ints++;
    }
    *out_int_count = ints;
    *out_string_count = strings;
}

bool
RSCache_CS2_Lower(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_LowerOptions* options,
    struct RSCache_CS2_Script* out,
    char* error,
    int error_capacity)
{
    assert(fs);
    assert(function);
    assert(options);
    assert(out);

    if( error && error_capacity > 0 )
        error[0] = '\0';

    struct cs2_lower lo;
    memset(&lo, 0, sizeof(lo));
    lo.fs = fs;
    lo.function = function;
    lo.options = options;
    lo.error = error;
    lo.error_capacity = error_capacity;
    lo.max_int_local = -1;
    lo.max_string_local = -1;
    RSCache_CS2_MapInit(&lo.label_index);
    RSCache_CS2_MapInit(&lo.spill_slots);

    /*
     * Spill slots are handed out above whatever the body already uses, and the
     * body has not been walked yet — so the counters are seeded from the
     * declared frame first. A leftover stack variable is rare (it takes a value
     * that crosses a merge), and seeding from the declaration rather than from
     * the true high-water mark costs at most a few unused slots.
     */
    int argument_ints = 0;
    int argument_strings = 0;
    cs2_argument_extents(function, &argument_ints, &argument_strings);
    lo.spill_int_next = function->original_local_int_count > argument_ints
                            ? function->original_local_int_count
                            : argument_ints;
    lo.spill_string_next = function->original_local_string_count > argument_strings
                               ? function->original_local_string_count
                               : argument_strings;

    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn && !lo.failed;
         insn = insn->next )
        cs2_emit_insn(&lo, insn);

    /* Branch operands count from the instruction after the branch. */
    for( int i = 0; i < lo.fixup_count && !lo.failed; i++ )
    {
        int position = cs2_label_position(&lo, lo.fixups[i].label);
        if( position < 0 )
        {
            cs2_lower_fail(&lo, "script %d: a jump targets a label that was never emitted",
                           function->id);
            break;
        }
        lo.int_operands[lo.fixups[i].op_index] = position - (lo.fixups[i].op_index + 1);
    }
    for( int i = 0; i < lo.switch_count && !lo.failed; i++ )
    {
        for( int j = 0; j < lo.switches[i].count; j++ )
        {
            int position = cs2_label_position(&lo, lo.switches[i].labels[j]);
            if( position < 0 )
            {
                cs2_lower_fail(&lo, "script %d: a switch case targets an unemitted label",
                               function->id);
                break;
            }
            lo.switches[i].labels[j] = (struct RSCache_CS2_Insn*)(intptr_t)position;
        }
    }

    /*
     * Put the closing epilogue back if the body no longer ends in it.
     *
     * It is unreachable in most scripts, so `cs2_remove_dead_code` deleted it —
     * and it is the only statement of what the script returns, which every
     * caller's typing is derived from. Restored rather than synthesised, so a
     * slot produced by a zero-argument command comes back as that command.
     */
    if( !lo.failed && !lo.emitted_epilogue )
    {
        /* Verbatim, including its own closing return — the recorded region is a
         * complete suffix, not a list of values to wrap. */
        for( int i = 0; i < function->epilogue_count && !lo.failed; i++ )
        {
            const struct RSCache_CS2_EpilogueOp* op = &function->epilogue[i];
            if( op->opcode == RSCACHE_CS2_OP_PUSH_CONSTANT_STRING )
                cs2_emit_string(&lo, op->text);
            else
                cs2_emit(&lo, op->opcode, op->operand);
        }
        if( function->epilogue_count == 0 )
            cs2_emit(&lo, RSCACHE_CS2_OP_RETURN, 0);
    }

    if( lo.count == 0 || lo.opcodes[lo.count - 1] != RSCACHE_CS2_OP_RETURN )
        cs2_lower_fail(&lo, "script %d: the body does not end in a return", function->id);

    if( lo.failed )
    {
        cs2_lower_free(&lo);
        return false;
    }

    /*
     * The switch table's own position is where its cases are measured from,
     * which is only known once every table has been placed. Walked here rather
     * than remembered per table, because a table's index is its operand.
     */
    int* switch_pcs = (int*)calloc((size_t)(lo.switch_count > 0 ? lo.switch_count : 1),
                                   sizeof(int));
    assert(switch_pcs);
    for( int i = 0; i < lo.count; i++ )
    {
        if( lo.opcodes[i] != RSCACHE_CS2_OP_SWITCH )
            continue;
        if( lo.int_operands[i] >= 0 && lo.int_operands[i] < lo.switch_count )
            switch_pcs[lo.int_operands[i]] = i;
    }

    memset(out, 0, sizeof(*out));
    RSCache_CS2_ScriptInit(out);
    out->script_id = function->id;
    size_t signature_length = options->signature ? strlen(options->signature) : 0;
    out->signature = (char*)calloc(signature_length + 1, 1);
    assert(out->signature);
    if( signature_length )
        memcpy(out->signature, options->signature, signature_length);
    out->op_count = lo.count;
    out->opcodes = lo.opcodes;
    out->int_operands = lo.int_operands;
    out->string_operands = lo.string_operands;
    out->long_operands = (int64_t*)calloc((size_t)(lo.count > 0 ? lo.count : 1),
                                          sizeof(int64_t));
    assert(out->long_operands);
    out->int_argument_count = function->original_int_argument_count > 0
                                  ? function->original_int_argument_count
                                  : argument_ints;
    out->string_argument_count = function->original_string_argument_count > 0
                                     ? function->original_string_argument_count
                                     : argument_strings;

    /*
     * Reads count towards the frame, not just writes: a script may push a local
     * it never assigned (the frame starts zeroed, so that is ordinary), and the
     * original's trailer counted it or the read would have been out of range.
     */
    int computed_ints = lo.max_int_local + 1;
    int computed_strings = lo.max_string_local + 1;
    if( computed_ints < argument_ints )
        computed_ints = argument_ints;
    if( computed_strings < argument_strings )
        computed_strings = argument_strings;
    if( options->preserve_frame_counts )
    {
        if( function->original_local_int_count > computed_ints )
            computed_ints = function->original_local_int_count;
        if( function->original_local_string_count > computed_strings )
            computed_strings = function->original_local_string_count;
    }
    out->local_int_count = computed_ints;
    out->local_string_count = computed_strings;

    bool ok = true;
    if( RSCache_CS2_ScriptAllocSwitches(out, lo.switch_count) )
    {
        for( int i = 0; i < lo.switch_count && ok; i++ )
        {
            if( !RSCache_CS2_ScriptAllocSwitchCases(out, i, lo.switches[i].count) )
            {
                ok = false;
                break;
            }
            for( int j = 0; j < lo.switches[i].count; j++ )
            {
                out->switch_tables[i].cases[j].key = lo.switches[i].keys[j];
                /* Cases are relative to the instruction after the switch. */
                out->switch_tables[i].cases[j].target_pc =
                    (int)(intptr_t)lo.switches[i].labels[j] - (switch_pcs[i] + 1);
            }
        }
    }
    else
    {
        ok = false;
    }
    free(switch_pcs);

    if( !ok )
    {
        /* The op arrays already belong to `out`; freeing it releases them. */
        RSCache_CS2_ScriptFree(out);
        memset(out, 0, sizeof(*out));
        lo.opcodes = NULL;
        lo.int_operands = NULL;
        lo.string_operands = NULL;
        lo.count = 0;
        cs2_lower_free(&lo);
        if( error && error_capacity > 0 && !error[0] )
            snprintf(error, (size_t)error_capacity, "script %d: out of memory", function->id);
        return false;
    }

    /* Ownership has moved; keep the teardown from touching them. */
    lo.opcodes = NULL;
    lo.int_operands = NULL;
    lo.string_operands = NULL;
    lo.count = 0;
    cs2_lower_free(&lo);
    return true;
}

/* -------------------------------------------------------------------------
 * Comparison
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_ScriptBytesEqual(
    const struct RSCache_CS2_Script* left,
    const struct RSCache_CS2_Script* right)
{
    assert(left);
    assert(right);
    if( left->op_count != right->op_count || left->local_int_count != right->local_int_count ||
        left->local_string_count != right->local_string_count ||
        left->int_argument_count != right->int_argument_count ||
        left->string_argument_count != right->string_argument_count ||
        left->switch_table_count != right->switch_table_count )
        return false;
    for( int i = 0; i < left->op_count; i++ )
    {
        if( left->opcodes[i] != right->opcodes[i] )
            return false;
        if( left->opcodes[i] == RSCACHE_CS2_OP_PUSH_CONSTANT_STRING )
        {
            const char* a = left->string_operands ? left->string_operands[i] : NULL;
            const char* b = right->string_operands ? right->string_operands[i] : NULL;
            if( strcmp(a ? a : "", b ? b : "") != 0 )
                return false;
            continue;
        }
        if( left->int_operands[i] != right->int_operands[i] )
            return false;
    }
    for( int i = 0; i < left->switch_table_count; i++ )
    {
        if( left->switch_tables[i].case_count != right->switch_tables[i].case_count )
            return false;
        for( int j = 0; j < left->switch_tables[i].case_count; j++ )
        {
            if( left->switch_tables[i].cases[j].key != right->switch_tables[i].cases[j].key ||
                left->switch_tables[i].cases[j].target_pc !=
                    right->switch_tables[i].cases[j].target_pc )
                return false;
        }
    }
    return true;
}
