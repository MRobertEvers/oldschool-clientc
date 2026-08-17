#include "cs2_opt.h"

#include "cs2_cfg.h"
#include "cs2_command.h"
#include "cs2_dfa.h"
#include "cs2_effects.h"
#include "cs2_interp.h"
#include "cs2_support.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cs2_opt
{
    struct RSCache_CS2_FunctionSet* fs;
    struct RSCache_CS2_Function* function;
    const struct RSCache_CS2_OptOptions* options;
    struct RSCache_CS2_OptStats* stats;
    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_opt_fail(struct cs2_opt* opt, const char* fmt, ...)
{
    assert(opt);
    assert(fmt);
    if( opt->failed )
        return;
    opt->failed = true;
    if( !opt->error || opt->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(opt->error, (size_t)opt->error_capacity, fmt, args);
    va_end(args);
}

void
RSCache_CS2_OptDefaults(struct RSCache_CS2_OptOptions* options, int level)
{
    assert(options);
    memset(options, 0, sizeof(*options));
    options->level = level;
    /*
     * Eight, and the number is measured rather than chosen.
     *
     * Inlining trades static size for dynamic cost: a call becomes a frame that
     * is never pushed, but the argument bindings and result slots are
     * instructions the `gosub` was not. Over cache.osrs239, at level 2:
     *
     *     callee limit   total ops    static gosub sites
     *              6      -1.82%       23769 -> 22203
     *              8      -0.04%       23769 -> 21207
     *             10      +2.38%       23769 -> 20424
     *             24     +12.62%       23769 -> 18535
     *
     * Eight is where the cache stops growing: 2,562 call sites removed for no
     * bytes. Past it the size cost is real and belongs to whoever decides the
     * trade — `cs2 optimize --inline-max`, and the driver's own option — rather
     * than to a default.
     */
    options->inline_max_callee_insns = 8;
    options->inline_max_insns = 4000;
    options->inline_rounds = 3;
    options->recursion_unroll_depth = 2;
    options->loop_unroll_max_trips = 8;
    options->loop_unroll_max_insns = 192;
}

/* -------------------------------------------------------------------------
 * Expression helpers
 * ---------------------------------------------------------------------- */

static bool
cs2_is_local(const struct RSCache_CS2_Expr* expr)
{
    return expr && expr->kind == RSCACHE_CS2_EXPR_ACCESS && expr->variable &&
           (expr->variable->kind == RSCACHE_CS2_VAR_INT ||
            expr->variable->kind == RSCACHE_CS2_VAR_STRING);
}

static bool
cs2_is_int_constant(const struct RSCache_CS2_Expr* expr)
{
    return expr && expr->kind == RSCACHE_CS2_EXPR_CONSTANT &&
           expr->value.stack_type == RSCACHE_CS2_STACK_INT;
}

/**
 * Everything this expression could do besides produce a value.
 *
 * True only when every node in the tree is a constant, a local read, or a PURE
 * operation over those. A global read is excluded on purpose: it is
 * READ_STATE, so deleting it is fine but *moving* it is not, and the callers
 * here want the second guarantee as well as the first.
 */
static bool
cs2_expr_is_side_effect_free(const struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return true;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
        return true;
    case RSCACHE_CS2_EXPR_ACCESS:
        return cs2_is_local(expr);
    case RSCACHE_CS2_EXPR_POINTER:
    case RSCACHE_CS2_EXPR_PROC:
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        return false;
    case RSCACHE_CS2_EXPR_COMPOUND:
    case RSCACHE_CS2_EXPR_OPERATION:
        break;
    }
    if( expr->kind == RSCACHE_CS2_EXPR_OPERATION && !RSCache_CS2_EffectIsPure(expr->opcode) )
        return false;
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(
        expr->kind == RSCACHE_CS2_EXPR_COMPOUND ? (struct RSCache_CS2_Expr*)expr
                                                : expr->arguments,
        &items, &count, &single);
    for( int i = 0; i < count; i++ )
    {
        if( !cs2_expr_is_side_effect_free(items[i]) )
            return false;
    }
    return true;
}

/** As above, but a global read is allowed — enough to delete, not to move. */
static bool
cs2_expr_is_deletable(const struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return true;
    switch( expr->kind )
    {
    case RSCACHE_CS2_EXPR_CONSTANT:
    case RSCACHE_CS2_EXPR_EVENT_PROPERTY:
    case RSCACHE_CS2_EXPR_ACCESS:
        return true;
    case RSCACHE_CS2_EXPR_POINTER:
    case RSCACHE_CS2_EXPR_PROC:
    case RSCACHE_CS2_EXPR_CLIENTSCRIPT:
        return false;
    case RSCACHE_CS2_EXPR_COMPOUND:
    case RSCACHE_CS2_EXPR_OPERATION:
        break;
    }
    if( expr->kind == RSCACHE_CS2_EXPR_OPERATION &&
        !RSCache_CS2_EffectIsDeletable(expr->opcode) )
        return false;
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    RSCache_CS2_ExprAsList(
        expr->kind == RSCACHE_CS2_EXPR_COMPOUND ? (struct RSCache_CS2_Expr*)expr
                                                : expr->arguments,
        &items, &count, &single);
    for( int i = 0; i < count; i++ )
    {
        if( !cs2_expr_is_deletable(items[i]) )
            return false;
    }
    return true;
}

static int
cs2_count_insns(const struct RSCache_CS2_Function* function)
{
    assert(function);
    return function->instructions.count;
}

/* -------------------------------------------------------------------------
 * Constant propagation and folding
 *
 * Per basic block, which is what makes it sound without a dataflow solve: the
 * table of known values is dropped at every label, so nothing learned on one
 * path is believed on another. Within a block the only thing that can change a
 * frame local is an instruction that writes it — no opcode reaches into another
 * frame — so tracking the writes is tracking everything.
 * ---------------------------------------------------------------------- */

struct cs2_const_table
{
    /* Parallel arrays over int slots then string slots. */
    struct RSCache_CS2_Expr** value;
    int int_count;
    int string_count;
    int total;
};

static int
cs2_const_slot(const struct cs2_const_table* table, const struct RSCache_CS2_Variable* variable)
{
    assert(table);
    if( !variable )
        return -1;
    if( variable->kind == RSCACHE_CS2_VAR_INT )
        return variable->id < table->int_count ? variable->id : -1;
    if( variable->kind == RSCACHE_CS2_VAR_STRING || variable->kind == RSCACHE_CS2_VAR_ARRAY )
        return variable->id < table->string_count ? table->int_count + variable->id : -1;
    return -1;
}

static void
cs2_const_clear(struct cs2_const_table* table)
{
    assert(table);
    memset(table->value, 0, (size_t)table->total * sizeof(*table->value));
}

/** Forget every entry that reads `slot`, because `slot` has just been written. */
static void
cs2_const_kill_readers(struct cs2_const_table* table, int slot)
{
    assert(table);
    for( int i = 0; i < table->total; i++ )
    {
        struct RSCache_CS2_Expr* value = table->value[i];
        if( !value || value->kind != RSCACHE_CS2_EXPR_ACCESS )
            continue;
        if( cs2_const_slot(table, value->variable) == slot )
            table->value[i] = NULL;
    }
}

/** Replace reads of known slots, and fold what that makes constant. */
static struct RSCache_CS2_Expr*
cs2_fold_expr(
    struct cs2_opt* opt,
    struct cs2_const_table* table,
    struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return NULL;

    if( expr->kind == RSCACHE_CS2_EXPR_ACCESS )
    {
        int slot = cs2_const_slot(table, expr->variable);
        if( slot < 0 || !table->value[slot] )
            return expr;
        /* Arrays are named by an operand, never read as a value; substituting
         * one would produce an expression the lowerer cannot spell. */
        if( expr->variable->kind == RSCACHE_CS2_VAR_ARRAY )
            return expr;
        /* Never substitute a local for itself; that is not progress and it
         * makes the fixpoint loop below think something changed. */
        if( table->value[slot] == expr )
            return expr;
        opt->stats->constants_propagated++;
        return table->value[slot];
    }

    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    if( expr->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        RSCache_CS2_ExprAsList(expr, &items, &count, &single);
        for( int i = 0; i < count; i++ )
            items[i] = cs2_fold_expr(opt, table, items[i]);
        return expr;
    }

    /* Hook payloads and trigger lists are data the client re-reads later; the
     * component is an ordinary value and may fold. */
    if( expr->kind == RSCACHE_CS2_EXPR_CLIENTSCRIPT )
    {
        expr->component = cs2_fold_expr(opt, table, expr->component);
        return expr;
    }

    if( expr->arguments )
    {
        RSCache_CS2_ExprAsList(expr->arguments, &items, &count, &single);
        for( int i = 0; i < count; i++ )
        {
            /* An array operation names its array in argument 0 and would lose
             * that name if the slot happened to hold a constant. */
            if( i == 0 && expr->kind == RSCACHE_CS2_EXPR_OPERATION &&
                (expr->opcode == RSCACHE_CS2_OP_PUSH_ARRAY_INT ||
                 expr->opcode == RSCACHE_CS2_OP_POP_ARRAY_INT ||
                 expr->opcode == RSCACHE_CS2_OP_DEFINE_ARRAY) )
                continue;
            items[i] = cs2_fold_expr(opt, table, items[i]);
        }
        if( count == 1 && single )
            expr->arguments = items[0];
    }

    if( expr->kind != RSCACHE_CS2_EXPR_OPERATION || !RSCache_CS2_EffectIsPure(expr->opcode) )
        return expr;

    RSCache_CS2_ExprAsList(expr->arguments, &items, &count, &single);
    int values[8];
    if( count > (int)(sizeof(values) / sizeof(values[0])) )
        return expr;
    for( int i = 0; i < count; i++ )
    {
        if( !cs2_is_int_constant(items[i]) )
            return expr;
        values[i] = items[i]->value.int_value;
    }
    int folded = 0;
    if( !RSCache_CS2_EffectFoldInt(expr->opcode, values, count, &folded) )
        return expr;
    opt->stats->constants_folded++;
    return RSCache_CS2_ExprConstantInt(&opt->fs->arena, folded);
}

/**
 * The comparison a branch performs, over two constants.
 *
 * The opcode numbering is the client's: 8 equal, 7 not-equal, 9 less, 10
 * greater, 31 less-or-equal, 32 greater-or-equal. False when the opcode is not
 * one of them.
 */
static bool
cs2_eval_compare(int opcode, int left, int right, bool* out)
{
    assert(out);
    switch( opcode )
    {
    case RSCACHE_CS2_OP_BRANCH_EQUALS:
        *out = left == right;
        return true;
    case RSCACHE_CS2_OP_BRANCH_NOT:
        *out = left != right;
        return true;
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN:
        *out = left < right;
        return true;
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN:
        *out = left > right;
        return true;
    case RSCACHE_CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        *out = left <= right;
        return true;
    case RSCACHE_CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
        *out = left >= right;
        return true;
    default:
        return false;
    }
}

static void
cs2_pass_fold(struct cs2_opt* opt)
{
    assert(opt);
    struct RSCache_CS2_Function* function = opt->function;

    struct cs2_const_table table;
    memset(&table, 0, sizeof(table));
    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn; insn = insn->next )
    {
        struct RSCache_CS2_Expr* probe[2] = { insn->definitions, insn->expression };
        for( int p = 0; p < 2; p++ )
        {
            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** items = NULL;
            int count = 0;
            if( !probe[p] )
                continue;
            RSCache_CS2_ExprAsList(probe[p], &items, &count, &single);
            for( int i = 0; i < count; i++ )
            {
                if( !items[i] || !items[i]->variable )
                    continue;
                if( items[i]->variable->kind == RSCACHE_CS2_VAR_INT &&
                    items[i]->variable->id + 1 > table.int_count )
                    table.int_count = items[i]->variable->id + 1;
                if( (items[i]->variable->kind == RSCACHE_CS2_VAR_STRING ||
                     items[i]->variable->kind == RSCACHE_CS2_VAR_ARRAY) &&
                    items[i]->variable->id + 1 > table.string_count )
                    table.string_count = items[i]->variable->id + 1;
            }
        }
    }
    /* Generous, because a slot the scan above missed is one this pass simply
     * never learns anything about, which is safe. */
    table.int_count += 8;
    table.string_count += 8;
    table.total = table.int_count + table.string_count;
    table.value = (struct RSCache_CS2_Expr**)calloc((size_t)table.total, sizeof(*table.value));
    assert(table.value);

    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn; insn = insn->next )
    {
        if( insn->kind == RSCACHE_CS2_INSN_LABEL )
        {
            /* A join: nothing learned above this point survives it. */
            cs2_const_clear(&table);
            continue;
        }

        insn->expression = cs2_fold_expr(opt, &table, insn->expression);

        if( insn->kind == RSCACHE_CS2_INSN_BRANCH && insn->expression &&
            insn->expression->kind == RSCACHE_CS2_EXPR_OPERATION )
        {
            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** items = NULL;
            int count = 0;
            RSCache_CS2_ExprAsList(insn->expression->arguments, &items, &count, &single);
            bool taken = false;
            if( count == 2 && cs2_is_int_constant(items[0]) && cs2_is_int_constant(items[1]) &&
                cs2_eval_compare(insn->expression->opcode, items[0]->value.int_value,
                                 items[1]->value.int_value, &taken) )
            {
                opt->stats->branches_folded++;
                if( taken )
                {
                    insn->kind = RSCACHE_CS2_INSN_GOTO;
                    insn->label = insn->pass;
                    insn->expression = NULL;
                }
                else
                {
                    /* Never taken: the test and its operands go, and control
                     * falls through exactly as it already did. */
                    struct RSCache_CS2_Insn* dead = insn;
                    insn = insn->prev ? insn->prev : function->instructions.first;
                    RSCache_CS2_ChainRemove(&function->instructions, dead);
                    opt->stats->instructions_removed++;
                    if( !insn )
                        break;
                    continue;
                }
            }
        }

        if( insn->kind != RSCACHE_CS2_INSN_ASSIGNMENT )
            continue;

        /* A `define_array` rebinds the handle in its string slot. */
        if( insn->expression && insn->expression->kind == RSCACHE_CS2_EXPR_OPERATION &&
            insn->expression->opcode == RSCACHE_CS2_OP_DEFINE_ARRAY )
        {
            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** items = NULL;
            int count = 0;
            RSCache_CS2_ExprAsList(insn->expression->arguments, &items, &count, &single);
            if( count > 0 && items[0] && items[0]->variable )
            {
                int slot = cs2_const_slot(&table, items[0]->variable);
                if( slot >= 0 )
                    table.value[slot] = NULL;
            }
        }

        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** targets = NULL;
        int target_count = 0;
        RSCache_CS2_ExprAsList(insn->definitions, &targets, &target_count, &single);
        /*
         * A single store of a constant *or of another local* becomes known.
         *
         * The copy half is what pays for inlining: binding an argument emits
         * `$param = $caller_value`, and without copy propagation every inlined
         * call keeps a store and a load per argument that the original call did
         * not have. Recording the copy lets the reads below rewrite themselves
         * back to the caller's own local, and the dead-store pass then removes
         * the binding entirely.
         */
        bool one_value = target_count == 1 && insn->expression &&
                         (insn->expression->kind == RSCACHE_CS2_EXPR_CONSTANT ||
                          cs2_is_local(insn->expression));
        for( int i = 0; i < target_count; i++ )
        {
            if( !targets[i] || targets[i]->kind != RSCACHE_CS2_EXPR_ACCESS )
                continue;
            int slot = cs2_const_slot(&table, targets[i]->variable);
            if( slot < 0 )
                continue;
            /* Anything that was a copy *of* this slot is now stale. */
            cs2_const_kill_readers(&table, slot);
            table.value[slot] =
                one_value && cs2_is_local(targets[i]) ? insn->expression : NULL;
        }
    }
    free(table.value);
}

