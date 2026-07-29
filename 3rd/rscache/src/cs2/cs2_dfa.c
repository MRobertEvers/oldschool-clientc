#include "cs2_dfa.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cs2_dfa
{
    struct RSCache_CS2_FunctionSet* fs;
    bool failed;
    char* error;
    int error_capacity;
};


/*
 * Spell a set of types into a caller buffer, for the "irreconcilable" failures.
 *
 * A contradiction is only ever actionable if you know which two types met — the
 * conflict is almost always one command's argument declared at the wrong type,
 * and the pair names it. Without them the message says a decompile failed and
 * nothing about where to look.
 */
static const char*
cs2_type_set_text(
    const enum RSCache_CS2_Type* types,
    int count,
    char* out,
    size_t capacity)
{
    size_t written = 0;
    out[0] = '\0';
    for( int i = 0; i < count && written + 1 < capacity; i++ )
    {
        const char* literal = RSCache_CS2_TypeLiteral(types[i]);
        int n = snprintf(out + written, capacity - written, "%s%s", written ? "/" : "",
                         literal ? literal : "?");
        if( n < 0 )
            break;
        written += (size_t)n;
    }
    return out;
}

static void
cs2_dfa_fail(struct cs2_dfa* dfa, const char* fmt, ...)
{
    if( dfa->failed )
        return;
    dfa->failed = true;
    if( !dfa->error || dfa->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(dfa->error, (size_t)dfa->error_capacity, fmt, args);
    va_end(args);
}

/* -------------------------------------------------------------------------
 * Shared helpers
 * ---------------------------------------------------------------------- */

/**
 * Concatenate two expressions' flat lists — the reference's `Expression.plus`.
 */
static struct RSCache_CS2_Expr*
cs2_expr_concat(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Expr* first,
    struct RSCache_CS2_Expr* second)
{
    struct RSCache_CS2_Expr* single_a = NULL;
    struct RSCache_CS2_Expr* single_b = NULL;
    struct RSCache_CS2_Expr** items_a = NULL;
    struct RSCache_CS2_Expr** items_b = NULL;
    int count_a = 0;
    int count_b = 0;
    RSCache_CS2_ExprAsList(first, &items_a, &count_a, &single_a);
    RSCache_CS2_ExprAsList(second, &items_b, &count_b, &single_b);

    struct RSCache_CS2_Vec joined;
    RSCache_CS2_VecInit(&joined);
    for( int i = 0; i < count_a; i++ )
        RSCache_CS2_VecPush(&joined, items_a[i]);
    for( int i = 0; i < count_b; i++ )
        RSCache_CS2_VecPush(&joined, items_b[i]);

    struct RSCache_CS2_Expr* out = RSCache_CS2_ExprFromList(
        arena, (struct RSCache_CS2_Expr* const*)joined.items, joined.count);
    RSCache_CS2_VecFree(&joined);
    return out;
}

static bool
cs2_is_stack_access(const struct RSCache_CS2_Expr* expr)
{
    return expr && expr->kind == RSCACHE_CS2_EXPR_ACCESS &&
           RSCache_CS2_VarIsStack(expr->variable->kind);
}

/* -------------------------------------------------------------------------
 * RemoveDeadCode
 *
 * Everything between a `return` and the next label is unreachable. The
 * reference asserts each dead instruction is a constant pushed into a stack
 * slot, which is what the compiler emits there; this drops the instruction
 * either way and only releases typings when the shape does match, so a script
 * with some other dead shape decompiles instead of aborting.
 * ---------------------------------------------------------------------- */

static void
cs2_remove_dead_code(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    struct RSCache_CS2_Insn* insn = function->instructions.first;
    while( insn )
    {
        if( insn->kind != RSCACHE_CS2_INSN_RETURN )
        {
            insn = insn->next;
            continue;
        }
        insn = insn->next;
        while( insn && insn->kind != RSCACHE_CS2_INSN_LABEL )
        {
            if( insn->kind == RSCACHE_CS2_INSN_ASSIGNMENT )
            {
                struct RSCache_CS2_Expr* definitions = insn->definitions;
                struct RSCache_CS2_Expr* expression = insn->expression;
                if( cs2_is_stack_access(definitions) )
                    RSCache_CS2_TypingsRemoveVariable(
                        &dfa->fs->typings, definitions->variable);
                if( expression && expression->kind == RSCACHE_CS2_EXPR_CONSTANT )
                    RSCache_CS2_TypingsRemoveConstant(&dfa->fs->typings, expression);
            }
            struct RSCache_CS2_Insn* next = insn->next;
            RSCache_CS2_ChainRemove(&function->instructions, insn);
            insn = next;
        }
    }
}

/* -------------------------------------------------------------------------
 * DeleteNops
 *
 * An assignment that defines nothing and whose value is only elements has no
 * effect — a discarded local read, typically.
 * ---------------------------------------------------------------------- */

static void
cs2_delete_nops(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    (void)dfa;
    struct RSCache_CS2_Insn* insn = function->instructions.first;
    while( insn )
    {
        struct RSCache_CS2_Insn* next = insn->next;
        if( insn->kind == RSCACHE_CS2_INSN_ASSIGNMENT )
        {
            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** items = NULL;
            int count = 0;
            RSCache_CS2_ExprAsList(insn->definitions, &items, &count, &single);
            if( count == 0 )
            {
                struct RSCache_CS2_Expr* value_single = NULL;
                struct RSCache_CS2_Expr** value_items = NULL;
                int value_count = 0;
                RSCache_CS2_ExprAsList(
                    insn->expression, &value_items, &value_count, &value_single);
                bool all_elements = true;
                for( int i = 0; i < value_count; i++ )
                {
                    if( !RSCache_CS2_ExprIsElement(value_items[i]) )
                    {
                        all_elements = false;
                        break;
                    }
                }
                if( all_elements )
                    RSCache_CS2_ChainRemove(&function->instructions, insn);
            }
        }
        insn = next;
    }
}

/* -------------------------------------------------------------------------
 * ReorderArgs
 *
 * A script's arguments are declared ints-then-strings, but callers pass them
 * interleaved. The call site's order is the one the source should show, so the
 * declaration is permuted to match the typings a caller already established.
 * ---------------------------------------------------------------------- */

static void
cs2_reorder_args(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    if( function->arguments.count <= 1 )
        return;

    enum RSCache_CS2_StackType old_types[256];
    int count = function->arguments.count;
    if( count > (int)(sizeof(old_types) / sizeof(old_types[0])) )
        return;
    for( int i = 0; i < count; i++ )
        old_types[i] = RSCache_CS2_VarStackType(
            ((struct RSCache_CS2_Variable*)function->arguments.items[i])->kind);

    struct RSCache_CS2_TypingList* typing = RSCache_CS2_TypingsArgs(
        &dfa->fs->typings, dfa->fs, function->id, old_types, count);
    if( typing->count != count )
        return;

    bool same = true;
    for( int i = 0; i < count; i++ )
    {
        if( typing->items[i]->stack_type != old_types[i] )
        {
            same = false;
            break;
        }
    }
    if( same )
        return;

    struct RSCache_CS2_Vec remaining;
    RSCache_CS2_VecInit(&remaining);
    for( int i = 0; i < count; i++ )
        RSCache_CS2_VecPush(&remaining, function->arguments.items[i]);

    RSCache_CS2_VecClear(&function->arguments);
    for( int i = 0; i < count; i++ )
    {
        int found = -1;
        for( int j = 0; j < remaining.count; j++ )
        {
            struct RSCache_CS2_Variable* variable =
                (struct RSCache_CS2_Variable*)remaining.items[j];
            if( RSCache_CS2_VarStackType(variable->kind) == typing->items[i]->stack_type )
            {
                found = j;
                break;
            }
        }
        if( found < 0 )
        {
            cs2_dfa_fail(
                dfa,
                "script %d: argument order does not match the call sites",
                function->id);
            RSCache_CS2_VecFree(&remaining);
            return;
        }
        RSCache_CS2_VecPush(&function->arguments, remaining.items[found]);
        RSCache_CS2_VecRemoveAt(&remaining, found);
    }
    RSCache_CS2_VecFree(&remaining);
}

/* -------------------------------------------------------------------------
 * FindArrayArgs
 *
 * A script may take one array argument, and the bytecode does not say so: the
 * array simply occupies local int slot 0 and is indexed without ever being
 * defined. Seeing a read of array 0 with no preceding `def_` is the only
 * evidence, so that is what this looks for.
 * ---------------------------------------------------------------------- */

static void
cs2_find_array_args_calls(struct cs2_dfa* dfa, int script_id)
{
    for( int fi = 0; fi < dfa->fs->function_ids.count; fi++ )
    {
        struct RSCache_CS2_Function* function =
            RSCache_CS2_FunctionSetGet(dfa->fs, dfa->fs->function_ids.items[fi]);
        if( !function )
            continue;
        for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn;
             insn = insn->next )
        {
            if( insn->kind != RSCACHE_CS2_INSN_ASSIGNMENT )
                continue;
            struct RSCache_CS2_Expr* expr = insn->expression;
            if( !expr || expr->kind != RSCACHE_CS2_EXPR_PROC || expr->script_id != script_id )
                continue;

            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** items = NULL;
            int count = 0;
            RSCache_CS2_ExprAsList(expr->arguments, &items, &count, &single);
            if( count == 0 )
                continue;

            /* The reference builds this pointer with the *caller's* id rather
             * than the callee's (its own `// todo`). Reproduced deliberately:
             * the identifier it prints is observable output, so "fixing" it
             * here would diverge from every existing decompilation. */
            struct RSCache_CS2_Expr* new_arg = RSCache_CS2_ExprPointer(
                &dfa->fs->arena,
                RSCache_CS2_VarIntern(dfa->fs, RSCACHE_CS2_VAR_ARRAY, function->id, 0));

            struct RSCache_CS2_Vec new_args;
            RSCache_CS2_VecInit(&new_args);
            RSCache_CS2_VecPush(&new_args, new_arg);
            for( int i = 1; i < count; i++ )
                RSCache_CS2_VecPush(&new_args, items[i]);
            expr->arguments = RSCache_CS2_ExprFromList(
                &dfa->fs->arena,
                (struct RSCache_CS2_Expr* const*)new_args.items,
                new_args.count);
            RSCache_CS2_VecFree(&new_args);

            if( function->id != script_id )
            {
                struct RSCache_CS2_TypingList* args =
                    (struct RSCache_CS2_TypingList*)RSCache_CS2_IntMapGet(
                        &dfa->fs->typings.args, script_id);
                if( args && args->count > 0 )
                    RSCache_CS2_TypingAssign(
                        RSCache_CS2_TypingsOfElement(&dfa->fs->typings, new_arg),
                        args->items[0]);
            }
        }
    }
}

