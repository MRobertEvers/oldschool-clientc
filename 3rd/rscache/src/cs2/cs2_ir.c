#include "cs2_ir.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Variables
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_VarIsGlobal(enum RSCache_CS2_VarKind kind)
{
    return kind <= RSCACHE_CS2_VAR_VARCLAN;
}

bool
RSCache_CS2_VarIsLocal(enum RSCache_CS2_VarKind kind)
{
    return kind >= RSCACHE_CS2_VAR_STACKINT;
}

bool
RSCache_CS2_VarIsStack(enum RSCache_CS2_VarKind kind)
{
    return kind == RSCACHE_CS2_VAR_STACKINT || kind == RSCACHE_CS2_VAR_STACKSTRING;
}

enum RSCache_CS2_StackType
RSCache_CS2_VarStackType(enum RSCache_CS2_VarKind kind)
{
    switch( kind )
    {
    case RSCACHE_CS2_VAR_VARCSTRING:
    case RSCACHE_CS2_VAR_STACKSTRING:
    case RSCACHE_CS2_VAR_STRING:
        return RSCACHE_CS2_STACK_STRING;
    default:
        /* Arrays are int-typed handles; their elements carry their own type. */
        return RSCACHE_CS2_STACK_INT;
    }
}

/* -------------------------------------------------------------------------
 * Event properties
 * ---------------------------------------------------------------------- */

struct cs2_event_info
{
    const char* literal;
    enum RSCache_CS2_ProtoId proto;
    /* Magic operand: a string for opbase, otherwise INT_MIN + n. */
    const char* magic_string;
    int magic_int;
};

static const struct cs2_event_info cs2_events[RSCACHE_CS2_EVENT_COUNT] = {
    [RSCACHE_CS2_EVENT_OPBASE] = { "event_opbase",
                                   RSCACHE_CS2_PROTO_OPBASE,
                                   "event_opbase",
                                   0 },
    [RSCACHE_CS2_EVENT_MOUSEX] = { "event_mousex", RSCACHE_CS2_PROTO_MOUSEX, NULL, INT_MIN + 1 },
    [RSCACHE_CS2_EVENT_MOUSEY] = { "event_mousey", RSCACHE_CS2_PROTO_MOUSEY, NULL, INT_MIN + 2 },
    [RSCACHE_CS2_EVENT_COM] = { "event_com", RSCACHE_CS2_PROTO_COMPONENT, NULL, INT_MIN + 3 },
    [RSCACHE_CS2_EVENT_OPINDEX] = { "event_opindex", RSCACHE_CS2_PROTO_OPINDEX, NULL, INT_MIN + 4 },
    [RSCACHE_CS2_EVENT_COMSUBID] = { "event_comsubid",
                                     RSCACHE_CS2_PROTO_COMSUBID,
                                     NULL,
                                     INT_MIN + 5 },
    [RSCACHE_CS2_EVENT_DROP] = { "event_drop", RSCACHE_CS2_PROTO_DROP, NULL, INT_MIN + 6 },
    [RSCACHE_CS2_EVENT_DROPSUBID] = { "event_dropsubid",
                                      RSCACHE_CS2_PROTO_DROPSUBID,
                                      NULL,
                                      INT_MIN + 7 },
    [RSCACHE_CS2_EVENT_KEY] = { "event_key", RSCACHE_CS2_PROTO_KEY, NULL, INT_MIN + 8 },
    [RSCACHE_CS2_EVENT_KEYCHAR] = { "event_keychar", RSCACHE_CS2_PROTO_KEYCHAR, NULL, INT_MIN + 9 },
};

bool
RSCache_CS2_EventPropertyMagic(
    enum RSCache_CS2_EventProperty property,
    struct RSCache_CS2_Value* out)
{
    assert(out);
    if( property < 0 || property >= RSCACHE_CS2_EVENT_COUNT )
        return false;
    const struct cs2_event_info* info = &cs2_events[property];
    memset(out, 0, sizeof(*out));
    if( info->magic_string )
    {
        out->stack_type = RSCACHE_CS2_STACK_STRING;
        out->string_value = info->magic_string;
        return true;
    }
    out->stack_type = RSCACHE_CS2_STACK_INT;
    out->int_value = info->magic_int;
    return true;
}

enum RSCache_CS2_EventProperty
RSCache_CS2_EventPropertyOf(const struct RSCache_CS2_Value* value)
{
    if( !value )
        return RSCACHE_CS2_EVENT_NONE;
    for( int i = 0; i < RSCACHE_CS2_EVENT_COUNT; i++ )
    {
        const struct cs2_event_info* info = &cs2_events[i];
        if( info->magic_string )
        {
            if( value->stack_type == RSCACHE_CS2_STACK_STRING && value->string_value &&
                strcmp(value->string_value, info->magic_string) == 0 )
                return (enum RSCache_CS2_EventProperty)i;
        }
        else if( value->stack_type == RSCACHE_CS2_STACK_INT && value->int_value == info->magic_int )
        {
            return (enum RSCache_CS2_EventProperty)i;
        }
    }
    return RSCACHE_CS2_EVENT_NONE;
}