/* -------------------------------------------------------------------------
 * Chain cleanup: unreachable blocks, redundant jumps, unused labels
 * ---------------------------------------------------------------------- */

static bool
cs2_pass_drop_unreachable(struct cs2_opt* opt)
{
    assert(opt);
    struct RSCache_CS2_Cfg cfg;
    RSCache_CS2_CfgBuild(&cfg, opt->function);
    bool changed = false;
    for( int i = 0; i < cfg.block_count; i++ )
    {
        if( cfg.blocks[i].reachable )
            continue;
        struct RSCache_CS2_Insn* at = cfg.blocks[i].first;
        struct RSCache_CS2_Insn* stop = cfg.blocks[i].last;
        while( at )
        {
            struct RSCache_CS2_Insn* next = at->next;
            RSCache_CS2_ChainRemove(&opt->function->instructions, at);
            opt->stats->instructions_removed++;
            changed = true;
            if( at == stop )
                break;
            at = next;
        }
    }
    RSCache_CS2_CfgFree(&cfg);
    return changed;
}

/** Is this label the destination of anything still in the chain? */
static bool
cs2_label_is_used(struct RSCache_CS2_Function* function, struct RSCache_CS2_Insn* label)
{
    assert(function);
    assert(label);
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; at = at->next )
    {
        if( at->kind == RSCACHE_CS2_INSN_BRANCH && at->pass == label )
            return true;
        if( at->kind == RSCACHE_CS2_INSN_GOTO && at->label == label )
            return true;
        if( at->kind != RSCACHE_CS2_INSN_SWITCH )
            continue;
        for( int i = 0; i < at->case_count; i++ )
        {
            if( at->case_labels[i] == label )
                return true;
        }
    }
    /* A return still points at one, and re-emitting that jump is optional. */
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; at = at->next )
    {
        if( at->kind == RSCACHE_CS2_INSN_RETURN && at->dead_goto_target == label )
            return true;
    }
    return false;
}