static void
cs2_find_array_args(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    bool has_int_arg = false;
    for( int i = 0; i < function->arguments.count; i++ )
    {
        struct RSCache_CS2_Variable* variable =
            (struct RSCache_CS2_Variable*)function->arguments.items[i];
        if( RSCache_CS2_VarStackType(variable->kind) == RSCACHE_CS2_STACK_INT )
        {
            has_int_arg = true;
            break;
        }
    }
    if( !has_int_arg )
        return;

    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn; insn = insn->next )
    {
        if( insn->kind != RSCACHE_CS2_INSN_ASSIGNMENT )
            continue;
        struct RSCache_CS2_Expr* expr = insn->expression;
        if( !expr || expr->kind != RSCACHE_CS2_EXPR_OPERATION )
            continue;

        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** items = NULL;
        int count = 0;
        RSCache_CS2_ExprAsList(expr->arguments, &items, &count, &single);
        if( count == 0 || items[0]->kind != RSCACHE_CS2_EXPR_ACCESS )
            continue;

        /* A definition of array 0 settles it the other way: the array is local,
         * so argument 0 is an ordinary int. */
        if( expr->opcode == RSCACHE_CS2_OP_DEFINE_ARRAY && items[0]->variable->id == 0 )
            return;

        if( expr->opcode != RSCACHE_CS2_OP_PUSH_ARRAY_INT || items[0]->variable->id != 0 )
            continue;

        struct RSCache_CS2_Variable* old_var =
            (struct RSCache_CS2_Variable*)function->arguments.items[0];
        struct RSCache_CS2_Variable* new_var =
            RSCache_CS2_VarIntern(dfa->fs, RSCACHE_CS2_VAR_ARRAY, function->id, 0);
        function->arguments.items[0] = new_var;

        struct RSCache_CS2_Typing* new_typing =
            RSCache_CS2_TypingsOfVariable(&dfa->fs->typings, new_var);
        enum RSCache_CS2_StackType stack_types[256];
        int arg_count = function->arguments.count;
        if( arg_count > (int)(sizeof(stack_types) / sizeof(stack_types[0])) )
            return;
        for( int i = 0; i < arg_count; i++ )
            stack_types[i] = RSCache_CS2_VarStackType(
                ((struct RSCache_CS2_Variable*)function->arguments.items[i])->kind);
        struct RSCache_CS2_TypingList* args = RSCache_CS2_TypingsArgs(
            &dfa->fs->typings, dfa->fs, function->id, stack_types, arg_count);
        if( args->count > 0 )
            args->items[0] = new_typing;
        RSCache_CS2_TypingsRemoveVariable(&dfa->fs->typings, old_var);
        cs2_find_array_args_calls(dfa, function->id);
        return;
    }
}