const char*
RSCache_CS2_EventPropertyLiteral(enum RSCache_CS2_EventProperty property)
{
    assert(property >= 0 && property < RSCACHE_CS2_EVENT_COUNT);
    return cs2_events[property].literal;
}

enum RSCache_CS2_ProtoId
RSCache_CS2_EventPropertyProto(enum RSCache_CS2_EventProperty property)
{
    assert(property >= 0 && property < RSCACHE_CS2_EVENT_COUNT);
    return cs2_events[property].proto;
}

/* -------------------------------------------------------------------------
 * Expressions
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_ExprIsElement(const struct RSCache_CS2_Expr* expr)
{
    return expr && expr->kind <= RSCACHE_CS2_EXPR_EVENT_PROPERTY;
}

void
RSCache_CS2_ExprAsList(
    struct RSCache_CS2_Expr* expr,
    struct RSCache_CS2_Expr*** out_items,
    int* out_count,
    struct RSCache_CS2_Expr** single_slot)
{
    if( expr && expr->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        *out_items = (struct RSCache_CS2_Expr**)expr->children.items;
        *out_count = expr->children.count;
        return;
    }
    *single_slot = expr;
    *out_items = single_slot;
    *out_count = expr ? 1 : 0;
}

int
RSCache_CS2_ExprStackTypeCount(const struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return 0;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
    case RSCACHE_CS2_EXPR_ACCESS:
    case RSCACHE_CS2_EXPR_POINTER:
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
        return 1;
    case RSCACHE_CS2_EXPR_COMPOUND:
    {
        int total = 0;
        for( int i = 0; i < expr->children.count; i++ )
            total += RSCache_CS2_ExprStackTypeCount(
                (const struct RSCache_CS2_Expr*)expr->children.items[i]);
        return total;
    }
    case RSCACHE_CS2_EXPR_OPERATION:
    case RSCACHE_CS2_EXPR_PROC:
        return expr->stack_type_count;
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
    default:
        /* A hook registration is a statement, never a value. */
        return 0;
    }
}

static enum RSCache_CS2_StackType
cs2_element_stack_type(const struct RSCache_CS2_Expr* expr)
{
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
        return expr->value.stack_type;
    case RSCACHE_CS2_EXPR_ACCESS:
    case RSCACHE_CS2_EXPR_POINTER:
        return RSCache_CS2_VarStackType(expr->variable->kind);
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
    default:
        return RSCache_CS2_TypeStackType(
            RSCache_CS2_ProtoGet(RSCache_CS2_EventPropertyProto(expr->event_property))->type);
    }
}

enum RSCache_CS2_StackType
RSCache_CS2_ExprStackTypeAt(const struct RSCache_CS2_Expr* expr, int index)
{
    assert(expr);
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
    case RSCACHE_CS2_EXPR_ACCESS:
    case RSCACHE_CS2_EXPR_POINTER:
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
        assert(index == 0);
        return cs2_element_stack_type(expr);
    case RSCACHE_CS2_EXPR_COMPOUND:
    {
        for( int i = 0; i < expr->children.count; i++ )
        {
            const struct RSCache_CS2_Expr* child =
                (const struct RSCache_CS2_Expr*)expr->children.items[i];
            int n = RSCache_CS2_ExprStackTypeCount(child);
            if( index < n )
                return RSCache_CS2_ExprStackTypeAt(child, index);
            index -= n;
        }
        assert(false && "stack type index out of range");
        return RSCACHE_CS2_STACK_INT;
    }
    case RSCACHE_CS2_EXPR_OPERATION:
    case RSCACHE_CS2_EXPR_PROC:
        assert(index >= 0 && index < expr->stack_type_count);
        return expr->stack_types[index];
    default:
        assert(false && "expression leaves nothing on the stack");
        return RSCACHE_CS2_STACK_INT;
    }
}