/** The first instruction at or after `at` that is not a label. */
static struct RSCache_CS2_Insn*
cs2_skip_labels(struct RSCache_CS2_Insn* at)
{
    while( at && at->kind == RSCACHE_CS2_INSN_LABEL )
        at = at->next;
    return at;
}

static bool
cs2_pass_clean_jumps(struct cs2_opt* opt)
{
    assert(opt);
    struct RSCache_CS2_Function* function = opt->function;
    bool changed = false;

    /*
     * Thread a jump whose destination only jumps again.
     *
     * Bounded rather than followed to a fixpoint: a chain of gotos can be a
     * cycle (`L: goto L` is legal bytecode and is how a script hangs), and
     * following one would hang the optimizer instead.
     */
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; at = at->next )
    {
        struct RSCache_CS2_Insn** slot = NULL;
        if( at->kind == RSCACHE_CS2_INSN_GOTO )
            slot = &at->label;
        else if( at->kind == RSCACHE_CS2_INSN_BRANCH )
            slot = &at->pass;
        if( !slot || !*slot )
            continue;
        for( int hop = 0; hop < 8; hop++ )
        {
            struct RSCache_CS2_Insn* target = cs2_skip_labels(*slot);
            if( !target || target->kind != RSCACHE_CS2_INSN_GOTO || !target->label ||
                target->label == *slot )
                break;
            *slot = target->label;
            changed = true;
        }
    }

    /* A jump to the very next instruction is not a jump. */
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; )
    {
        struct RSCache_CS2_Insn* next = at->next;
        struct RSCache_CS2_Insn* destination = NULL;
        if( at->kind == RSCACHE_CS2_INSN_GOTO )
            destination = at->label;
        else if( at->kind == RSCACHE_CS2_INSN_BRANCH )
            destination = at->pass;
        if( !destination )
        {
            at = next;
            continue;
        }
        struct RSCache_CS2_Insn* after = at->next;
        while( after && after->kind == RSCACHE_CS2_INSN_LABEL && after != destination )
            after = after->next;
        if( after != destination )
        {
            at = next;
            continue;
        }
        if( at->kind == RSCACHE_CS2_INSN_BRANCH && !cs2_expr_is_side_effect_free(at->expression) )
        {
            /* The test still has to run even though both arms agree. */
            at = next;
            continue;
        }
        RSCache_CS2_ChainRemove(&function->instructions, at);
        opt->stats->instructions_removed++;
        changed = true;
        at = next;
    }

    /* Labels nothing points at: harmless in the output, but they end a basic
     * block, so removing them lets the folder see across the join. */
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; )
    {
        struct RSCache_CS2_Insn* next = at->next;
        if( at->kind == RSCACHE_CS2_INSN_LABEL && !cs2_label_is_used(function, at) )
        {
            RSCache_CS2_ChainRemove(&function->instructions, at);
            changed = true;
        }
        at = next;
    }
    return changed;
}

/* -------------------------------------------------------------------------
 * Dead stores
 * ---------------------------------------------------------------------- */

/*
 * Dead stores, in two phases: decide, then delete.
 *
 * The order is the whole correctness argument. Liveness is a property of the
 * *graph*, and the graph is built from the chain — so deleting an instruction
 * mid-walk leaves every later question being answered from a description of a
 * program that no longer exists. Doing that removed 5,448 instructions across
 * cache.osrs239 instead of the 200-odd that are really dead, and among them was
 * a loop's `$i = calc($i + 1)`: script 4731 then ran until the VM's
 * million-cycle cap cut it off, which is the only reason it was noticed at all.
 * The lowering gate cannot see this — an infinite loop is perfectly well-formed
 * bytecode — so the pass has to be right rather than merely checked.
 *
 * Collecting first is sound in the direction that matters: removing a dead
 * store can only make more stores dead, never fewer, so a set computed against
 * the unmutated chain is a subset of what is really dead. The fixpoint loop in
 * the driver picks up the rest on its next round.
 */
static bool
cs2_pass_dead_stores(struct cs2_opt* opt)
{
    assert(opt);
    struct RSCache_CS2_Cfg cfg;
    RSCache_CS2_CfgBuild(&cfg, opt->function);

    struct RSCache_CS2_Vec dead;
    RSCache_CS2_VecInit(&dead);
    for( struct RSCache_CS2_Insn* at = opt->function->instructions.first; at; at = at->next )
    {
        if( at->kind != RSCACHE_CS2_INSN_ASSIGNMENT )
            continue;
        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** targets = NULL;
        int count = 0;
        RSCache_CS2_ExprAsList(at->definitions, &targets, &count, &single);
        if( count != 1 || !cs2_is_local(targets[0]) )
            continue;
        if( !cs2_expr_is_deletable(at->expression) )
            continue;
        int slot = RSCache_CS2_CfgSlotOf(&cfg, targets[0]->variable);
        if( slot < 0 || RSCache_CS2_CfgLiveAfter(&cfg, at, slot) )
            continue;
        RSCache_CS2_VecPush(&dead, at);
    }
    RSCache_CS2_CfgFree(&cfg);

    for( int i = 0; i < dead.count; i++ )
    {
        RSCache_CS2_ChainRemove(&opt->function->instructions,
                                (struct RSCache_CS2_Insn*)dead.items[i]);
        opt->stats->instructions_removed++;
    }
    bool changed = dead.count > 0;
    RSCache_CS2_VecFree(&dead);
    return changed;
}