/* -------------------------------------------------------------------------
 * CombineSameLineOperations
 *
 * A command returning several values pushes them one at a time, so the IR
 * carries a run of single assignments that the source writes as one
 * multiple-assignment. Merged back-to-front, matching stack order.
 * ---------------------------------------------------------------------- */

static bool
cs2_is_stack_assign(const struct RSCache_CS2_Insn* insn)
{
    return insn && insn->kind == RSCACHE_CS2_INSN_ASSIGNMENT &&
           cs2_is_stack_access(insn->expression);
}

static void
cs2_combine_same_line(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    struct RSCache_CS2_Insn* start = function->instructions.first;
    while( start )
    {
        if( !cs2_is_stack_assign(start) )
        {
            start = start->next;
            continue;
        }

        struct RSCache_CS2_Insn* end = start;
        while( cs2_is_stack_assign(end) )
        {
            if( !end->next )
                break;
            end = end->next;
        }
        if( cs2_is_stack_assign(end) )
            break;

        if( start != end && start->next != end )
        {
            struct RSCache_CS2_Insn* merged = RSCache_CS2_InsnAssignment(
                &dfa->fs->arena,
                RSCache_CS2_ExprEmpty(&dfa->fs->arena),
                RSCache_CS2_ExprEmpty(&dfa->fs->arena));
            RSCache_CS2_ChainInsertBefore(&function->instructions, merged, start);

            struct RSCache_CS2_Insn* node = start;
            while( node != end )
            {
                RSCache_CS2_ChainRemove(&function->instructions, node);
                merged->expression =
                    cs2_expr_concat(&dfa->fs->arena, node->expression, merged->expression);
                merged->definitions =
                    cs2_expr_concat(&dfa->fs->arena, node->definitions, merged->definitions);
                node = node->next;
            }
        }
        start = end->next;
    }
}

/* -------------------------------------------------------------------------
 * InlineStackDefinitions
 *
 * The pass that actually rebuilds expressions: a stack slot defined once and
 * consumed once is substituted into its consumer, collapsing the interpreter's
 * three-address form back to nested calls.
 * ---------------------------------------------------------------------- */

/**
 * Find `defs` as a contiguous run of accesses inside `items`, and splice `by`
 * in its place. Returns NULL when the run is not present.
 */
static struct RSCache_CS2_Expr*
cs2_replace_sub_expr(
    struct cs2_dfa* dfa,
    struct RSCache_CS2_Expr** items,
    int count,
    struct RSCache_CS2_Expr** defs,
    int def_count,
    struct RSCache_CS2_Expr* by)
{
    if( def_count == 0 || def_count > count )
        return NULL;

    int found = -1;
    for( int start = 0; start + def_count <= count; start++ )
    {
        bool match = true;
        for( int i = 0; i < def_count; i++ )
        {
            struct RSCache_CS2_Expr* item = items[start + i];
            if( item->kind != RSCACHE_CS2_EXPR_ACCESS ||
                item->variable != defs[i]->variable )
            {
                match = false;
                break;
            }
        }
        if( match )
        {
            found = start;
            break;
        }
    }
    if( found < 0 )
        return NULL;

    struct RSCache_CS2_Vec spliced;
    RSCache_CS2_VecInit(&spliced);
    for( int i = 0; i < found; i++ )
        RSCache_CS2_VecPush(&spliced, items[i]);
    RSCache_CS2_VecPush(&spliced, by);
    for( int i = found + def_count; i < count; i++ )
        RSCache_CS2_VecPush(&spliced, items[i]);

    struct RSCache_CS2_Expr* defs_expr = RSCache_CS2_ExprFromList(
        &dfa->fs->arena, (struct RSCache_CS2_Expr* const*)defs, def_count);
    RSCache_CS2_TypingReplaceLists(
        RSCache_CS2_TypingsOfExpr(&dfa->fs->typings, defs_expr),
        RSCache_CS2_TypingsOfExpr(&dfa->fs->typings, by));
    for( int i = 0; i < def_count; i++ )
        RSCache_CS2_TypingsForgetVariable(&dfa->fs->typings, defs[i]->variable);

    struct RSCache_CS2_Expr* out = RSCache_CS2_ExprFromList(
        &dfa->fs->arena,
        (struct RSCache_CS2_Expr* const*)spliced.items,
        spliced.count);
    RSCache_CS2_VecFree(&spliced);
    return out;
}

static bool
cs2_try_replace_in(
    struct cs2_dfa* dfa,
    struct RSCache_CS2_Expr** target,
    struct RSCache_CS2_Expr** defs,
    int def_count,
    struct RSCache_CS2_Expr* by)
{
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(*target, &items, &count, &single);

    /* Snapshot: the splice reads `items` while building its replacement, and
     * `items` may point into the compound being rebuilt. */
    struct RSCache_CS2_Expr* stack_copy[64];
    struct RSCache_CS2_Expr** copy = stack_copy;
    if( count > (int)(sizeof(stack_copy) / sizeof(stack_copy[0])) )
    {
        copy = (struct RSCache_CS2_Expr**)malloc((size_t)count * sizeof(*copy));
        if( !copy )
            return false;
    }
    for( int i = 0; i < count; i++ )
        copy[i] = items[i];