static struct RSCache_CS2_Expr*
cs2_expr_new(struct RSCache_CS2_Arena* arena, enum RSCache_CS2_ExprKind kind)
{
    struct RSCache_CS2_Expr* expr =
        (struct RSCache_CS2_Expr*)RSCache_CS2_ArenaAlloc(arena, sizeof(*expr));
    expr->kind = kind;
    RSCache_CS2_VecInit(&expr->children);
    RSCache_CS2_ArenaTrackVec(arena, &expr->children);
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprConstantInt(struct RSCache_CS2_Arena* arena, int value)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_CONSTANT);
    expr->value.stack_type = RSCACHE_CS2_STACK_INT;
    expr->value.int_value = value;
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprConstantString(struct RSCache_CS2_Arena* arena, const char* value)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_CONSTANT);
    expr->value.stack_type = RSCACHE_CS2_STACK_STRING;
    expr->value.string_value = value ? value : "";
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprAccess(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Variable* variable,
    const struct RSCache_CS2_Value* value)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_ACCESS);
    expr->variable = variable;
    if( value )
    {
        assert(RSCache_CS2_VarStackType(variable->kind) == value->stack_type);
        expr->value = *value;
        expr->has_value = true;
    }
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprPointer(struct RSCache_CS2_Arena* arena, struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_POINTER);
    expr->variable = variable;
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprEventProperty(
    struct RSCache_CS2_Arena* arena,
    enum RSCache_CS2_EventProperty property)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_EVENT_PROPERTY);
    expr->event_property = property;
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprEmpty(struct RSCache_CS2_Arena* arena)
{
    return cs2_expr_new(arena, RSCACHE_CS2_EXPR_COMPOUND);
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprFromList(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* const* items,
    int count)
{
    if( count == 1 )
        return items[0];

    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_COMPOUND);
    for( int i = 0; i < count; i++ )
    {
        struct RSCache_CS2_Expr* item = items[i];
        if( !item )
            continue;
        /* Compounds never nest: splicing here is what keeps `asList` a flat
         * view, which every consumer of the IR relies on. */
        if( item->kind == RSCACHE_CS2_EXPR_COMPOUND )
        {
            for( int j = 0; j < item->children.count; j++ )
                RSCache_CS2_VecPush(&expr->children, item->children.items[j]);
        }
        else
        {
            RSCache_CS2_VecPush(&expr->children, item);
        }
    }
    if( expr->children.count == 1 )
        return (struct RSCache_CS2_Expr*)expr->children.items[0];
    return expr;
}

static enum RSCache_CS2_StackType*
cs2_copy_stack_types(
    struct RSCache_CS2_Arena* arena,
    const enum RSCache_CS2_StackType* stack_types,
    int count)
{
    if( count <= 0 )
        return NULL;
    enum RSCache_CS2_StackType* out = (enum RSCache_CS2_StackType*)RSCache_CS2_ArenaAlloc(
        arena, (size_t)count * sizeof(*out));
    memcpy(out, stack_types, (size_t)count * sizeof(*out));
    return out;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprOperation(
    struct RSCache_CS2_Arena* arena,
    const enum RSCache_CS2_StackType* stack_types,
    int stack_type_count,
    int opcode,
    struct RSCache_CS2_Expr* arguments,
    bool dot)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_OPERATION);
    expr->opcode = opcode;
    expr->arguments = arguments;
    expr->dot = dot;
    expr->stack_types = cs2_copy_stack_types(arena, stack_types, stack_type_count);
    expr->stack_type_count = stack_type_count;
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprProc(
    struct RSCache_CS2_Arena* arena,
    const enum RSCache_CS2_StackType* stack_types,
    int stack_type_count,
    int script_id,
    struct RSCache_CS2_Expr* arguments)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_PROC);
    expr->opcode = RSCACHE_CS2_OP_GOSUB_WITH_PARAMS;
    expr->script_id = script_id;
    expr->arguments = arguments;
    expr->stack_types = cs2_copy_stack_types(arena, stack_types, stack_type_count);
    expr->stack_type_count = stack_type_count;
    return expr;
}

struct RSCache_CS2_Expr*
RSCache_CS2_ExprClientScript(
    struct RSCache_CS2_Arena* arena,
    int opcode,
    int script_id,
    struct RSCache_CS2_Expr* arguments,
    struct RSCache_CS2_Expr* triggers,
    bool dot,
    struct RSCache_CS2_Expr* component,
    const char* hook_descriptor)
{
    struct RSCache_CS2_Expr* expr = cs2_expr_new(arena, RSCACHE_CS2_EXPR_CLIENTSCRIPT);
    expr->opcode = opcode;
    expr->script_id = script_id;
    expr->arguments = arguments;
    expr->triggers = triggers;
    expr->dot = dot;
    expr->component = component;
    expr->hook_descriptor = hook_descriptor;
    return expr;
}

/* -------------------------------------------------------------------------
 * Instructions
 * ---------------------------------------------------------------------- */