/* -------------------------------------------------------------------------
 * Inlining
 * ---------------------------------------------------------------------- */

struct cs2_clone
{
    struct cs2_opt* opt;
    struct RSCache_CS2_Function* callee;
    /* Callee Variable* -> caller Variable* */
    struct RSCache_CS2_Map variables;
    /* Callee Insn* (LABEL) -> caller Insn* */
    struct RSCache_CS2_Map labels;
    int next_int_slot;
    int next_string_slot;
};

/** The caller-side variable a callee variable maps to, allocating on first use. */
static struct RSCache_CS2_Variable*
cs2_clone_variable(struct cs2_clone* clone, struct RSCache_CS2_Variable* variable)
{
    assert(clone);
    assert(variable);
    /*
     * Globals keep their identity but not their storage.
     *
     * A varp is shared state, so the clone must name the same one — but the
     * *node* naming it was interned in the callee's function set and dies with
     * that set's arena. Returning it unchanged leaves the caller holding a
     * pointer into freed memory that reads back as kind 0, id 0: every
     * `pop_varc_string` in an inlined body became `pop_var 0`, quietly writing
     * varp 0 instead of the client variable it meant. Re-interning in the
     * caller's set is what makes the identity outlive the callee.
     */
    if( RSCache_CS2_VarIsGlobal(variable->kind) )
        return RSCache_CS2_VarIntern(clone->opt->fs, variable->kind, variable->script,
                                     variable->id);
    void* existing = RSCache_CS2_MapGet(&clone->variables, variable);
    if( existing )
        return (struct RSCache_CS2_Variable*)existing;
    int slot;
    if( variable->kind == RSCACHE_CS2_VAR_INT )
        slot = clone->next_int_slot++;
    else
        slot = clone->next_string_slot++;
    struct RSCache_CS2_Variable* fresh = RSCache_CS2_VarIntern(
        clone->opt->fs, variable->kind, clone->opt->function->id, slot);
    RSCache_CS2_MapPut(&clone->variables, variable, fresh);
    return fresh;
}

static struct RSCache_CS2_Expr*
cs2_clone_expr(struct cs2_clone* clone, struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return NULL;
    struct RSCache_CS2_Arena* arena = &clone->opt->fs->arena;
    struct RSCache_CS2_Expr* copy =
        (struct RSCache_CS2_Expr*)RSCache_CS2_ArenaAlloc(arena, sizeof(*copy));
    *copy = *expr;
    if( expr->variable )
        copy->variable = cs2_clone_variable(clone, expr->variable);

    /*
     * Strings are re-owned, not shared.
     *
     * A callee is interpreted into its own function set and that set is freed
     * the moment the splice is done, taking its arena — and every string
     * constant and hook descriptor in it — with it. A shallow copy leaves the
     * caller holding pointers into freed memory, which reads back as a hook
     * whose argument descriptor is byte 0x01 and a script id that is not a
     * literal: 112 scripts of cache.osrs239, caught by the re-interpret gate.
     */
    if( expr->value.string_value )
        copy->value.string_value = RSCache_CS2_ArenaStrDup(arena, expr->value.string_value);
    if( expr->hook_descriptor )
        copy->hook_descriptor = RSCache_CS2_ArenaStrDup(arena, expr->hook_descriptor);

    if( expr->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        RSCache_CS2_VecInit(&copy->children);
        RSCache_CS2_ArenaTrackVec(arena, &copy->children);
        for( int i = 0; i < expr->children.count; i++ )
            RSCache_CS2_VecPush(
                &copy->children,
                cs2_clone_expr(clone, (struct RSCache_CS2_Expr*)expr->children.items[i]));
        return copy;
    }
    copy->arguments = cs2_clone_expr(clone, expr->arguments);
    copy->triggers = cs2_clone_expr(clone, expr->triggers);
    copy->component = cs2_clone_expr(clone, expr->component);
    copy->pointer_source = cs2_clone_expr(clone, expr->pointer_source);
    if( expr->stack_type_count > 0 && expr->stack_types )
    {
        copy->stack_types = (enum RSCache_CS2_StackType*)RSCache_CS2_ArenaAlloc(
            arena, (size_t)expr->stack_type_count * sizeof(*copy->stack_types));
        memcpy(copy->stack_types, expr->stack_types,
               (size_t)expr->stack_type_count * sizeof(*copy->stack_types));
    }
    return copy;
}

/** The caller-side label standing for a callee label. */
static struct RSCache_CS2_Insn*
cs2_clone_label(struct cs2_clone* clone, struct RSCache_CS2_Insn* label)
{
    assert(clone);
    if( !label )
        return NULL;
    void* existing = RSCache_CS2_MapGet(&clone->labels, label);
    if( existing )
        return (struct RSCache_CS2_Insn*)existing;
    struct RSCache_CS2_Insn* fresh =
        RSCache_CS2_InsnLabel(&clone->opt->fs->arena, label->label_id);
    RSCache_CS2_MapPut(&clone->labels, label, fresh);
    return fresh;
}

/** Substitute `with` for `target` inside `root`; returns the possibly-new root. */
static struct RSCache_CS2_Expr*
cs2_substitute(
    struct RSCache_CS2_Expr* root,
    struct RSCache_CS2_Expr* target,
    struct RSCache_CS2_Expr* with)
{
    if( !root )
        return NULL;
    if( root == target )
        return with;
    if( root->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        for( int i = 0; i < root->children.count; i++ )
            root->children.items[i] = cs2_substitute(
                (struct RSCache_CS2_Expr*)root->children.items[i], target, with);
        return root;
    }
    root->arguments = cs2_substitute(root->arguments, target, with);
    root->component = cs2_substitute(root->component, target, with);
    return root;
}

/** Find a PROC node anywhere in the tree, or NULL. */
static struct RSCache_CS2_Expr*
cs2_find_proc(struct RSCache_CS2_Expr* expr)
{
    if( !expr )
        return NULL;
    if( expr->kind == RSCACHE_CS2_EXPR_PROC )
        return expr;
    /* A hook's payload is data, not a call site — its arguments are read by
     * the client later and must not be evaluated here. */
    if( expr->kind == RSCACHE_CS2_EXPR_CLIENTSCRIPT )
        return NULL;
    if( expr->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        for( int i = 0; i < expr->children.count; i++ )
        {
            struct RSCache_CS2_Expr* found =
                cs2_find_proc((struct RSCache_CS2_Expr*)expr->children.items[i]);
            if( found )
                return found;
        }
        return NULL;
    }
    return cs2_find_proc(expr->arguments);
}

/**
 * May the call be lifted out of the expression it sits in?
 *
 * Everything to the call's *left* in the same expression is evaluated before it
 * in the original and would be evaluated after it once the call becomes its own
 * statement. A constant or a frame-local read is unaffected by anything the
 * callee can do — the callee's own locals are renamed into free slots, and no
 * opcode writes another frame's locals — so those are safe to leave where they
 * are. Anything else is not, and the call is left alone.
 */