    struct RSCache_CS2_Expr* replaced =
        cs2_replace_sub_expr(dfa, copy, count, defs, def_count, by);
    if( copy != stack_copy )
        free(copy);
    if( !replaced )
        return false;
    *target = replaced;
    return true;
}

static bool
cs2_replace_exprs(
    struct cs2_dfa* dfa,
    struct RSCache_CS2_Insn* dst,
    struct RSCache_CS2_Expr** defs,
    int def_count,
    struct RSCache_CS2_Expr* by)
{
    if( cs2_try_replace_in(dfa, &dst->expression, defs, def_count, by) )
        return true;

    struct RSCache_CS2_Expr* expr = dst->expression;
    if( !expr )
        return false;

    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_OPERATION:
    case RSCACHE_CS2_EXPR_PROC:
        return cs2_try_replace_in(dfa, &expr->arguments, defs, def_count, by);

    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
    {
        struct RSCache_CS2_Expr* component = expr->component;
        if( def_count == 1 && component && component->kind == RSCACHE_CS2_EXPR_ACCESS &&
            component->variable == defs[0]->variable )
        {
            struct RSCache_CS2_TypingList* by_typings =
                RSCache_CS2_TypingsOfExpr(&dfa->fs->typings, by);
            if( by_typings->count != 1 )
            {
                cs2_dfa_fail(dfa, "hook component was defined by a multi-value expression");
                return false;
            }
            RSCache_CS2_TypingReplace(
                RSCache_CS2_TypingsOfElement(&dfa->fs->typings, component),
                by_typings->items[0]);
            RSCache_CS2_TypingsForgetVariable(&dfa->fs->typings, component->variable);
            expr->component = by;
            return true;
        }
        if( cs2_try_replace_in(dfa, &expr->arguments, defs, def_count, by) )
            return true;
        return cs2_try_replace_in(dfa, &expr->triggers, defs, def_count, by);
    }

    default:
        return false;
    }
}

static void
cs2_inline_stack_definitions(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    struct RSCache_CS2_Insn* insn = function->instructions.first;
    while( insn )
    {
        struct RSCache_CS2_Insn* next_insn = insn->next;
        if( insn->kind != RSCACHE_CS2_INSN_ASSIGNMENT )
        {
            insn = next_insn;
            continue;
        }

        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** defs = NULL;
        int def_count = 0;
        RSCache_CS2_ExprAsList(insn->definitions, &defs, &def_count, &single);
        if( def_count == 0 )
        {
            insn = next_insn;
            continue;
        }
        bool all_stack = true;
        for( int i = 0; i < def_count; i++ )
        {
            if( !cs2_is_stack_access(defs[i]) )
            {
                all_stack = false;
                break;
            }
        }
        if( !all_stack )
        {
            insn = next_insn;
            continue;
        }

        struct RSCache_CS2_Expr* defs_copy[64];
        if( def_count > (int)(sizeof(defs_copy) / sizeof(defs_copy[0])) )
        {
            insn = next_insn;
            continue;
        }
        for( int i = 0; i < def_count; i++ )
            defs_copy[i] = defs[i];

        struct RSCache_CS2_Expr* value = insn->expression;
        bool inlined = false;
        for( struct RSCache_CS2_Insn* consumer = insn->next;
             consumer && RSCache_CS2_InsnIsEvaluation(consumer);
             consumer = consumer->next )
        {
            if( cs2_replace_exprs(dfa, consumer, defs_copy, def_count, value) )
            {
                RSCache_CS2_ChainRemove(&function->instructions, insn);
                inlined = true;
                break;
            }
            if( dfa->failed )
                return;
        }

        if( !inlined )
        {
            if( value && value->kind == RSCACHE_CS2_EXPR_CONSTANT && def_count == 1 )
            {
                /* A constant nobody consumes leaves nothing behind. */
                RSCache_CS2_TypingsRemoveVariable(&dfa->fs->typings, defs_copy[0]->variable);
                RSCache_CS2_TypingsRemoveConstant(&dfa->fs->typings, value);
                RSCache_CS2_ChainRemove(&function->instructions, insn);
            }
            else
            {
                /* Keep the call for its effect, drop the unread results. */
                for( int i = 0; i < def_count; i++ )
                    RSCache_CS2_TypingsRemoveVariable(
                        &dfa->fs->typings, defs_copy[i]->variable);
                insn->definitions = RSCache_CS2_ExprEmpty(&dfa->fs->arena);
            }
        }

        insn = next_insn;
    }
}

/* -------------------------------------------------------------------------
 * CalcTypes
 * ---------------------------------------------------------------------- */

struct cs2_worklist
{
    struct RSCache_CS2_Vec items;
};

static void
cs2_worklist_compact(struct cs2_worklist* list)
{
    int write = 0;
    for( int i = 0; i < list->items.count; i++ )
    {
        if( list->items.items[i] )
            list->items.items[write++] = list->items.items[i];
    }
    list->items.count = write;
}