static struct RSCache_CS2_Insn*
cs2_insn_new(struct RSCache_CS2_Arena* arena, enum RSCache_CS2_InsnKind kind)
{
    struct RSCache_CS2_Insn* insn =
        (struct RSCache_CS2_Insn*)RSCache_CS2_ArenaAlloc(arena, sizeof(*insn));
    insn->kind = kind;
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnAssignment(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* definitions,
    struct RSCache_CS2_Expr* expression)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_ASSIGNMENT);
    insn->definitions = definitions;
    insn->expression = expression;
    /* Either the definitions match the value's arity, or there are none — the
     * "call for effect, discard results" case. */
    assert(
        RSCache_CS2_ExprStackTypeCount(definitions) ==
            RSCache_CS2_ExprStackTypeCount(expression) ||
        RSCache_CS2_ExprStackTypeCount(definitions) == 0);
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnReturn(struct RSCache_CS2_Arena* arena, struct RSCache_CS2_Expr* expression)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_RETURN);
    insn->expression = expression;
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnBranch(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* expression,
    int target_id)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_BRANCH);
    insn->expression = expression;
    insn->target_id = target_id;
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnSwitch(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* expression,
    const int* keys,
    const int* target_ids,
    int count)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_SWITCH);
    insn->expression = expression;
    insn->case_count = count;
    if( count > 0 )
    {
        insn->case_keys = (int*)RSCache_CS2_ArenaAlloc(arena, (size_t)count * sizeof(int));
        insn->case_target_ids = (int*)RSCache_CS2_ArenaAlloc(arena, (size_t)count * sizeof(int));
        insn->case_labels = (struct RSCache_CS2_Insn**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)count * sizeof(struct RSCache_CS2_Insn*));
        memcpy(insn->case_keys, keys, (size_t)count * sizeof(int));
        memcpy(insn->case_target_ids, target_ids, (size_t)count * sizeof(int));
    }
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnLabel(struct RSCache_CS2_Arena* arena, int label_id)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_LABEL);
    insn->label_id = label_id;
    return insn;
}

struct RSCache_CS2_Insn*
RSCache_CS2_InsnGoto(struct RSCache_CS2_Arena* arena, int target_id)
{
    struct RSCache_CS2_Insn* insn = cs2_insn_new(arena, RSCACHE_CS2_INSN_GOTO);
    insn->target_id = target_id;
    return insn;
}

bool
RSCache_CS2_InsnIsEvaluation(const struct RSCache_CS2_Insn* insn)
{
    if( !insn )
        return false;
    switch( insn->kind )
    {
    case RSCACHE_CS2_INSN_ASSIGNMENT:
    case RSCACHE_CS2_INSN_RETURN:
    case RSCACHE_CS2_INSN_BRANCH:
    case RSCACHE_CS2_INSN_SWITCH:
        return true;
    default:
        return false;
    }
}

/* -------------------------------------------------------------------------
 * Chain
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_ChainInit(struct RSCache_CS2_Chain* chain)
{
    chain->first = NULL;
    chain->last = NULL;
    chain->count = 0;
}

void
RSCache_CS2_ChainAddLast(struct RSCache_CS2_Chain* chain, struct RSCache_CS2_Insn* insn)
{
    assert(!insn->linked);
    insn->prev = chain->last;
    insn->next = NULL;
    if( chain->last )
        chain->last->next = insn;
    else
        chain->first = insn;
    chain->last = insn;
    insn->linked = true;
    chain->count++;
}

void
RSCache_CS2_ChainInsertBefore(
    struct RSCache_CS2_Chain* chain,
    struct RSCache_CS2_Insn* insn,
    struct RSCache_CS2_Insn* point)
{
    assert(!insn->linked && point && point->linked);
    insn->prev = point->prev;
    insn->next = point;
    if( point->prev )
        point->prev->next = insn;
    else
        chain->first = insn;
    point->prev = insn;
    insn->linked = true;
    chain->count++;
}

void
RSCache_CS2_ChainInsertAfter(
    struct RSCache_CS2_Chain* chain,
    struct RSCache_CS2_Insn* insn,
    struct RSCache_CS2_Insn* point)
{
    assert(!insn->linked && point && point->linked);
    insn->next = point->next;
    insn->prev = point;
    if( point->next )
        point->next->prev = insn;
    else
        chain->last = insn;
    point->next = insn;
    insn->linked = true;
    chain->count++;
}

void
RSCache_CS2_ChainRemove(struct RSCache_CS2_Chain* chain, struct RSCache_CS2_Insn* insn)
{
    assert(insn && insn->linked);
    if( insn->prev )
        insn->prev->next = insn->next;
    else
        chain->first = insn->next;
    if( insn->next )
        insn->next->prev = insn->prev;
    else
        chain->last = insn->prev;
    insn->linked = false;
    /* prev/next are deliberately left intact: several passes remove an
     * instruction and then keep walking from it, exactly as the reference's
     * iterator does. */
    chain->count--;
}