static bool
cs2_left_siblings_are_safe(struct RSCache_CS2_Expr* root, struct RSCache_CS2_Expr* call)
{
    if( !root || root == call )
        return true;
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    if( root->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        RSCache_CS2_ExprAsList(root, &items, &count, &single);
    }
    else if( root->arguments )
    {
        RSCache_CS2_ExprAsList(root->arguments, &items, &count, &single);
    }
    else
    {
        return true;
    }
    for( int i = 0; i < count; i++ )
    {
        if( cs2_find_proc(items[i]) == call )
            return cs2_left_siblings_are_safe(items[i], call);
        /* Left of the call: must be inert. Right of it: evaluated after the
         * call in both versions, so nothing to check. */
        if( !cs2_expr_is_side_effect_free(items[i]) )
            return false;
    }
    return true;
}

/**
 * Is this call and this callee a shape the splice can actually reproduce?
 *
 * Two things have to hold and neither is guaranteed. Every argument must be one
 * stack value, because binding is per value and an argument that is itself a
 * multi-value call has no single parameter to go to. And every `return` in the
 * callee must leave exactly what the callee declares, because the splice turns
 * each one into a store into that many result slots.
 *
 * Both are ordinary for hand-written scripts and neither is universal, so they
 * are checked rather than assumed — `RSCache_CS2_InsnAssignment` asserts on the
 * mismatch, which is a crash rather than a bad inline, but only because the
 * check exists somewhere.
 */
static bool
cs2_inline_shape_is_supported(
    struct RSCache_CS2_Expr* call,
    struct RSCache_CS2_Function* callee)
{
    assert(call);
    assert(callee);
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** args = NULL;
    int arg_count = 0;
    RSCache_CS2_ExprAsList(call->arguments, &args, &arg_count, &single);
    for( int i = 0; i < arg_count; i++ )
    {
        if( RSCache_CS2_ExprStackTypeCount(args[i]) != 1 )
            return false;
    }
    /* The callee's own parameters are numbered by bank over its declared
     * counts; a call passing a different number of values than the callee takes
     * would bind the wrong slots. */
    int int_args = 0;
    int string_args = 0;
    for( int i = 0; i < arg_count; i++ )
    {
        if( RSCache_CS2_ExprStackTypeAt(args[i], 0) == RSCACHE_CS2_STACK_STRING )
            string_args++;
        else
            int_args++;
    }
    if( int_args != callee->original_int_argument_count ||
        string_args != callee->original_string_argument_count )
        return false;

    for( struct RSCache_CS2_Insn* at = callee->instructions.first; at; at = at->next )
    {
        if( at->kind != RSCACHE_CS2_INSN_RETURN )
            continue;
        if( RSCache_CS2_ExprStackTypeCount(at->expression) != callee->return_type_count )
            return false;
    }
    return true;
}

/**
 * Inline one call in place.
 *
 * The callee arrives as its own FunctionSet so that what is spliced in is the
 * body of the bytecode that will ship, not a second opinion about it.
 */
static bool
cs2_inline_one(
    struct cs2_opt* opt,
    struct RSCache_CS2_Insn* insn,
    struct RSCache_CS2_Expr* call,
    struct RSCache_CS2_FunctionSet* callee_fs,
    struct RSCache_CS2_Function* callee)
{
    assert(opt);
    assert(insn);
    assert(call);
    assert(callee_fs);
    assert(callee);

    struct RSCache_CS2_Arena* arena = &opt->fs->arena;
    struct RSCache_CS2_Function* caller = opt->function;

    struct cs2_clone clone;
    memset(&clone, 0, sizeof(clone));
    clone.opt = opt;
    clone.callee = callee;
    RSCache_CS2_MapInit(&clone.variables);
    RSCache_CS2_MapInit(&clone.labels);

    /* Fresh slots start above every slot the caller uses, including the ones a
     * previous inline handed out. */
    struct RSCache_CS2_Cfg cfg;
    RSCache_CS2_CfgBuild(&cfg, caller);
    clone.next_int_slot = cfg.slots.int_count;
    clone.next_string_slot = cfg.slots.string_count;
    RSCache_CS2_CfgFree(&cfg);

    /*
     * Arguments bind by bank ordinal, which is what the VM does: the i-th int
     * value pushed becomes int local i and the j-th string value string local
     * j, regardless of how they were interleaved at the call site.
     */
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** args = NULL;
    int arg_count = 0;
    RSCache_CS2_ExprAsList(call->arguments, &args, &arg_count, &single);

    struct RSCache_CS2_Vec prologue;
    RSCache_CS2_VecInit(&prologue);
    int next_int = 0;
    int next_string = 0;
    for( int i = 0; i < arg_count; i++ )
    {
        enum RSCache_CS2_StackType bank = RSCache_CS2_ExprStackTypeAt(args[i], 0);
        /*
         * Interned in the *callee's* set, not the caller's.
         *
         * `cs2_clone_variable` is a map keyed by pointer, and a variable is only
         * pointer-equal to itself within one function set. Interning the
         * parameter here would produce a different pointer from the one the
         * callee's body reads, so the prologue would write one fresh slot and
         * the body would read another — arguments silently dropped, every
         * inline, with only the stack-type check noticing.
         */
        struct RSCache_CS2_Variable* parameter = RSCache_CS2_VarIntern(
            callee_fs,
            bank == RSCACHE_CS2_STACK_STRING ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
            callee->id,
            bank == RSCACHE_CS2_STACK_STRING ? next_string++ : next_int++);
        struct RSCache_CS2_Variable* target = cs2_clone_variable(&clone, parameter);
        RSCache_CS2_VecPush(
            &prologue,
            RSCache_CS2_InsnAssignment(
                arena, RSCache_CS2_ExprAccess(arena, target, NULL), args[i]));
    }

    /*
     * Locals the callee reads before writing start zeroed in a fresh frame and
     * do not in a slot the caller has been using. Rather than prove which those
     * are, every renamed slot is initialised: the store is one instruction, and
     * the constant folder deletes the ones that turn out to be redundant.
     */
    struct RSCache_CS2_Insn* join = RSCache_CS2_InsnLabel(arena, -1);
    int return_count = callee->return_type_count;
    struct RSCache_CS2_Expr** results = NULL;
    if( return_count > 0 )
    {
        results = (struct RSCache_CS2_Expr**)RSCache_CS2_ArenaAlloc(
            arena, (size_t)return_count * sizeof(*results));
        for( int i = 0; i < return_count; i++ )
        {
            bool is_string = callee->return_types[i] == RSCACHE_CS2_STACK_STRING;
            struct RSCache_CS2_Variable* slot = RSCache_CS2_VarIntern(
                opt->fs,
                is_string ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
                caller->id,
                is_string ? clone.next_string_slot++ : clone.next_int_slot++);
            results[i] = RSCache_CS2_ExprAccess(arena, slot, NULL);
        }
    }

    /*
     * A callee that returns from exactly one place, at the end, needs no join.
     *
     * That is the common shape — most procs compute a value and return it — and
     * the label plus the `goto` reaching it are pure overhead in it. Skipping
     * them is what keeps a small inline from costing more instructions than the
     * call it replaced.
     */
    int return_sites = 0;
    for( struct RSCache_CS2_Insn* at = callee->instructions.first; at; at = at->next )
    {
        if( at->kind == RSCACHE_CS2_INSN_RETURN )
            return_sites++;
    }
    bool needs_join =
        return_sites > 1 || !callee->instructions.last ||
        callee->instructions.last->kind != RSCACHE_CS2_INSN_RETURN;

