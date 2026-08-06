#include "cs2_cfa.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Basic blocks
 * ---------------------------------------------------------------------- */

struct cs2_block
{
    int index;
    struct RSCache_CS2_Insn* head;
    struct RSCache_CS2_Insn* tail;

    struct RSCache_CS2_Vec preds;
    struct RSCache_CS2_Vec succs;

    /* Dominator tree. */
    struct cs2_block* idom;
    struct RSCache_CS2_Vec dom_children;
    int post_order;
    bool in_post_order;
};

struct cs2_flow
{
    struct RSCache_CS2_Arena* arena;
    struct RSCache_CS2_Function* function;

    struct cs2_block** blocks;
    int block_count;
    /* Insn* -> cs2_block* */
    struct RSCache_CS2_Map block_of;

    bool failed;
    char* error;
    int error_capacity;
};

static void
cs2_cfa_fail(struct cs2_flow* flow, const char* fmt, ...)
{
    if( flow->failed )
        return;
    flow->failed = true;
    if( !flow->error || flow->error_capacity <= 0 )
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(flow->error, (size_t)flow->error_capacity, fmt, args);
    va_end(args);
}

static struct cs2_block*
cs2_block_of(struct cs2_flow* flow, struct RSCache_CS2_Insn* insn)
{
    struct cs2_block* block = (struct cs2_block*)RSCache_CS2_MapGet(&flow->block_of, insn);
    if( !block )
        cs2_cfa_fail(flow, "branch target is not the head of any basic block");
    return block;
}

static struct cs2_block*
cs2_block_next(struct cs2_flow* flow, struct cs2_block* block)
{
    if( block->index + 1 >= flow->block_count )
    {
        cs2_cfa_fail(flow, "control falls off the end of the script");
        return NULL;
    }
    return flow->blocks[block->index + 1];
}

/**
 * Split the instruction list at every label and after every branch — the two
 * places control can enter or leave other than by falling through.
 */
static bool
cs2_partition_blocks(struct cs2_flow* flow)
{
    struct RSCache_CS2_Chain* chain = &flow->function->instructions;
    if( !chain->first )
    {
        cs2_cfa_fail(flow, "script has no instructions");
        return false;
    }

    struct RSCache_CS2_Vec heads;
    RSCache_CS2_VecInit(&heads);
    RSCache_CS2_VecPush(&heads, chain->first);

    for( struct RSCache_CS2_Insn* insn = chain->first; insn; insn = insn->next )
    {
        if( insn->kind == RSCACHE_CS2_INSN_LABEL )
        {
            if( heads.items[heads.count - 1] != insn )
                RSCache_CS2_VecPush(&heads, insn);
        }
        else if(
            insn->kind == RSCACHE_CS2_INSN_BRANCH || insn->kind == RSCACHE_CS2_INSN_SWITCH )
        {
            if( !insn->next )
            {
                cs2_cfa_fail(flow, "script ends on a branch");
                RSCache_CS2_VecFree(&heads);
                return false;
            }
            if( heads.items[heads.count - 1] == insn->next )
            {
                cs2_cfa_fail(flow, "two block boundaries landed on one instruction");
                RSCache_CS2_VecFree(&heads);
                return false;
            }
            RSCache_CS2_VecPush(&heads, insn->next);
        }
    }

    flow->block_count = heads.count;
    flow->blocks = (struct cs2_block**)RSCache_CS2_ArenaAlloc(
        flow->arena, (size_t)heads.count * sizeof(struct cs2_block*));

    for( int i = 0; i < heads.count; i++ )
    {
        struct cs2_block* block =
            (struct cs2_block*)RSCache_CS2_ArenaAlloc(flow->arena, sizeof(*block));
        block->index = i;
        block->head = (struct RSCache_CS2_Insn*)heads.items[i];
        block->tail = i + 1 < heads.count
                          ? ((struct RSCache_CS2_Insn*)heads.items[i + 1])->prev
                          : chain->last;
        block->post_order = -1;
        RSCache_CS2_VecInit(&block->preds);
        RSCache_CS2_VecInit(&block->succs);
        RSCache_CS2_VecInit(&block->dom_children);
        RSCache_CS2_ArenaTrackVec(flow->arena, &block->preds);
        RSCache_CS2_ArenaTrackVec(flow->arena, &block->succs);
        RSCache_CS2_ArenaTrackVec(flow->arena, &block->dom_children);
        flow->blocks[i] = block;

        for( struct RSCache_CS2_Insn* insn = block->head;; insn = insn->next )
        {
            RSCache_CS2_MapPut(&flow->block_of, insn, block);
            if( insn == block->tail || !insn->next )
                break;
        }
    }

    RSCache_CS2_VecFree(&heads);
    return true;
}