/* -------------------------------------------------------------------------
 * Typings
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_TypingsInit(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Arena* arena)
{
    memset(typings, 0, sizeof(*typings));
    typings->arena = arena;
    RSCache_CS2_MapInit(&typings->constants);
    RSCache_CS2_MapInit(&typings->variables);
    RSCache_CS2_MapInit(&typings->operations);
    RSCache_CS2_IntMapInit(&typings->returns);
    RSCache_CS2_IntMapInit(&typings->args);
    RSCache_CS2_VecInit(&typings->all);
}

void
RSCache_CS2_TypingsFree(struct RSCache_CS2_Typings* typings)
{
    /* Each typing's three edge sets are arena-tracked, so they are released
     * with the arena rather than walked here — `all` no longer lists typings
     * that were removed mid-pass, and walking it would miss theirs. */
    RSCache_CS2_MapFree(&typings->constants);
    RSCache_CS2_MapFree(&typings->variables);
    RSCache_CS2_MapFree(&typings->operations);
    RSCache_CS2_IntMapFree(&typings->returns);
    RSCache_CS2_IntMapFree(&typings->args);
    RSCache_CS2_VecFree(&typings->all);
}

struct RSCache_CS2_Typing*
RSCache_CS2_TypingNew(struct RSCache_CS2_Typings* typings, enum RSCache_CS2_StackType stack_type)
{
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_ArenaAlloc(typings->arena, sizeof(*typing));
    typing->stack_type = stack_type;
    typing->type = RSCACHE_CS2_TYPE_NONE;
    typing->identifier = NULL;
    RSCache_CS2_VecInit(&typing->from);
    RSCache_CS2_VecInit(&typing->to);
    RSCache_CS2_VecInit(&typing->compare);
    RSCache_CS2_ArenaTrackVec(typings->arena, &typing->from);
    RSCache_CS2_ArenaTrackVec(typings->arena, &typing->to);
    RSCache_CS2_ArenaTrackVec(typings->arena, &typing->compare);
    RSCache_CS2_VecPush(&typings->all, typing);
    return typing;
}

bool
RSCache_CS2_TypingFreezeType(struct RSCache_CS2_Typing* typing, enum RSCache_CS2_Type type)
{
    if( RSCache_CS2_TypeStackType(type) != typing->stack_type )
        return false;
    typing->freeze_type = true;
    typing->type = type;
    return true;
}

void
RSCache_CS2_TypingFreezeIdentifier(struct RSCache_CS2_Typing* typing, const char* identifier)
{
    typing->freeze_identifier = true;
    typing->identifier = identifier;
}

bool
RSCache_CS2_TypingFreezeProto(struct RSCache_CS2_Typing* typing, enum RSCache_CS2_ProtoId proto)
{
    const struct RSCache_CS2_Prototype* prototype = RSCache_CS2_ProtoGet(proto);
    if( !RSCache_CS2_TypingFreezeType(typing, prototype->type) )
        return false;
    RSCache_CS2_TypingFreezeIdentifier(typing, prototype->identifier);
    return true;
}

bool
RSCache_CS2_TypingAssign(struct RSCache_CS2_Typing* from, struct RSCache_CS2_Typing* to)
{
    if( from == to )
        return false;
    if( from->stack_type != to->stack_type )
        return false;
    RSCache_CS2_VecAddUnique(&from->to, to);
    RSCache_CS2_VecAddUnique(&to->from, from);
    return true;
}

bool
RSCache_CS2_TypingAssignLists(
    const struct RSCache_CS2_TypingList* from,
    const struct RSCache_CS2_TypingList* to)
{
    if( from->count != to->count )
        return false;
    for( int i = 0; i < from->count; i++ )
    {
        if( from->items[i] == to->items[i] )
            continue;
        if( !RSCache_CS2_TypingAssign(from->items[i], to->items[i]) )
            return false;
    }
    return true;
}

bool
RSCache_CS2_TypingCompare(struct RSCache_CS2_Typing* a, struct RSCache_CS2_Typing* b)
{
    if( a == b )
        return false;
    if( a->stack_type != b->stack_type )
        return false;
    RSCache_CS2_VecAddUnique(&a->compare, b);
    RSCache_CS2_VecAddUnique(&b->compare, a);
    return true;
}

void
RSCache_CS2_TypingUnlink(struct RSCache_CS2_Typing* typing)
{
    for( int i = 0; i < typing->from.count; i++ )
        RSCache_CS2_VecRemove(&((struct RSCache_CS2_Typing*)typing->from.items[i])->to, typing);
    for( int i = 0; i < typing->to.count; i++ )
        RSCache_CS2_VecRemove(&((struct RSCache_CS2_Typing*)typing->to.items[i])->from, typing);
    for( int i = 0; i < typing->compare.count; i++ )
        RSCache_CS2_VecRemove(
            &((struct RSCache_CS2_Typing*)typing->compare.items[i])->compare, typing);
}