    /* Clone the body, turning each callee return into "store results, jump". */
    struct RSCache_CS2_Vec body;
    RSCache_CS2_VecInit(&body);
    for( struct RSCache_CS2_Insn* at = callee->instructions.first; at; at = at->next )
    {
        switch( at->kind )
        {
        case RSCACHE_CS2_INSN_LABEL:
            RSCache_CS2_VecPush(&body, cs2_clone_label(&clone, at));
            break;
        case RSCACHE_CS2_INSN_ASSIGNMENT:
            RSCache_CS2_VecPush(
                &body,
                RSCache_CS2_InsnAssignment(arena, cs2_clone_expr(&clone, at->definitions),
                                           cs2_clone_expr(&clone, at->expression)));
            break;
        case RSCACHE_CS2_INSN_RETURN:
        {
            struct RSCache_CS2_Expr* value = cs2_clone_expr(&clone, at->expression);
            if( return_count > 0 )
            {
                struct RSCache_CS2_Expr* targets = RSCache_CS2_ExprFromList(
                    arena, (struct RSCache_CS2_Expr* const*)results, return_count);
                RSCache_CS2_VecPush(&body,
                                    RSCache_CS2_InsnAssignment(arena, targets, value));
            }
            else if( value && !cs2_expr_is_side_effect_free(value) )
            {
                RSCache_CS2_VecPush(
                    &body,
                    RSCache_CS2_InsnAssignment(arena, RSCache_CS2_ExprEmpty(arena), value));
            }
            if( needs_join )
            {
                struct RSCache_CS2_Insn* jump = RSCache_CS2_InsnGoto(arena, 0);
                jump->label = join;
                RSCache_CS2_VecPush(&body, jump);
            }
            break;
        }
        case RSCACHE_CS2_INSN_BRANCH:
        {
            struct RSCache_CS2_Insn* branch = RSCache_CS2_InsnBranch(
                arena, cs2_clone_expr(&clone, at->expression), 0);
            branch->pass = cs2_clone_label(&clone, at->pass);
            RSCache_CS2_VecPush(&body, branch);
            break;
        }
        case RSCACHE_CS2_INSN_GOTO:
        {
            struct RSCache_CS2_Insn* jump = RSCache_CS2_InsnGoto(arena, 0);
            jump->label = cs2_clone_label(&clone, at->label);
            RSCache_CS2_VecPush(&body, jump);
            break;
        }
        case RSCACHE_CS2_INSN_SWITCH:
        {
            int* keys = (int*)RSCache_CS2_ArenaAlloc(
                arena, (size_t)(at->case_count > 0 ? at->case_count : 1) * sizeof(int));
            int* targets = (int*)RSCache_CS2_ArenaAlloc(
                arena, (size_t)(at->case_count > 0 ? at->case_count : 1) * sizeof(int));
            for( int i = 0; i < at->case_count; i++ )
            {
                keys[i] = at->case_keys[i];
                targets[i] = 0;
            }
            struct RSCache_CS2_Insn* sw = RSCache_CS2_InsnSwitch(
                arena, cs2_clone_expr(&clone, at->expression), keys, targets,
                at->case_count);
            for( int i = 0; i < at->case_count; i++ )
                sw->case_labels[i] = cs2_clone_label(&clone, at->case_labels[i]);
            RSCache_CS2_VecPush(&body, sw);
            break;
        }
        }
    }

    /* Splice: prologue, body, join label — all before the call's statement. */
    for( int i = 0; i < prologue.count; i++ )
        RSCache_CS2_ChainInsertBefore(
            &caller->instructions, (struct RSCache_CS2_Insn*)prologue.items[i], insn);
    for( int i = 0; i < body.count; i++ )
        RSCache_CS2_ChainInsertBefore(
            &caller->instructions, (struct RSCache_CS2_Insn*)body.items[i], insn);
    if( needs_join )
        RSCache_CS2_ChainInsertBefore(&caller->instructions, join, insn);
    RSCache_CS2_VecFree(&prologue);
    RSCache_CS2_VecFree(&body);

    /* The call node becomes the values it produced. */
    struct RSCache_CS2_Expr* replacement =
        return_count > 0
            ? RSCache_CS2_ExprFromList(
                  arena, (struct RSCache_CS2_Expr* const*)results, return_count)
            : RSCache_CS2_ExprEmpty(arena);
    if( insn->expression == call )
        insn->expression = replacement;
    else
        insn->expression = cs2_substitute(insn->expression, call, replacement);

    RSCache_CS2_MapFree(&clone.variables);
    RSCache_CS2_MapFree(&clone.labels);
    opt->stats->calls_inlined++;
    return true;
}

/** Interpret a callee into its own function set, ready to be cloned from. */
static struct RSCache_CS2_Function*
cs2_load_callee(
    struct cs2_opt* opt,
    int script_id,
    struct RSCache_CS2_FunctionSet* into)
{
    assert(opt);
    assert(into);
    if( !opt->options->callees.scripts.load )
        return NULL;
    char error[256] = { 0 };
    RSCache_CS2_FunctionSetInit(into);
    if( !RSCache_CS2_Interpret(into, &script_id, 1, &opt->options->callees, error,
                               (int)sizeof(error)) ||
        !RSCache_CS2_TransformCore(into, error, (int)sizeof(error)) )
        return NULL;
    return RSCache_CS2_FunctionSetGet(into, script_id);
}

static bool
cs2_pass_inline(struct cs2_opt* opt)
{
    assert(opt);
    bool changed = false;
    for( struct RSCache_CS2_Insn* at = opt->function->instructions.first; at; at = at->next )
    {
        if( at->kind != RSCACHE_CS2_INSN_ASSIGNMENT && at->kind != RSCACHE_CS2_INSN_RETURN )
            continue;
        struct RSCache_CS2_Expr* call = cs2_find_proc(at->expression);
        if( !call )
            continue;
        /* Never itself: that is what the recursion passes are for, and doing it
         * here would not terminate. */
        if( call->script_id == opt->function->id )
            continue;
        if( cs2_count_insns(opt->function) >= opt->options->inline_max_insns )
            break;
        if( !cs2_left_siblings_are_safe(at->expression, call) )
            continue;

        struct RSCache_CS2_FunctionSet callee_fs;
        struct RSCache_CS2_Function* callee = cs2_load_callee(opt, call->script_id, &callee_fs);
        if( !callee )
        {
            RSCache_CS2_FunctionSetFree(&callee_fs);
            continue;
        }
        bool small_enough = callee->instructions.count <= opt->options->inline_max_callee_insns;
        bool has_own_call = false;
        bool touches_arrays = false;
        for( struct RSCache_CS2_Insn* body = callee->instructions.first; body;
             body = body->next )
        {
            if( cs2_find_proc(body->expression) )
                has_own_call = true;
            struct RSCache_CS2_Expr* expression = body->expression;
            if( expression && expression->kind == RSCACHE_CS2_EXPR_OPERATION &&
                (expression->opcode == RSCACHE_CS2_OP_DEFINE_ARRAY ||
                 expression->opcode == RSCACHE_CS2_OP_PUSH_ARRAY_INT ||
                 expression->opcode == RSCACHE_CS2_OP_POP_ARRAY_INT) )
                touches_arrays = true;
        }
        /*
         * Arrays are excluded for now, and the reason is a real ambiguity
         * rather than caution: at this revision a handle rides a *string local*
         * and is passed to a proc as an ordinary string argument, while the
         * IR's array model dates from a numbered-global bank. Until a VM test
         * pins which one the bytecode means, a callee that touches an array is
         * left alone. Nothing else in the pass depends on that answer.
         */
        if( !small_enough || has_own_call || touches_arrays ||
            !cs2_inline_shape_is_supported(call, callee) ||
            cs2_count_insns(opt->function) + callee->instructions.count >
                opt->options->inline_max_insns )
        {
            RSCache_CS2_FunctionSetFree(&callee_fs);
            continue;
        }
        if( cs2_inline_one(opt, at, call, &callee_fs, callee) )
            changed = true;
        RSCache_CS2_FunctionSetFree(&callee_fs);
        if( opt->failed )
            return changed;
    }
    return changed;
}