static void
cs2_add_edge(struct cs2_block* from, struct cs2_block* to)
{
    if( !from || !to )
        return;
    RSCache_CS2_VecPush(&from->succs, to);
    RSCache_CS2_VecPush(&to->preds, from);
}

static bool
cs2_graph_blocks(struct cs2_flow* flow)
{
    for( int i = 0; i < flow->block_count; i++ )
    {
        struct cs2_block* block = flow->blocks[i];
        struct RSCache_CS2_Insn* tail = block->tail;
        switch( tail->kind )
        {
        case RSCACHE_CS2_INSN_GOTO:
            cs2_add_edge(block, cs2_block_of(flow, tail->label));
            break;
        case RSCACHE_CS2_INSN_SWITCH:
            cs2_add_edge(block, cs2_block_next(flow, block));
            for( int j = 0; j < tail->case_count; j++ )
                cs2_add_edge(block, cs2_block_of(flow, tail->case_labels[j]));
            break;
        case RSCACHE_CS2_INSN_BRANCH:
            cs2_add_edge(block, cs2_block_of(flow, tail->pass));
            cs2_add_edge(block, cs2_block_next(flow, block));
            break;
        case RSCACHE_CS2_INSN_RETURN:
            break;
        default:
            cs2_add_edge(block, cs2_block_next(flow, block));
            break;
        }
        if( flow->failed )
            return false;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Dominator tree
 * ---------------------------------------------------------------------- */

/** Reverse post-order numbering, iteratively so deep scripts cannot overflow. */
static void
cs2_post_order(struct cs2_flow* flow, struct cs2_block** order, int* order_count)
{
    struct cs2_block** stack =
        (struct cs2_block**)calloc((size_t)flow->block_count, sizeof(*stack));
    int* next_succ = (int*)calloc((size_t)flow->block_count, sizeof(int));
    bool* seen = (bool*)calloc((size_t)flow->block_count, sizeof(bool));
    if( !stack || !next_succ || !seen )
    {
        free(stack);
        free(next_succ);
        free(seen);
        *order_count = 0;
        return;
    }

    int top = 0;
    stack[top] = flow->blocks[0];
    seen[0] = true;
    *order_count = 0;

    while( top >= 0 )
    {
        struct cs2_block* block = stack[top];
        if( next_succ[block->index] < block->succs.count )
        {
            struct cs2_block* succ =
                (struct cs2_block*)block->succs.items[next_succ[block->index]++];
            if( !seen[succ->index] )
            {
                seen[succ->index] = true;
                stack[++top] = succ;
            }
        }
        else
        {
            block->post_order = *order_count;
            block->in_post_order = true;
            order[(*order_count)++] = block;
            top--;
        }
    }

    free(stack);
    free(next_succ);
    free(seen);
}

static struct cs2_block*
cs2_dom_intersect(struct cs2_flow* flow, struct cs2_block* a, struct cs2_block* b)
{
    while( a != b )
    {
        while( a->post_order < b->post_order )
        {
            if( !a->idom )
            {
                cs2_cfa_fail(flow, "flow graph has no single entry point");
                return a;
            }
            a = a->idom;
        }
        while( b->post_order < a->post_order )
        {
            if( !b->idom )
            {
                cs2_cfa_fail(flow, "flow graph has no single entry point");
                return b;
            }
            b = b->idom;
        }
    }
    return a;
}

/* A Simple, Fast Dominance Algorithm — Cooper, Harvey, Kennedy, 2001. */
static bool
cs2_dominator_tree(struct cs2_flow* flow)
{
    struct cs2_block** order =
        (struct cs2_block**)calloc((size_t)flow->block_count, sizeof(*order));
    if( !order )
        return false;
    int order_count = 0;
    cs2_post_order(flow, order, &order_count);

    bool changed = true;
    while( changed && !flow->failed )
    {
        changed = false;
        /* Skip the entry (last in post-order) and walk the rest in reverse
         * post-order, so most predecessors are already settled. */
        for( int i = order_count - 2; i >= 0; i-- )
        {
            struct cs2_block* block = order[i];
            if( block->preds.count == 0 )
                continue;
            struct cs2_block* new_idom = (struct cs2_block*)block->preds.items[0];
            for( int j = 1; j < block->preds.count; j++ )
            {
                struct cs2_block* pred = (struct cs2_block*)block->preds.items[j];
                if( pred->idom || pred == order[order_count - 1] )
                    new_idom = cs2_dom_intersect(flow, pred, new_idom);
                if( flow->failed )
                {
                    free(order);
                    return false;
                }
            }
            if( block->idom != new_idom )
            {
                block->idom = new_idom;
                changed = true;
            }
        }
    }
    free(order);
    if( flow->failed )
        return false;

    for( int i = 0; i < flow->block_count; i++ )
    {
        struct cs2_block* block = flow->blocks[i];
        if( block->idom )
            RSCache_CS2_VecPush(&block->idom->dom_children, block);
    }
    return true;
}

/** Is `block` anywhere in `root`'s dominator subtree? */
static bool
cs2_dominates(struct cs2_flow* flow, struct cs2_block* root, struct cs2_block* block)
{
    struct cs2_block** stack =
        (struct cs2_block**)calloc((size_t)flow->block_count + 1, sizeof(*stack));
    if( !stack )
        return false;
    int top = 0;
    stack[top++] = root;
    bool found = false;
    while( top > 0 && !found )
    {
        struct cs2_block* current = stack[--top];
        for( int i = 0; i < current->dom_children.count; i++ )
        {
            struct cs2_block* child = (struct cs2_block*)current->dom_children.items[i];
            if( child == block )
            {
                found = true;
                break;
            }
            if( top < flow->block_count )
                stack[top++] = child;
        }
    }
    free(stack);
    return found;
}

/* -------------------------------------------------------------------------
 * Constructs
 * ---------------------------------------------------------------------- */

static struct RSCache_CS2_Construct*
cs2_construct_new(struct cs2_flow* flow, enum RSCache_CS2_ConstructKind kind)
{
    struct RSCache_CS2_Construct* construct =
        (struct RSCache_CS2_Construct*)RSCache_CS2_ArenaAlloc(flow->arena, sizeof(*construct));
    construct->kind = kind;
    RSCache_CS2_VecInit(&construct->instructions);
    RSCache_CS2_VecInit(&construct->branch_conditions);
    RSCache_CS2_VecInit(&construct->branch_bodies);
    RSCache_CS2_VecInit(&construct->case_keys);
    RSCache_CS2_VecInit(&construct->case_bodies);
    RSCache_CS2_ArenaTrackVec(flow->arena, &construct->instructions);
    RSCache_CS2_ArenaTrackVec(flow->arena, &construct->branch_conditions);
    RSCache_CS2_ArenaTrackVec(flow->arena, &construct->branch_bodies);
    RSCache_CS2_ArenaTrackVec(flow->arena, &construct->case_keys);
    RSCache_CS2_ArenaTrackVec(flow->arena, &construct->case_bodies);
    return construct;
}

static bool
cs2_construct_is_empty_seq(const struct RSCache_CS2_Construct* construct)
{
    return construct && construct->kind == RSCACHE_CS2_CONSTRUCT_SEQ &&
           construct->instructions.count == 0 && construct->next == NULL;
}

/**
 * True when this construct's last statement is a `return` that had an
 * unreachable `goto` after it in the bytecode — the marker that the branch it
 * ends was written with an `else`.
 *
 * Walks to the end of the construct chain and takes the last instruction of the
 * last sequence. A body ending in a nested `if` or `switch` answers false,
 * which is the safe direction: the flattened spelling is what this produced for
 * everything before the flag existed.
 */
static bool
cs2_construct_ends_in_else_marked_return(const struct RSCache_CS2_Construct* construct)
{
    const struct RSCache_CS2_Construct* last = NULL;
    for( const struct RSCache_CS2_Construct* c = construct; c; c = c->next )
        last = c;
    if( !last || last->kind != RSCACHE_CS2_CONSTRUCT_SEQ || last->instructions.count == 0 )
        return false;
    const struct RSCache_CS2_Insn* insn =
        (const struct RSCache_CS2_Insn*)last->instructions.items[last->instructions.count - 1];
    return insn && insn->kind == RSCACHE_CS2_INSN_RETURN && insn->dead_goto_follows;
}

/**
 * Emit `block` and everything it dominates into `prev`.
 *
 * Returns the block where control rejoins the enclosing region — the "what
 * comes after this if/switch" that the caller must go on to emit — or NULL when
 * this path ends (a return, or a merge the caller already owns).
 */
static struct cs2_block*
cs2_reconstruct_block(
    struct cs2_flow* flow,
    struct RSCache_CS2_Construct* prev,
    struct cs2_block* dominator,
    struct cs2_block* block)
{
    if( flow->failed || !block )
        return NULL;

    /* Reached from outside the region being built: it belongs to an enclosing
     * one, so hand it back rather than emitting it twice. */
    if( dominator != block && !cs2_dominates(flow, dominator, block) )
        return block;

    struct RSCache_CS2_Construct* seq;
    if( prev->kind == RSCACHE_CS2_CONSTRUCT_SEQ )
    {
        seq = prev;
    }
    else
    {
        seq = cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
        prev->next = seq;
    }

    for( struct RSCache_CS2_Insn* insn = block->head;; insn = insn->next )
    {
        if( insn != block->tail && insn->kind != RSCACHE_CS2_INSN_LABEL )
            RSCache_CS2_VecPush(&seq->instructions, insn);
        if( insn == block->tail )
            break;
    }

    struct RSCache_CS2_Insn* tail = block->tail;
    switch( tail->kind )
    {
    case RSCACHE_CS2_INSN_RETURN:
        RSCache_CS2_VecPush(&seq->instructions, tail);
        return NULL;

    case RSCACHE_CS2_INSN_GOTO:
        return cs2_reconstruct_block(flow, seq, dominator, cs2_block_of(flow, tail->label));

    case RSCACHE_CS2_INSN_SWITCH:
    {
        struct cs2_block* after = NULL;
        struct RSCache_CS2_Construct* construct =
            cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SWITCH);
        construct->expression = tail->expression;
        seq->next = construct;

        /* Cases sharing a target become one group, in first-seen order. */
        for( int i = 0; i < tail->case_count; i++ )
        {
            bool already_grouped = false;
            for( int j = 0; j < i; j++ )
                already_grouped =
                    already_grouped || tail->case_labels[j] == tail->case_labels[i];
            if( already_grouped )
                continue;

            struct RSCache_CS2_IntVec* keys = (struct RSCache_CS2_IntVec*)
                RSCache_CS2_ArenaAlloc(flow->arena, sizeof(*keys));
            RSCache_CS2_IntVecInit(keys);
            for( int j = 0; j < tail->case_count; j++ )
            {
                if( tail->case_labels[j] == tail->case_labels[i] )
                    RSCache_CS2_IntVecPush(keys, tail->case_keys[j]);
            }

            struct RSCache_CS2_Construct* body =
                cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
            RSCache_CS2_VecPush(&construct->case_keys, keys);
            RSCache_CS2_VecPush(&construct->case_bodies, body);

            struct cs2_block* target = cs2_block_of(flow, tail->case_labels[i]);
            if( flow->failed )
                return NULL;
            struct cs2_block* rejoin =
                cs2_reconstruct_block(flow, body, target, target);
            if( rejoin )
                after = rejoin;
        }

        struct cs2_block* fallthrough = cs2_block_next(flow, block);
        if( flow->failed )
            return NULL;
        struct RSCache_CS2_Construct* default_case =
            cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
        construct->default_case = default_case;
        struct cs2_block* rejoin =
            cs2_reconstruct_block(flow, default_case, fallthrough, fallthrough);
        if( rejoin )
            after = rejoin;
        if( cs2_construct_is_empty_seq(default_case) )
            construct->default_case = NULL;

        if( after )
            return cs2_reconstruct_block(flow, construct, block, after);
        return NULL;
    }

    case RSCACHE_CS2_INSN_BRANCH:
    {
        struct cs2_block* pass = cs2_block_of(flow, tail->pass);
        struct cs2_block* fail = cs2_block_next(flow, block);
        if( flow->failed )
            return NULL;

        /* A predecessor later in the listing means control comes back here:
         * that is a loop, not a forward branch. */
        bool is_loop = false;
        for( int i = 0; i < block->preds.count; i++ )
        {
            struct cs2_block* pred = (struct cs2_block*)block->preds.items[i];
            if( pred->index > block->index )
            {
                is_loop = true;
                break;
            }
        }

        if( is_loop )
        {
            struct RSCache_CS2_Construct* loop =
                cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_WHILE);
            loop->condition = tail->expression;
            seq->next = loop;
            loop->body = cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
            cs2_reconstruct_block(flow, loop->body, pass, pass);
            return cs2_reconstruct_block(flow, loop, fail, fail);
        }

        struct RSCache_CS2_Construct* branch =
            cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_IF);
        seq->next = branch;
        struct RSCache_CS2_Construct* body =
            cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
        RSCache_CS2_VecPush(&branch->branch_conditions, tail->expression);
        RSCache_CS2_VecPush(&branch->branch_bodies, body);

        struct cs2_block* after_if = cs2_reconstruct_block(flow, body, pass, pass);
        struct RSCache_CS2_Construct* otherwise =
            cs2_construct_new(flow, RSCACHE_CS2_CONSTRUCT_SEQ);
        branch->otherwise = otherwise;
        struct cs2_block* after_else = cs2_reconstruct_block(flow, otherwise, fail, fail);
        if( flow->failed )
            return NULL;

        /*
         * Is there an `else` at all?
         *
         * When the `if` side rejoins, yes — the two paths meet, so the second
         * one is an else. When it does not (the body returns), the else side is
         * just what follows, and printing it as an else adds a level for
         * nothing... except that the *source* it came from may have had one,
         * and the two compile differently. An `else` needs a jump past it from
         * the end of the `if` body; two separate statements do not. That jump
         * is unreachable, so nothing at run time can tell, and the dead-code
         * pass deletes it before this function ever sees the graph.
         *
         * `dead_goto_follows` is that jump, recorded on the `return` as it was
         * deleted. Reading it here is what keeps `if (a) { return; } else …`
         * and `if (a) { return; } …` distinct through a round trip, in both
         * directions — roughly 300 scripts had one spelling and got the other.
         */
        bool has_else = after_if || cs2_construct_ends_in_else_marked_return(body);

        if( otherwise->instructions.count == 0 && otherwise->next == NULL )
        {
            branch->otherwise = NULL;
        }
        else if(
            has_else && otherwise->instructions.count == 0 && otherwise->next &&
            otherwise->next->kind == RSCACHE_CS2_CONSTRUCT_IF &&
            otherwise->next->next == NULL )
        {
            /* An else whose whole body is another if becomes `else if`.
             *
             * EXCEPTIONS G2 records the symptom from the far end: source with
             * an `else if` chain that comes back as two separate `if`s. It is
             * this, and `has_else` is the fix. */
            struct RSCache_CS2_Construct* inner = otherwise->next;
            for( int i = 0; i < inner->branch_conditions.count; i++ )
            {
                RSCache_CS2_VecPush(
                    &branch->branch_conditions, inner->branch_conditions.items[i]);
                RSCache_CS2_VecPush(&branch->branch_bodies, inner->branch_bodies.items[i]);
            }
            branch->otherwise = inner->otherwise;
        }

        if( !after_if )
        {
            struct RSCache_CS2_Construct* tail_construct = branch;
            if( !has_else )
            {
                branch->next = branch->otherwise;
                branch->otherwise = NULL;
                tail_construct = branch->next ? branch->next : branch;
            }
            if( after_else )
                return cs2_reconstruct_block(flow, tail_construct, block, after_else);
            return NULL;
        }
        return cs2_reconstruct_block(flow, branch, block, after_if);
    }

    case RSCACHE_CS2_INSN_ASSIGNMENT:
        RSCache_CS2_VecPush(&seq->instructions, tail);
        return cs2_reconstruct_block(flow, seq, dominator, cs2_block_next(flow, block));

    case RSCACHE_CS2_INSN_LABEL:
    default:
        return cs2_reconstruct_block(flow, seq, dominator, cs2_block_next(flow, block));
    }
}

struct RSCache_CS2_Construct*
RSCache_CS2_Reconstruct(
    struct RSCache_CS2_Arena* arena,
    struct RSCache_CS2_Function* function,
    char* error,
    int error_capacity)
{
    struct cs2_flow flow;
    memset(&flow, 0, sizeof(flow));
    flow.arena = arena;
    flow.function = function;
    flow.error = error;
    flow.error_capacity = error_capacity;
    RSCache_CS2_MapInit(&flow.block_of);

    struct RSCache_CS2_Construct* root = NULL;
    if( cs2_partition_blocks(&flow) && cs2_graph_blocks(&flow) && cs2_dominator_tree(&flow) )
    {
        root = cs2_construct_new(&flow, RSCACHE_CS2_CONSTRUCT_SEQ);
        cs2_reconstruct_block(&flow, root, flow.blocks[0], flow.blocks[0]);
    }

    RSCache_CS2_MapFree(&flow.block_of);
    return flow.failed ? NULL : root;
}