bool
RSCache_CS2_TypingReplace(struct RSCache_CS2_Typing* typing, struct RSCache_CS2_Typing* by)
{
    if( typing == by || typing->stack_type != by->stack_type )
        return false;
    /* Only an unsolved typing may be replaced; a solved one carries a decision
     * that would be silently discarded. */
    if( typing->type != RSCACHE_CS2_TYPE_NONE || typing->identifier != NULL )
        return false;

    for( int i = 0; i < typing->from.count; i++ )
    {
        struct RSCache_CS2_Typing* neighbour = (struct RSCache_CS2_Typing*)typing->from.items[i];
        RSCache_CS2_VecRemove(&neighbour->to, typing);
        if( neighbour != by )
        {
            RSCache_CS2_VecAddUnique(&neighbour->to, by);
            RSCache_CS2_VecAddUnique(&by->from, neighbour);
        }
    }
    for( int i = 0; i < typing->to.count; i++ )
    {
        struct RSCache_CS2_Typing* neighbour = (struct RSCache_CS2_Typing*)typing->to.items[i];
        RSCache_CS2_VecRemove(&neighbour->from, typing);
        if( neighbour != by )
        {
            RSCache_CS2_VecAddUnique(&neighbour->from, by);
            RSCache_CS2_VecAddUnique(&by->to, neighbour);
        }
    }
    for( int i = 0; i < typing->compare.count; i++ )
    {
        struct RSCache_CS2_Typing* neighbour = (struct RSCache_CS2_Typing*)typing->compare.items[i];
        RSCache_CS2_VecRemove(&neighbour->compare, typing);
        if( neighbour != by )
        {
            RSCache_CS2_VecAddUnique(&neighbour->compare, by);
            RSCache_CS2_VecAddUnique(&by->compare, neighbour);
        }
    }
    return true;
}

bool
RSCache_CS2_TypingReplaceLists(
    const struct RSCache_CS2_TypingList* typings,
    const struct RSCache_CS2_TypingList* by)
{
    if( typings->count != by->count )
        return false;
    bool ok = true;
    for( int i = 0; i < typings->count; i++ )
        ok = RSCache_CS2_TypingReplace(typings->items[i], by->items[i]) && ok;
    return ok;
}

static struct RSCache_CS2_TypingList*
cs2_typing_list_new(struct RSCache_CS2_Arena* arena, int count)
{
    struct RSCache_CS2_TypingList* list =
        (struct RSCache_CS2_TypingList*)RSCache_CS2_ArenaAlloc(arena, sizeof(*list));
    list->count = count;
    if( count > 0 )
        list->items = (struct RSCache_CS2_Typing**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)count * sizeof(struct RSCache_CS2_Typing*));
    return list;
}

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_MapGet(&typings->variables, variable);
    if( !typing )
    {
        typing = RSCache_CS2_TypingNew(typings, RSCache_CS2_VarStackType(variable->kind));
        RSCache_CS2_MapPut(&typings->variables, variable, typing);
    }
    return typing;
}

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfArray(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable,
    enum RSCache_CS2_StackType element_stack)
{
    assert(variable->kind == RSCACHE_CS2_VAR_ARRAY);
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_MapGet(&typings->variables, variable);
    if( !typing )
    {
        typing = RSCache_CS2_TypingNew(typings, element_stack);
        RSCache_CS2_MapPut(&typings->variables, variable, typing);
    }
    return typing;
}

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfProto(struct RSCache_CS2_Typings* typings, enum RSCache_CS2_ProtoId proto)
{
    assert(proto >= 0 && proto < RSCACHE_CS2_PROTO_COUNT_);
    if( !typings->prototypes[proto] )
    {
        const struct RSCache_CS2_Prototype* prototype = RSCache_CS2_ProtoGet(proto);
        struct RSCache_CS2_Typing* typing =
            RSCache_CS2_TypingNew(typings, RSCache_CS2_TypeStackType(prototype->type));
        RSCache_CS2_TypingFreezeProto(typing, proto);
        typings->prototypes[proto] = typing;
    }
    return typings->prototypes[proto];
}