static bool
cs2_calc_types(struct cs2_dfa* dfa)
{
    struct RSCache_CS2_Typings* typings = &dfa->fs->typings;

    struct cs2_worklist unresolved;
    RSCache_CS2_VecInit(&unresolved.items);
    for( int i = 0; i < typings->all.count; i++ )
    {
        struct RSCache_CS2_Typing* typing = (struct RSCache_CS2_Typing*)typings->all.items[i];
        if( typing->type == RSCACHE_CS2_TYPE_NONE )
            RSCache_CS2_VecPush(&unresolved.items, typing);
    }

    /* Strings first: the stack has exactly one string type, so a string slot's
     * type is decided before any inference runs. */
    for( int i = 0; i < unresolved.items.count; i++ )
    {
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)unresolved.items.items[i];
        if( typing->stack_type == RSCACHE_CS2_STACK_STRING )
        {
            RSCache_CS2_TypingFreezeType(typing, RSCACHE_CS2_TYPE_STRING);
            unresolved.items.items[i] = NULL;
        }
    }
    cs2_worklist_compact(&unresolved);

    enum RSCache_CS2_Type scratch[64];

    /* Forward flow: a slot fed only by known types takes their union. */
    bool changed = true;
    while( changed )
    {
        changed = false;
        for( int i = 0; i < unresolved.items.count; i++ )
        {
            struct RSCache_CS2_Typing* typing =
                (struct RSCache_CS2_Typing*)unresolved.items.items[i];
            if( !typing || typing->from.count == 0 )
                continue;
            int count = 0;
            bool complete = true;
            for( int j = 0; j < typing->from.count; j++ )
            {
                struct RSCache_CS2_Typing* source =
                    (struct RSCache_CS2_Typing*)typing->from.items[j];
                if( source->type == RSCACHE_CS2_TYPE_NONE )
                {
                    complete = false;
                    break;
                }
                bool seen = false;
                for( int k = 0; k < count; k++ )
                    seen = seen || scratch[k] == source->type;
                if( !seen && count < (int)(sizeof(scratch) / sizeof(scratch[0])) )
                    scratch[count++] = source->type;
            }
            if( !complete || count == 0 )
                continue;
            enum RSCache_CS2_Type merged = RSCache_CS2_TypeUnion(scratch, count);
            if( merged == RSCACHE_CS2_TYPE_NONE )
            {
                cs2_dfa_fail(dfa, "irreconcilable types flowing into one value");
                RSCache_CS2_VecFree(&unresolved.items);
                return false;
            }
            RSCache_CS2_TypingFreezeType(typing, merged);
            unresolved.items.items[i] = NULL;
            changed = true;
        }
        cs2_worklist_compact(&unresolved);
    }

    /* Then the looser rules, applied in batches so a pass's own results cannot
     * feed back into it — the reference collects into a map and writes after,
     * deliberately, and the order dependence that would otherwise appear is
     * exactly what makes a decompile non-reproducible. */
    changed = true;
    while( changed )
    {
        changed = false;

        /* An int constant that reached `obj` is written literally in source, so
         * it narrows to the "named in the code" form. */
        for( int i = 0; i < typings->constants.capacity; i++ )
        {
            if( !typings->constants.entries[i].occupied )
                continue;
            struct RSCache_CS2_Typing* typing =
                (struct RSCache_CS2_Typing*)typings->constants.entries[i].value;
            if( typing->type == RSCACHE_CS2_TYPE_OBJ )
            {
                RSCache_CS2_TypingFreezeType(typing, RSCACHE_CS2_TYPE_NAMEDOBJ);
                changed = true;
            }
        }
        if( changed )
            continue;

        /* Combine what flows in with what flows out. */
        int resolved = 0;
        for( int i = 0; i < unresolved.items.count; i++ )
        {
            struct RSCache_CS2_Typing* typing =
                (struct RSCache_CS2_Typing*)unresolved.items.items[i];
            if( !typing )
                continue;

            enum RSCache_CS2_Type in_types[64];
            enum RSCache_CS2_Type out_types[64];
            int in_count = 0;
            int out_count = 0;
            for( int j = 0; j < typing->from.count; j++ )
            {
                enum RSCache_CS2_Type type =
                    ((struct RSCache_CS2_Typing*)typing->from.items[j])->type;
                if( type == RSCACHE_CS2_TYPE_NONE )
                    continue;
                bool seen = false;
                for( int k = 0; k < in_count; k++ )
                    seen = seen || in_types[k] == type;
                if( !seen && in_count < 64 )
                    in_types[in_count++] = type;
            }
            for( int j = 0; j < typing->to.count; j++ )
            {
                enum RSCache_CS2_Type type =
                    ((struct RSCache_CS2_Typing*)typing->to.items[j])->type;
                if( type == RSCACHE_CS2_TYPE_NONE )
                    continue;
                bool seen = false;
                for( int k = 0; k < out_count; k++ )
                    seen = seen || out_types[k] == type;
                if( !seen && out_count < 64 )
                    out_types[out_count++] = type;
            }
            if( in_count == 0 && out_count == 0 )
                continue;

            enum RSCache_CS2_Type candidates[2];
            int candidate_count = 0;
            enum RSCache_CS2_Type merged = RSCache_CS2_TypeUnion(in_types, in_count);
            if( merged != RSCACHE_CS2_TYPE_NONE )
                candidates[candidate_count++] = merged;
            merged = RSCache_CS2_TypeIntersection(out_types, out_count);
            if( merged != RSCACHE_CS2_TYPE_NONE )
            {
                bool seen = candidate_count == 1 && candidates[0] == merged;
                if( !seen )
                    candidates[candidate_count++] = merged;
            }
            enum RSCache_CS2_Type result =
                RSCache_CS2_TypeIntersection(candidates, candidate_count);
            if( result == RSCACHE_CS2_TYPE_NONE )
            {
                char in_text[128];
                char out_text[128];
                cs2_dfa_fail(dfa,
                             "a value is used at two irreconcilable types: "
                             "defined as %s, used as %s",
                             cs2_type_set_text(in_types, in_count, in_text, sizeof(in_text)),
                             cs2_type_set_text(out_types, out_count, out_text,
                                               sizeof(out_text)));
                RSCache_CS2_VecFree(&unresolved.items);
                return false;
            }
            /* Not frozen: `check` below may still widen it. */
            typing->type = result;
            unresolved.items.items[i] = NULL;
            resolved++;
        }
        if( resolved )
        {
            cs2_worklist_compact(&unresolved);
            changed = true;
            continue;
        }

        /* Last resort: agree with whatever this value is compared against. */
        for( int i = 0; i < unresolved.items.count; i++ )
        {
            struct RSCache_CS2_Typing* typing =
                (struct RSCache_CS2_Typing*)unresolved.items.items[i];
            if( !typing || typing->compare.count == 0 )
                continue;
            int count = 0;
            for( int j = 0; j < typing->compare.count; j++ )
            {
                enum RSCache_CS2_Type type =
                    ((struct RSCache_CS2_Typing*)typing->compare.items[j])->type;
                if( type == RSCACHE_CS2_TYPE_NONE )
                    continue;
                bool seen = false;
                for( int k = 0; k < count; k++ )
                    seen = seen || scratch[k] == type;
                if( !seen && count < (int)(sizeof(scratch) / sizeof(scratch[0])) )
                    scratch[count++] = type;
            }
            if( count == 0 )
                continue;
            enum RSCache_CS2_Type merged = RSCache_CS2_TypeUnion(scratch, count);
            if( merged == RSCACHE_CS2_TYPE_NONE )
            {
                cs2_dfa_fail(dfa, "a value is compared against two irreconcilable types");
                RSCache_CS2_VecFree(&unresolved.items);
                return false;
            }
            typing->type = merged;
            unresolved.items.items[i] = NULL;
            changed = true;
        }
        cs2_worklist_compact(&unresolved);
    }

    /* Re-widen anything an early guess pinned too narrowly. */
    changed = true;
    while( changed )
    {
        changed = false;
        for( int i = 0; i < typings->all.count; i++ )
        {
            struct RSCache_CS2_Typing* typing =
                (struct RSCache_CS2_Typing*)typings->all.items[i];
            if( typing->type == RSCACHE_CS2_TYPE_NONE || typing->freeze_type )
                continue;
            int count = 0;
            for( int j = 0; j < typing->from.count; j++ )
            {
                enum RSCache_CS2_Type type =
                    ((struct RSCache_CS2_Typing*)typing->from.items[j])->type;
                if( type == RSCACHE_CS2_TYPE_NONE )
                    continue;
                bool seen = false;
                for( int k = 0; k < count; k++ )
                    seen = seen || scratch[k] == type;
                if( !seen && count < (int)(sizeof(scratch) / sizeof(scratch[0])) )
                    scratch[count++] = type;
            }
            if( count == 0 )
                continue;
            enum RSCache_CS2_Type merged = RSCache_CS2_TypeUnion(scratch, count);
            if( merged == RSCACHE_CS2_TYPE_NONE )
            {
                cs2_dfa_fail(dfa, "irreconcilable types flowing into one value");
                RSCache_CS2_VecFree(&unresolved.items);
                return false;
            }
            if( typing->type != merged )
            {
                typing->type = merged;
                changed = true;
            }
        }
    }

    /* Whatever is still open is simply an int or a string. */
    for( int i = 0; i < unresolved.items.count; i++ )
    {
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)unresolved.items.items[i];
        if( !typing )
            continue;
        typing->type = typing->stack_type == RSCACHE_CS2_STACK_STRING ? RSCACHE_CS2_TYPE_STRING
                                                                      : RSCACHE_CS2_TYPE_INT;
    }
    RSCache_CS2_VecFree(&unresolved.items);
    return true;
}