/* -------------------------------------------------------------------------
 * Tail recursion
 * ---------------------------------------------------------------------- */

/**
 * Turn a tail call into a jump back to the top.
 *
 * Two shapes qualify and both appear in this cache. `return(~self(a, b))` is
 * the one that reads like tail recursion; `~self(a, b); return;` is the one a
 * void proc actually compiles to, and it is the common case — script 2621, the
 * spellbook's quicksort, ends in exactly that and recurses to depth 70 sorting
 * the standard book.
 *
 * The transformation is sound because the frame being replaced is dead the
 * moment the call is made: nothing in it is read after the callee returns. What
 * it is *not* is free, and the reason is the frame itself. A `gosub` hands the
 * callee a frame of zeroes; jumping back to the top hands it whatever the
 * previous iteration left in those slots. So every local the body reads before
 * writing has to be re-zeroed, which is what `RSCache_CS2_CfgLiveAtEntry`
 * answers — precisely, rather than by zeroing all of them.
 */
static bool
cs2_pass_tail_recursion(struct cs2_opt* opt)
{
    assert(opt);
    struct RSCache_CS2_Function* function = opt->function;
    struct RSCache_CS2_Arena* arena = &opt->fs->arena;

    /* Nothing to do for a function that never calls itself, and finding that
     * out is cheaper than the analysis below. */
    bool self_calling = false;
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; at = at->next )
    {
        struct RSCache_CS2_Expr* call = cs2_find_proc(at->expression);
        if( call && call->script_id == function->id )
            self_calling = true;
    }
    if( !self_calling )
        return false;

    struct RSCache_CS2_Cfg cfg;
    RSCache_CS2_CfgBuild(&cfg, function);
    int spare_int = cfg.slots.int_count;
    int spare_string = cfg.slots.string_count;

    /*
     * Which slots a reused frame has to clear, and which are parameters.
     *
     * A parameter is about to be overwritten by the argument binding, so it
     * needs no zeroing however it is read; everything else that is live at
     * entry does.
     */
    size_t map_bytes = (size_t)(cfg.slots.total > 0 ? cfg.slots.total : 1) * sizeof(bool);
    bool* is_parameter = (bool*)calloc(1, map_bytes);
    assert(is_parameter);
    /* Array handles share the string bank, and zeroing one destroys the array
     * rather than resetting a string. They are tracked so they can be left
     * alone. */
    bool* is_array_slot = (bool*)calloc(1, map_bytes);
    assert(is_array_slot);
    for( int i = 0; i < function->arguments.count; i++ )
    {
        struct RSCache_CS2_Variable* parameter =
            (struct RSCache_CS2_Variable*)function->arguments.items[i];
        int slot = RSCache_CS2_CfgSlotOf(&cfg, parameter);
        if( slot < 0 )
            continue;
        is_parameter[slot] = true;
        if( parameter->kind == RSCACHE_CS2_VAR_ARRAY )
            is_array_slot[slot] = true;
    }
    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; at = at->next )
    {
        struct RSCache_CS2_Expr* expression = at->expression;
        if( !expression || expression->kind != RSCACHE_CS2_EXPR_OPERATION )
            continue;
        if( expression->opcode != RSCACHE_CS2_OP_DEFINE_ARRAY &&
            expression->opcode != RSCACHE_CS2_OP_PUSH_ARRAY_INT &&
            expression->opcode != RSCACHE_CS2_OP_POP_ARRAY_INT )
            continue;
        struct RSCache_CS2_Expr* one = NULL;
        struct RSCache_CS2_Expr** items = NULL;
        int count = 0;
        RSCache_CS2_ExprAsList(expression->arguments, &items, &count, &one);
        if( count == 0 || !items[0] )
            continue;
        int slot = RSCache_CS2_CfgSlotOf(&cfg, items[0]->variable);
        if( slot >= 0 )
            is_array_slot[slot] = true;
    }

    bool changed = false;
    struct RSCache_CS2_Insn* entry = NULL;

    for( struct RSCache_CS2_Insn* at = function->instructions.first; at; )
    {
        struct RSCache_CS2_Insn* next = at->next;
        struct RSCache_CS2_Expr* call = NULL;
        struct RSCache_CS2_Insn* tail_return = NULL;

        if( at->kind == RSCACHE_CS2_INSN_RETURN && at->expression &&
            at->expression->kind == RSCACHE_CS2_EXPR_PROC &&
            at->expression->script_id == function->id )
        {
            /* `return(~self(...))` — the call *is* the returned value. */
            call = at->expression;
            tail_return = at;
        }
        else if( at->kind == RSCACHE_CS2_INSN_ASSIGNMENT && at->expression &&
                 at->expression->kind == RSCACHE_CS2_EXPR_PROC &&
                 at->expression->script_id == function->id &&
                 RSCache_CS2_ExprStackTypeCount(at->definitions) == 0 )
        {
            /* `~self(...); return;` — a statement, then a void return. Labels
             * in between are harmless: control still reaches the return from
             * here, and whatever else jumps to that label is unaffected. */
            struct RSCache_CS2_Insn* after = at->next;
            while( after && after->kind == RSCACHE_CS2_INSN_LABEL )
                after = after->next;
            if( after && after->kind == RSCACHE_CS2_INSN_RETURN &&
                RSCache_CS2_ExprStackTypeCount(after->expression) == 0 &&
                function->return_type_count == 0 )
            {
                call = at->expression;
                tail_return = at;
            }
        }

        if( !call )
        {
            at = next;
            continue;
        }

        struct RSCache_CS2_Expr* single = NULL;
        struct RSCache_CS2_Expr** args = NULL;
        int arg_count = 0;
        RSCache_CS2_ExprAsList(call->arguments, &args, &arg_count, &single);
        if( arg_count != function->arguments.count )
        {
            at = next;
            continue;
        }
        /*
         * The bank is the *parameter's*, not the argument expression's.
         *
         * `RSCache_CS2_VarStackType` calls an array int-typed — true of its
         * elements, false of the handle, which rides a string local. Reading
         * the bank off the argument therefore binds an array parameter through
         * an int temporary, and script 2621's sort came back as a program that
         * pops a string where it wanted an int.
         *
         * An array parameter is not rebound at all. It cannot be: the handle is
         * not a value the IR can assign, only an operand a `define_array` or an
         * array access names. What makes that correct rather than a shortcut is
         * that a recursive sort passes the *same* array down every level, so
         * the binding would be `$arrayN = $arrayN`. Anything else is refused.
         */
        bool shaped = true;
        for( int i = 0; i < arg_count && shaped; i++ )
        {
            struct RSCache_CS2_Variable* parameter =
                (struct RSCache_CS2_Variable*)function->arguments.items[i];
            if( parameter->kind == RSCACHE_CS2_VAR_ARRAY )
            {
                struct RSCache_CS2_Expr* passed = args[i];
                if( !passed || !passed->variable ||
                    passed->variable->kind != RSCACHE_CS2_VAR_ARRAY ||
                    passed->variable->id != parameter->id )
                    shaped = false;
                continue;
            }
            if( RSCache_CS2_ExprStackTypeCount(args[i]) != 1 )
                shaped = false;
        }
        /* A local array the body reads before defining would need a handle a
         * reused frame no longer has, and there is nothing to put back. */
        for( int slot = 0; slot < cfg.slots.total && shaped; slot++ )
        {
            if( is_parameter[slot] || !is_array_slot[slot] )
                continue;
            if( RSCache_CS2_CfgLiveAtEntry(&cfg, slot) )
                shaped = false;
        }
        if( !shaped )
        {
            at = next;
            continue;
        }

        if( !entry )
        {
            entry = RSCache_CS2_InsnLabel(arena, -2);
            RSCache_CS2_ChainInsertBefore(&function->instructions, entry,
                                          function->instructions.first);
        }

        /*
         * Every argument is evaluated into a temporary before any parameter is
         * written. `~self($b, $a)` swaps its arguments, and assigning them one
         * at a time would feed the second assignment the value the first just
         * overwrote.
         */
        int next_int = 0;
        int next_string = 0;
        for( int i = 0; i < arg_count; i++ )
        {
            struct RSCache_CS2_Variable* parameter =
                (struct RSCache_CS2_Variable*)function->arguments.items[i];
            if( parameter->kind == RSCACHE_CS2_VAR_ARRAY )
                continue; /* Same array every level; nothing to move. */
            bool is_string = parameter->kind == RSCACHE_CS2_VAR_STRING;
            struct RSCache_CS2_Variable* temporary = RSCache_CS2_VarIntern(
                opt->fs, is_string ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
                function->id,
                is_string ? spare_string + next_string : spare_int + next_int);
            RSCache_CS2_ChainInsertBefore(
                &function->instructions,
                RSCache_CS2_InsnAssignment(
                    arena, RSCache_CS2_ExprAccess(arena, temporary, NULL), args[i]),
                tail_return);
            if( is_string )
                next_string++;
            else
                next_int++;
        }

        /* Clear what a fresh frame would have zeroed, before the parameters are
         * bound — a parameter is excluded above, so the order is free, and this
         * way the zeroing cannot clobber a binding. */
        for( int slot = 0; slot < cfg.slots.total; slot++ )
        {
            if( is_parameter[slot] || is_array_slot[slot] ||
                !RSCache_CS2_CfgLiveAtEntry(&cfg, slot) )
                continue;
            bool is_string = slot >= cfg.slots.int_count;
            struct RSCache_CS2_Variable* local = RSCache_CS2_VarIntern(
                opt->fs, is_string ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
                function->id, is_string ? slot - cfg.slots.int_count : slot);
            struct RSCache_CS2_Expr* zero =
                is_string ? RSCache_CS2_ExprConstantString(arena, "")
                          : RSCache_CS2_ExprConstantInt(arena, 0);
            RSCache_CS2_ChainInsertBefore(
                &function->instructions,
                RSCache_CS2_InsnAssignment(
                    arena, RSCache_CS2_ExprAccess(arena, local, NULL), zero),
                tail_return);
        }

        next_int = 0;
        next_string = 0;
        for( int i = 0; i < arg_count; i++ )
        {
            struct RSCache_CS2_Variable* declared =
                (struct RSCache_CS2_Variable*)function->arguments.items[i];
            if( declared->kind == RSCACHE_CS2_VAR_ARRAY )
                continue;
            bool is_string = declared->kind == RSCACHE_CS2_VAR_STRING;
            struct RSCache_CS2_Variable* temporary = RSCache_CS2_VarIntern(
                opt->fs, is_string ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
                function->id,
                is_string ? spare_string + next_string : spare_int + next_int);
            struct RSCache_CS2_Variable* parameter = RSCache_CS2_VarIntern(
                opt->fs, is_string ? RSCACHE_CS2_VAR_STRING : RSCACHE_CS2_VAR_INT,
                function->id, is_string ? next_string : next_int);
            RSCache_CS2_ChainInsertBefore(
                &function->instructions,
                RSCache_CS2_InsnAssignment(
                    arena, RSCache_CS2_ExprAccess(arena, parameter, NULL),
                    RSCache_CS2_ExprAccess(arena, temporary, NULL)),
                tail_return);
            if( is_string )
                next_string++;
            else
                next_int++;
        }

        /* The call site becomes the jump. For the statement form the `return`
         * that followed it stays where it is, unreachable from here and still
         * reachable from wherever else falls into it. */
        tail_return->kind = RSCACHE_CS2_INSN_GOTO;
        tail_return->label = entry;
        tail_return->expression = NULL;
        tail_return->definitions = NULL;
        opt->stats->tail_calls_looped++;
        changed = true;
        at = next;
    }

    free(is_parameter);
    free(is_array_slot);
    RSCache_CS2_CfgFree(&cfg);
    return changed;
}