struct RSCache_CS2_Typing*
RSCache_CS2_TypingsOfElement(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Expr* element)
{
    switch( element->kind )
    {
    case RSCACHE_CS2_EXPR_ACCESS:
    case RSCACHE_CS2_EXPR_POINTER:
        return RSCache_CS2_TypingsOfVariable(typings, element->variable);
    case RSCACHE_CS2_EXPR_CONSTANT:
    {
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)RSCache_CS2_MapGet(&typings->constants, element);
        if( !typing )
        {
            typing = RSCache_CS2_TypingNew(typings, element->value.stack_type);
            RSCache_CS2_MapPut(&typings->constants, element, typing);
        }
        return typing;
    }
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
    {
        enum RSCache_CS2_EventProperty property = element->event_property;
        if( !typings->event_properties[property] )
        {
            enum RSCache_CS2_ProtoId proto = RSCache_CS2_EventPropertyProto(property);
            struct RSCache_CS2_Typing* typing = RSCache_CS2_TypingNew(
                typings, RSCache_CS2_TypeStackType(RSCache_CS2_ProtoGet(proto)->type));
            RSCache_CS2_TypingFreezeProto(typing, proto);
            typings->event_properties[property] = typing;
        }
        return typings->event_properties[property];
    }
    default:
        assert(false && "not an element");
        return NULL;
    }
}

static void
cs2_collect_typings(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Expr* expr,
    struct RSCache_CS2_Vec* out);

static struct RSCache_CS2_TypingList*
cs2_typings_of_operation(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Expr* expr)
{
    struct RSCache_CS2_TypingList* list =
        (struct RSCache_CS2_TypingList*)RSCache_CS2_MapGet(&typings->operations, expr);
    if( !list )
    {
        list = cs2_typing_list_new(typings->arena, expr->stack_type_count);
        for( int i = 0; i < expr->stack_type_count; i++ )
            list->items[i] = RSCache_CS2_TypingNew(typings, expr->stack_types[i]);
        RSCache_CS2_MapPut(&typings->operations, expr, list);
    }
    return list;
}

static void
cs2_collect_typings(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Expr* expr,
    struct RSCache_CS2_Vec* out)
{
    if( !expr )
        return;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_COMPOUND:
        for( int i = 0; i < expr->children.count; i++ )
            cs2_collect_typings(typings, (struct RSCache_CS2_Expr*)expr->children.items[i], out);
        return;
    case RSCACHE_CS2_EXPR_PROC:
    {
        struct RSCache_CS2_TypingList* list = RSCache_CS2_TypingsReturns(
            typings, expr->script_id, expr->stack_types, expr->stack_type_count);
        for( int i = 0; i < list->count; i++ )
            RSCache_CS2_VecPush(out, list->items[i]);
        return;
    }
    case RSCACHE_CS2_EXPR_OPERATION:
    {
        struct RSCache_CS2_TypingList* list = cs2_typings_of_operation(typings, expr);
        for( int i = 0; i < list->count; i++ )
            RSCache_CS2_VecPush(out, list->items[i]);
        return;
    }
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        /* A hook registration produces nothing. */
        return;
    default:
        RSCache_CS2_VecPush(out, RSCache_CS2_TypingsOfElement(typings, expr));
        return;
    }
}

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsOfExpr(struct RSCache_CS2_Typings* typings, struct RSCache_CS2_Expr* expr)
{
    struct RSCache_CS2_Vec collected;
    RSCache_CS2_VecInit(&collected);
    cs2_collect_typings(typings, expr, &collected);

    struct RSCache_CS2_TypingList* list = cs2_typing_list_new(typings->arena, collected.count);
    for( int i = 0; i < collected.count; i++ )
        list->items[i] = (struct RSCache_CS2_Typing*)collected.items[i];
    RSCache_CS2_VecFree(&collected);
    return list;
}

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsOfProtos(
    struct RSCache_CS2_Typings* typings,
    const enum RSCache_CS2_ProtoId* protos,
    int count)
{
    struct RSCache_CS2_TypingList* list = cs2_typing_list_new(typings->arena, count);
    for( int i = 0; i < count; i++ )
        list->items[i] = RSCache_CS2_TypingsOfProto(typings, protos[i]);
    return list;
}

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsReturns(
    struct RSCache_CS2_Typings* typings,
    int script_id,
    const enum RSCache_CS2_StackType* stack_types,
    int count)
{
    struct RSCache_CS2_TypingList* list =
        (struct RSCache_CS2_TypingList*)RSCache_CS2_IntMapGet(&typings->returns, script_id);
    if( list )
        return list;
    list = cs2_typing_list_new(typings->arena, count);
    for( int i = 0; i < count; i++ )
        list->items[i] = RSCache_CS2_TypingNew(typings, stack_types[i]);
    RSCache_CS2_IntMapPut(&typings->returns, script_id, list);
    return list;
}