/* -------------------------------------------------------------------------
 * CalcIdentifiers
 *
 * Types decide how a value prints; identifiers decide what a local is *called*.
 * `int $width0` and `int $x0` are the same type — the difference comes from
 * which command's argument the value reached.
 * ---------------------------------------------------------------------- */

static const enum RSCache_CS2_ProtoId cs2_strong_protos[] = {
    RSCACHE_CS2_PROTO_MESUID,   RSCACHE_CS2_PROTO_CHATFILTER,
    RSCACHE_CS2_PROTO_RANK,     RSCACHE_CS2_PROTO_WORLD,
    RSCACHE_CS2_PROTO_KEY,      RSCACHE_CS2_PROTO_IFTYPE,
    RSCACHE_CS2_PROTO_SETSIZE,  RSCACHE_CS2_PROTO_SETPOSH,
    RSCACHE_CS2_PROTO_SETPOSV,  RSCACHE_CS2_PROTO_SETTEXTALIGNH,
    RSCACHE_CS2_PROTO_SETTEXTALIGNV, RSCACHE_CS2_PROTO_CHATTYPE,
    RSCACHE_CS2_PROTO_WINDOWMODE, RSCACHE_CS2_PROTO_CLIENTTYPE,
};

/**
 * A typing's prototype, as (type, identifier). Both must be settled — an
 * unsettled neighbour is why several of the merge rules below decline.
 */
struct cs2_proto_ref
{
    enum RSCache_CS2_Type type;
    const char* identifier;
    bool known;
};

static bool
cs2_proto_ref_equal(const struct cs2_proto_ref* a, const struct cs2_proto_ref* b)
{
    if( !a->known || !b->known )
        return a->known == b->known;
    return a->type == b->type && strcmp(a->identifier, b->identifier) == 0;
}

static struct cs2_proto_ref
cs2_proto_ref_of(const struct RSCache_CS2_Typing* typing)
{
    struct cs2_proto_ref ref;
    ref.type = typing->type;
    ref.identifier = typing->identifier;
    ref.known = typing->type != RSCACHE_CS2_TYPE_NONE && typing->identifier != NULL;
    return ref;
}

static bool
cs2_proto_ref_is_default(const struct cs2_proto_ref* ref)
{
    return strcmp(ref->identifier, RSCache_CS2_TypeLiteral(ref->type)) == 0;
}

static bool
cs2_proto_ref_is(const struct cs2_proto_ref* ref, enum RSCache_CS2_ProtoId proto)
{
    if( !ref->known )
        return false;
    const struct RSCache_CS2_Prototype* prototype = RSCache_CS2_ProtoGet(proto);
    return ref->type == prototype->type && strcmp(ref->identifier, prototype->identifier) == 0;
}

/** Set semantics over a small array of prototype references. */
static void
cs2_proto_set_add(struct cs2_proto_ref* set, int* count, const struct cs2_proto_ref* ref)
{
    for( int i = 0; i < *count; i++ )
    {
        if( cs2_proto_ref_equal(&set[i], ref) )
            return;
    }
    if( *count < 64 )
        set[(*count)++] = *ref;
}

static void
cs2_proto_set_remove(struct cs2_proto_ref* set, int* count, enum RSCache_CS2_ProtoId proto)
{
    for( int i = 0; i < *count; i++ )
    {
        if( cs2_proto_ref_is(&set[i], proto) )
        {
            for( int j = i; j + 1 < *count; j++ )
                set[j] = set[j + 1];
            (*count)--;
            return;
        }
    }
}

static bool
cs2_proto_set_has(const struct cs2_proto_ref* set, int count, enum RSCache_CS2_ProtoId proto)
{
    for( int i = 0; i < count; i++ )
    {
        if( cs2_proto_ref_is(&set[i], proto) )
            return true;
    }
    return false;
}

/** All neighbours agree on one non-default identifier. */
static bool
cs2_merge_safe_single(const struct cs2_proto_ref* set, int count, struct cs2_proto_ref* out)
{
    if( count != 1 || !set[0].known )
        return false;
    if( cs2_proto_ref_is_default(&set[0]) )
        return false;
    *out = set[0];
    return true;
}