/* -------------------------------------------------------------------------
 * Driver
 * ---------------------------------------------------------------------- */

bool
RSCache_CS2_Optimize(
    struct RSCache_CS2_FunctionSet* fs,
    struct RSCache_CS2_Function* function,
    const struct RSCache_CS2_OptOptions* options,
    struct RSCache_CS2_OptStats* stats,
    char* error,
    int error_capacity)
{
    assert(fs);
    assert(function);
    assert(options);
    assert(stats);

    memset(stats, 0, sizeof(*stats));
    if( error && error_capacity > 0 )
        error[0] = '\0';
    if( options->level <= RSCACHE_CS2_OPT_NONE )
        return true;

    struct cs2_opt opt;
    memset(&opt, 0, sizeof(opt));
    opt.fs = fs;
    opt.function = function;
    opt.options = options;
    opt.stats = stats;
    opt.error = error;
    opt.error_capacity = error_capacity;

    /*
     * `CS2_OPT_PASSES` is a bisection aid, not a feature: a bitmask of which
     * local passes run (1 fold, 2 jumps, 4 unreachable, 8 dead stores). A pass
     * that miscompiles one script in nine thousand is found by turning them off
     * one at a time, and rebuilding the tool four times to do that is slower
     * than reading an environment variable once.
     */
    static int pass_mask = -1;
    if( pass_mask < 0 )
    {
        const char* text = getenv("CS2_OPT_PASSES");
        pass_mask = text ? atoi(text) : 15;
    }

    /* The local passes run to a fixpoint: folding exposes a dead branch, which
     * makes a block unreachable, which makes a store dead, which can make
     * another value constant. Bounded because each round must remove something
     * to continue. */
    for( int round = 0; round < 8 && !opt.failed; round++ )
    {
        int before = stats->constants_folded + stats->branches_folded +
                     stats->instructions_removed + stats->constants_propagated;
        if( pass_mask & 1 )
            cs2_pass_fold(&opt);
        if( pass_mask & 2 )
            cs2_pass_clean_jumps(&opt);
        if( pass_mask & 4 )
            cs2_pass_drop_unreachable(&opt);
        if( pass_mask & 8 )
            cs2_pass_dead_stores(&opt);
        int after = stats->constants_folded + stats->branches_folded +
                    stats->instructions_removed + stats->constants_propagated;
        if( after == before )
            break;
    }
    if( opt.failed )
        return false;

    if( options->level < RSCACHE_CS2_OPT_FULL )
        return true;

    if( cs2_pass_tail_recursion(&opt) )
    {
        cs2_pass_fold(&opt);
        cs2_pass_clean_jumps(&opt);
    }

    for( int round = 0; round < options->inline_rounds && !opt.failed; round++ )
    {
        if( !cs2_pass_inline(&opt) )
            break;
        /* The point of inlining is what the local passes can then see: a
         * callee's parameter is now a constant the caller supplied. */
        for( int inner = 0; inner < 4; inner++ )
        {
            int before = stats->constants_folded + stats->branches_folded +
                         stats->instructions_removed;
            if( pass_mask & 1 )
                cs2_pass_fold(&opt);
            if( pass_mask & 2 )
                cs2_pass_clean_jumps(&opt);
            if( pass_mask & 4 )
                cs2_pass_drop_unreachable(&opt);
            if( pass_mask & 8 )
                cs2_pass_dead_stores(&opt);
            if( stats->constants_folded + stats->branches_folded +
                    stats->instructions_removed ==
                before )
                break;
        }
    }
    return !opt.failed;
}