struct RSCache_CS2_TypingList*
RSCache_CS2_TypingsArgs(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_FunctionSet* fs,
    int script_id,
    const enum RSCache_CS2_StackType* stack_types,
    int count)
{
    struct RSCache_CS2_TypingList* list =
        (struct RSCache_CS2_TypingList*)RSCache_CS2_IntMapGet(&typings->args, script_id);
    if( list )
        return list;

    list = cs2_typing_list_new(typings->arena, count);
    /* Int and string arguments are numbered in separate sequences, matching how
     * the VM lays out a frame's locals. */
    int int_index = 0;
    int string_index = 0;
    for( int i = 0; i < count; i++ )
    {
        struct RSCache_CS2_Variable* variable;
        if( stack_types[i] == RSCACHE_CS2_STACK_INT )
            variable = RSCache_CS2_VarIntern(fs, RSCACHE_CS2_VAR_INT, script_id, int_index++);
        else
            variable =
                RSCache_CS2_VarIntern(fs, RSCACHE_CS2_VAR_STRING, script_id, string_index++);
        list->items[i] = RSCache_CS2_TypingsOfVariable(typings, variable);
    }
    RSCache_CS2_IntMapPut(&typings->args, script_id, list);
    return list;
}

void
RSCache_CS2_TypingsRemoveVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_MapRemove(&typings->variables, variable);
    if( !typing )
        return;
    RSCache_CS2_TypingUnlink(typing);
    RSCache_CS2_VecRemove(&typings->all, typing);
}

void
RSCache_CS2_TypingsRemoveConstant(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Expr* constant)
{
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_MapRemove(&typings->constants, constant);
    if( !typing )
        return;
    RSCache_CS2_TypingUnlink(typing);
    RSCache_CS2_VecRemove(&typings->all, typing);
}

void
RSCache_CS2_TypingsForgetVariable(
    struct RSCache_CS2_Typings* typings,
    struct RSCache_CS2_Variable* variable)
{
    struct RSCache_CS2_Typing* typing =
        (struct RSCache_CS2_Typing*)RSCache_CS2_MapRemove(&typings->variables, variable);
    if( typing )
        RSCache_CS2_VecRemove(&typings->all, typing);
}

/* -------------------------------------------------------------------------
 * Function set
 * ---------------------------------------------------------------------- */

void
RSCache_CS2_FunctionSetInit(struct RSCache_CS2_FunctionSet* fs)
{
    memset(fs, 0, sizeof(*fs));
    RSCache_CS2_ArenaInit(&fs->arena);
    RSCache_CS2_IntMapInit(&fs->functions);
    RSCache_CS2_IntVecInit(&fs->function_ids);
    RSCache_CS2_IntMapInit(&fs->call_triggers);
    RSCache_CS2_VecInit(&fs->interned_variables);
    RSCache_CS2_MapInit(&fs->variable_index);
    RSCache_CS2_TypingsInit(&fs->typings, &fs->arena);
}

void
RSCache_CS2_FunctionSetFree(struct RSCache_CS2_FunctionSet* fs)
{
    if( !fs )
        return;
    /* Expression children, function arguments and typing edge sets are all
     * registered with the arena, so freeing it releases them too. */
    RSCache_CS2_TypingsFree(&fs->typings);
    RSCache_CS2_IntMapFree(&fs->functions);
    RSCache_CS2_IntVecFree(&fs->function_ids);
    RSCache_CS2_IntMapFree(&fs->call_triggers);
    RSCache_CS2_VecFree(&fs->interned_variables);
    RSCache_CS2_MapFree(&fs->variable_index);
    RSCache_CS2_ArenaFree(&fs->arena);
}

struct RSCache_CS2_Function*
RSCache_CS2_FunctionSetGet(struct RSCache_CS2_FunctionSet* fs, int script_id)
{
    return (struct RSCache_CS2_Function*)RSCache_CS2_IntMapGet(&fs->functions, script_id);
}

struct RSCache_CS2_Variable*
RSCache_CS2_VarIntern(
    struct RSCache_CS2_FunctionSet* fs,
    enum RSCache_CS2_VarKind kind,
    int script,
    int id)
{
    if( RSCache_CS2_VarIsGlobal(kind) )
        script = 0;

    /* Linear scan of a per-(kind, script) bucket. The reference gets value
     * equality free from Kotlin data classes; this is the cheapest thing that
     * restores it without a second hash implementation. Buckets stay short --
     * a script has a few dozen locals, and the id is the index within one. */
    for( int i = 0; i < fs->interned_variables.count; i++ )
    {
        struct RSCache_CS2_Variable* variable =
            (struct RSCache_CS2_Variable*)fs->interned_variables.items[i];
        if( variable->kind == kind && variable->script == script && variable->id == id )
            return variable;
    }

    struct RSCache_CS2_Variable* variable =
        (struct RSCache_CS2_Variable*)RSCache_CS2_ArenaAlloc(&fs->arena, sizeof(*variable));
    variable->kind = kind;
    variable->script = script;
    variable->id = id;
    RSCache_CS2_VecPush(&fs->interned_variables, variable);
    return variable;
}