/**
 * Reconcile competing identifiers.
 *
 * The pairwise rules are the reference's, and they encode which of two names is
 * the more specific description of the same value: a width is an x, a
 * component sub-id is a slot, and so on.
 */
static bool
cs2_merge_union(struct cs2_proto_ref* set, int count, struct cs2_proto_ref* out)
{
    for( int i = 0; i < count; i++ )
    {
        for( size_t j = 0; j < sizeof(cs2_strong_protos) / sizeof(cs2_strong_protos[0]); j++ )
        {
            if( cs2_proto_ref_is(&set[i], cs2_strong_protos[j]) )
            {
                *out = set[i];
                return true;
            }
        }
    }
    for( int i = 0; i < count; i++ )
    {
        if( !set[i].known )
            return false;
    }
    for( int i = 0; i < count; i++ )
    {
        if( cs2_proto_ref_is_default(&set[i]) )
            return false;
    }
    if( count == 1 )
    {
        *out = set[0];
        return true;
    }

#define CS2_PREFER(keep, drop)                                                              \
    if( cs2_proto_set_has(set, count, keep) && cs2_proto_set_has(set, count, drop) )         \
    cs2_proto_set_remove(set, &count, drop)

    CS2_PREFER(RSCACHE_CS2_PROTO_X, RSCACHE_CS2_PROTO_WIDTH);
    CS2_PREFER(RSCACHE_CS2_PROTO_Y, RSCACHE_CS2_PROTO_HEIGHT);
    CS2_PREFER(RSCACHE_CS2_PROTO_X, RSCACHE_CS2_PROTO_MOUSEX);
    CS2_PREFER(RSCACHE_CS2_PROTO_Y, RSCACHE_CS2_PROTO_MOUSEY);
    CS2_PREFER(RSCACHE_CS2_PROTO_SLOT, RSCACHE_CS2_PROTO_COMSUBID);
    CS2_PREFER(RSCACHE_CS2_PROTO_SLOT, RSCACHE_CS2_PROTO_DROPSUBID);
    CS2_PREFER(RSCACHE_CS2_PROTO_COMSUBID, RSCACHE_CS2_PROTO_DROPSUBID);
    CS2_PREFER(RSCACHE_CS2_PROTO_INDEX, RSCACHE_CS2_PROTO_COMSUBID);
    CS2_PREFER(RSCACHE_CS2_PROTO_INDEX, RSCACHE_CS2_PROTO_COUNT);
    CS2_PREFER(RSCACHE_CS2_PROTO_COUNT, RSCACHE_CS2_PROTO_LENGTH);
    CS2_PREFER(RSCACHE_CS2_PROTO_COMSUBID, RSCACHE_CS2_PROTO_COUNT);
    CS2_PREFER(RSCACHE_CS2_PROTO_OP, RSCACHE_CS2_PROTO_TEXT);

#undef CS2_PREFER

    if( count != 1 )
        return false;
    *out = set[0];
    return true;
}

static bool
cs2_identifiers_flow(
    struct cs2_dfa* dfa,
    struct cs2_worklist* unresolved,
    bool use_from,
    bool use_compare,
    bool merge_union)
{
    bool changed = false;
    for( int i = 0; i < unresolved->items.count; i++ )
    {
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)unresolved->items.items[i];
        if( !typing )
            continue;

        const struct RSCache_CS2_Vec* edges =
            use_compare ? &typing->compare : (use_from ? &typing->from : &typing->to);
        if( edges->count == 0 )
            continue;

        struct cs2_proto_ref set[64];
        int count = 0;
        for( int j = 0; j < edges->count; j++ )
        {
            struct cs2_proto_ref ref =
                cs2_proto_ref_of((const struct RSCache_CS2_Typing*)edges->items[j]);
            cs2_proto_set_add(set, &count, &ref);
        }

        struct cs2_proto_ref merged;
        bool ok = merge_union ? cs2_merge_union(set, count, &merged)
                              : cs2_merge_safe_single(set, count, &merged);
        if( !ok )
            continue;
        if( typing->type != merged.type )
        {
            cs2_dfa_fail(
                dfa,
                "identifier '%s' does not belong to type '%s'",
                merged.identifier,
                RSCache_CS2_TypeLiteral(typing->type));
            return false;
        }
        RSCache_CS2_TypingFreezeIdentifier(typing, merged.identifier);
        unresolved->items.items[i] = NULL;
        changed = true;
    }
    cs2_worklist_compact(unresolved);
    return changed;
}

static bool
cs2_calc_identifiers(struct cs2_dfa* dfa)
{
    struct RSCache_CS2_Typings* typings = &dfa->fs->typings;

    struct cs2_worklist unresolved;
    RSCache_CS2_VecInit(&unresolved.items);
    for( int i = 0; i < typings->all.count; i++ )
    {
        struct RSCache_CS2_Typing* typing = (struct RSCache_CS2_Typing*)typings->all.items[i];
        if( !typing->identifier )
            RSCache_CS2_VecPush(&unresolved.items, typing);
    }

    while( cs2_identifiers_flow(dfa, &unresolved, true, false, false) ||
           cs2_identifiers_flow(dfa, &unresolved, false, false, false) )
    {
        if( dfa->failed )
        {
            RSCache_CS2_VecFree(&unresolved.items);
            return false;
        }
    }
    if( dfa->failed )
    {
        RSCache_CS2_VecFree(&unresolved.items);
        return false;
    }

    /* A literal compared against a named value takes that value's name, which
     * is how `if ($lvl = 99)` prints 99 as a level rather than a bare int. */
    for( int i = 0; i < typings->constants.capacity; i++ )
    {
        if( !typings->constants.entries[i].occupied )
            continue;
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)typings->constants.entries[i].value;
        if( typing->identifier || typing->compare.count == 0 )
            continue;
        if( typing->compare.count != 1 )
            continue;
        struct RSCache_CS2_Typing* other =
            (struct RSCache_CS2_Typing*)typing->compare.items[0];
        if( !other->identifier )
            continue;
        RSCache_CS2_TypingFreezeIdentifier(typing, other->identifier);
        for( int j = 0; j < unresolved.items.count; j++ )
        {
            if( unresolved.items.items[j] == typing )
                unresolved.items.items[j] = NULL;
        }
    }
    cs2_worklist_compact(&unresolved);

    while( cs2_identifiers_flow(dfa, &unresolved, true, false, true) ||
           cs2_identifiers_flow(dfa, &unresolved, false, false, true) ||
           cs2_identifiers_flow(dfa, &unresolved, false, true, true) )
    {
        if( dfa->failed )
        {
            RSCache_CS2_VecFree(&unresolved.items);
            return false;
        }
    }
    if( dfa->failed )
    {
        RSCache_CS2_VecFree(&unresolved.items);
        return false;
    }

    for( int i = 0; i < unresolved.items.count; i++ )
    {
        struct RSCache_CS2_Typing* typing =
            (struct RSCache_CS2_Typing*)unresolved.items.items[i];
        if( typing )
            typing->identifier = RSCache_CS2_TypeLiteral(typing->type);
    }
    RSCache_CS2_VecFree(&unresolved.items);
    return true;
}

/* -------------------------------------------------------------------------
 * AddShortCircuitOperators
 *
 * `a | b` compiles to two branches to the same label, and `a & b` to a branch
 * over a branch. Recognising those two shapes is what turns a nest of gotos
 * back into one condition.
 * ---------------------------------------------------------------------- */

static bool
cs2_label_unused(struct RSCache_CS2_Function* function, struct RSCache_CS2_Insn* label)
{
    for( struct RSCache_CS2_Insn* insn = label; insn; insn = insn->next )
    {
        if( insn->kind == RSCACHE_CS2_INSN_GOTO && insn->label == label )
            return false;
    }
    return true;
}

static bool
cs2_add_ors(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    for( struct RSCache_CS2_Insn* first = function->instructions.first; first;
         first = first->next )
    {
        if( first->kind != RSCACHE_CS2_INSN_BRANCH )
            continue;
        struct RSCache_CS2_Insn* second = first->next;
        if( !second || second->kind != RSCACHE_CS2_INSN_BRANCH )
            continue;
        if( first->pass != second->pass )
            continue;

        struct RSCache_CS2_Expr* pair[2] = { first->expression, second->expression };
        second->expression = RSCache_CS2_ExprOperation(
            &dfa->fs->arena,
            NULL,
            0,
            RSCACHE_CS2_OP_SS_OR,
            RSCache_CS2_ExprFromList(&dfa->fs->arena, pair, 2),
            false);
        RSCache_CS2_ChainRemove(&function->instructions, first);
        return true;
    }
    return false;
}

static bool
cs2_add_ands(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    for( struct RSCache_CS2_Insn* first = function->instructions.first; first;
         first = first->next )
    {
        if( first->kind != RSCACHE_CS2_INSN_BRANCH || !first->pass )
            continue;
        struct RSCache_CS2_Insn* second = first->pass->next;
        if( !second || second->kind != RSCACHE_CS2_INSN_BRANCH )
            continue;
        struct RSCache_CS2_Insn* first_else = first->next;
        if( !first_else || first_else->kind != RSCACHE_CS2_INSN_GOTO )
            continue;
        struct RSCache_CS2_Insn* second_else = second->next;
        if( !second_else )
            continue;

        bool matched = false;
        if( second_else->kind == RSCACHE_CS2_INSN_GOTO )
        {
            if( first_else->label == second_else->label &&
                cs2_label_unused(function, first->pass) )
            {
                RSCache_CS2_ChainRemove(&function->instructions, first->pass);
                RSCache_CS2_ChainRemove(&function->instructions, second_else);
                RSCache_CS2_ChainRemove(&function->instructions, second);
                matched = true;
            }
        }
        else if( second_else->kind == RSCACHE_CS2_INSN_LABEL )
        {
            if( first_else->label == second_else && cs2_label_unused(function, first->pass) )
            {
                RSCache_CS2_ChainRemove(&function->instructions, first->pass);
                RSCache_CS2_ChainRemove(&function->instructions, first_else);
                RSCache_CS2_ChainRemove(&function->instructions, second_else);
                RSCache_CS2_ChainRemove(&function->instructions, second);
                matched = true;
            }
        }
        if( !matched )
            continue;

        struct RSCache_CS2_Expr* pair[2] = { first->expression, second->expression };
        first->pass = second->pass;
        first->expression = RSCache_CS2_ExprOperation(
            &dfa->fs->arena,
            NULL,
            0,
            RSCACHE_CS2_OP_SS_AND,
            RSCache_CS2_ExprFromList(&dfa->fs->arena, pair, 2),
            false);
        return true;
    }
    return false;
}

static void
cs2_add_short_circuit(struct cs2_dfa* dfa, struct RSCache_CS2_Function* function)
{
    while( cs2_add_ors(dfa, function) || cs2_add_ands(dfa, function) )
        ;
}

/* -------------------------------------------------------------------------
 * Driver
 * ---------------------------------------------------------------------- */

typedef void (*cs2_function_pass)(struct cs2_dfa*, struct RSCache_CS2_Function*);

static bool
cs2_run_function_pass(struct cs2_dfa* dfa, cs2_function_pass pass)
{
    for( int i = 0; i < dfa->fs->function_ids.count; i++ )
    {
        struct RSCache_CS2_Function* function =
            RSCache_CS2_FunctionSetGet(dfa->fs, dfa->fs->function_ids.items[i]);
        if( !function )
            continue;
        pass(dfa, function);
        if( dfa->failed )
            return false;
    }
    return true;
}

bool
RSCache_CS2_Transform(struct RSCache_CS2_FunctionSet* fs, char* error, int error_capacity)
{
    struct cs2_dfa dfa;
    memset(&dfa, 0, sizeof(dfa));
    dfa.fs = fs;
    dfa.error = error;
    dfa.error_capacity = error_capacity;

    /* Phase.DEFAULT, in order. */
    if( !cs2_run_function_pass(&dfa, cs2_remove_dead_code) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_delete_nops) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_reorder_args) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_find_array_args) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_combine_same_line) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_inline_stack_definitions) )
        return false;
    if( !cs2_calc_types(&dfa) )
        return false;
    if( !cs2_calc_identifiers(&dfa) )
        return false;
    if( !cs2_run_function_pass(&dfa, cs2_add_short_circuit) )
        return false;
    return !dfa.failed;
}
