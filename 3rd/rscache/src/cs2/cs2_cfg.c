#include "cs2_cfg.h"

#include "cs2_command.h"
#include "cs2_support.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Bit sets
 * ---------------------------------------------------------------------- */

static bool
cs2_bit_get(const unsigned char* bits, int index)
{
    assert(bits);
    assert(index >= 0);
    return (bits[index >> 3] & (1u << (index & 7))) != 0;
}

static void
cs2_bit_set(unsigned char* bits, int index)
{
    assert(bits);
    assert(index >= 0);
    bits[index >> 3] |= (unsigned char)(1u << (index & 7));
}

/** `into |= from`; returns true when a bit was added. */
static bool
cs2_bit_or(unsigned char* into, const unsigned char* from, int bytes)
{
    assert(into);
    assert(from);
    bool changed = false;
    for( int i = 0; i < bytes; i++ )
    {
        unsigned char merged = (unsigned char)(into[i] | from[i]);
        if( merged == into[i] )
            continue;
        into[i] = merged;
        changed = true;
    }
    return changed;
}

/* -------------------------------------------------------------------------
 * Slots
 * ---------------------------------------------------------------------- */

int
RSCache_CS2_CfgSlotOf(
    const struct RSCache_CS2_Cfg* cfg,
    const struct RSCache_CS2_Variable* variable)
{
    assert(cfg);
    if( !variable )
        return -1;
    switch( variable->kind )
    {
    case RSCACHE_CS2_VAR_INT:
        return variable->id < cfg->slots.int_count ? variable->id : -1;
    case RSCACHE_CS2_VAR_STRING:
    case RSCACHE_CS2_VAR_ARRAY:
        /* An array handle occupies a string local, so the two share a bank and
         * writing the handle kills whatever string was in that slot. */
        return variable->id < cfg->slots.string_count ? cfg->slots.int_count + variable->id
                                                      : -1;
    default:
        return -1;
    }
}

static void
cs2_slots_note(struct RSCache_CS2_Slots* slots, const struct RSCache_CS2_Variable* variable)
{
    assert(slots);
    if( !variable )
        return;
    if( variable->kind == RSCACHE_CS2_VAR_INT && variable->id + 1 > slots->int_count )
        slots->int_count = variable->id + 1;
    else if( (variable->kind == RSCACHE_CS2_VAR_STRING ||
              variable->kind == RSCACHE_CS2_VAR_ARRAY) &&
             variable->id + 1 > slots->string_count )
        slots->string_count = variable->id + 1;
}

/** Walk every variable an expression mentions. */
static void
cs2_walk_expr_variables(
    struct RSCache_CS2_Expr* expr,
    void (*visit)(void* user, struct RSCache_CS2_Variable* variable),
    void* user)
{
    if( !expr )
        return;
    if( expr->variable )
        visit(user, expr->variable);
    struct RSCache_CS2_Expr* single = NULL;
    struct RSCache_CS2_Expr** items = NULL;
    int count = 0;
    if( expr->kind == RSCACHE_CS2_EXPR_COMPOUND )
    {
        RSCache_CS2_ExprAsList(expr, &items, &count, &single);
        for( int i = 0; i < count; i++ )
            cs2_walk_expr_variables(items[i], visit, user);
        return;
    }
    cs2_walk_expr_variables(expr->arguments, visit, user);
    cs2_walk_expr_variables(expr->triggers, visit, user);
    cs2_walk_expr_variables(expr->component, visit, user);
    cs2_walk_expr_variables(expr->pointer_source, visit, user);
}

static void
cs2_slot_visit(void* user, struct RSCache_CS2_Variable* variable)
{
    cs2_slots_note((struct RSCache_CS2_Slots*)user, variable);
}

/* -------------------------------------------------------------------------
 * Blocks
 * ---------------------------------------------------------------------- */

/** True for an instruction after which control does not simply fall through. */
static bool
cs2_ends_block(const struct RSCache_CS2_Insn* insn)
{
    assert(insn);
    return insn->kind == RSCACHE_CS2_INSN_BRANCH || insn->kind == RSCACHE_CS2_INSN_GOTO ||
           insn->kind == RSCACHE_CS2_INSN_SWITCH || insn->kind == RSCACHE_CS2_INSN_RETURN;
}

static void
cs2_push_edge(int** list, int* count, int value)
{
    assert(list);
    assert(count);
    for( int i = 0; i < *count; i++ )
    {
        if( (*list)[i] == value )
            return;
    }
    int* grown = (int*)realloc(*list, (size_t)(*count + 1) * sizeof(int));
    assert(grown);
    grown[*count] = value;
    *list = grown;
    *count += 1;
}

int
RSCache_CS2_CfgBlockOf(const struct RSCache_CS2_Cfg* cfg, const struct RSCache_CS2_Insn* insn)
{
    assert(cfg);
    if( !insn )
        return -1;
    for( int i = 0; i < cfg->block_count; i++ )
    {
        for( struct RSCache_CS2_Insn* at = cfg->blocks[i].first;; at = at->next )
        {
            if( at == insn )
                return i;
            if( at == cfg->blocks[i].last )
                break;
        }
    }
    return -1;
}

/* Instructions carrying a read of a slot that has not been written in this
 * block yet contribute to `use`; a definition contributes to `def`. */
struct cs2_liveness_visit
{
    struct RSCache_CS2_Cfg* cfg;
    unsigned char* use;
    unsigned char* def;
};

static void
cs2_use_visit(void* user, struct RSCache_CS2_Variable* variable)
{
    struct cs2_liveness_visit* visit = (struct cs2_liveness_visit*)user;
    int slot = RSCache_CS2_CfgSlotOf(visit->cfg, variable);
    if( slot < 0 )
        return;
    if( cs2_bit_get(visit->def, slot) )
        return;
    cs2_bit_set(visit->use, slot);
}

void
RSCache_CS2_CfgBuild(struct RSCache_CS2_Cfg* cfg, struct RSCache_CS2_Function* function)
{
    assert(cfg);
    assert(function);
    memset(cfg, 0, sizeof(*cfg));
    cfg->function = function;

    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn; insn = insn->next )
    {
        cs2_walk_expr_variables(insn->definitions, cs2_slot_visit, &cfg->slots);
        cs2_walk_expr_variables(insn->expression, cs2_slot_visit, &cfg->slots);
    }
    for( int i = 0; i < function->arguments.count; i++ )
        cs2_slots_note(&cfg->slots,
                       (struct RSCache_CS2_Variable*)function->arguments.items[i]);
    cfg->slots.total = cfg->slots.int_count + cfg->slots.string_count;
    cfg->bitset_bytes = (cfg->slots.total + 7) / 8;
    if( cfg->bitset_bytes == 0 )
        cfg->bitset_bytes = 1;

    /*
     * A block starts at the first instruction, at every label, and after every
     * instruction that ends one. Labels are their own leaders rather than being
     * folded into the previous block, which keeps a jump's destination equal to
     * a block start and makes the edge list a lookup instead of a search.
     */
    int capacity = 8;
    cfg->blocks = (struct RSCache_CS2_Block*)calloc((size_t)capacity, sizeof(*cfg->blocks));
    assert(cfg->blocks);
    struct RSCache_CS2_Insn* start = NULL;
    for( struct RSCache_CS2_Insn* insn = function->instructions.first; insn; insn = insn->next )
    {
        bool leader = !start || insn->kind == RSCACHE_CS2_INSN_LABEL;
        if( leader && start )
        {
            /* Close the open block before this label. */
            if( cfg->block_count == capacity )
            {
                capacity *= 2;
                cfg->blocks = (struct RSCache_CS2_Block*)realloc(
                    cfg->blocks, (size_t)capacity * sizeof(*cfg->blocks));
                assert(cfg->blocks);
                memset(cfg->blocks + cfg->block_count, 0,
                       (size_t)(capacity - cfg->block_count) * sizeof(*cfg->blocks));
            }
            cfg->blocks[cfg->block_count].index = cfg->block_count;
            cfg->blocks[cfg->block_count].first = start;
            cfg->blocks[cfg->block_count].last = insn->prev;
            cfg->block_count++;
        }
        if( leader )
            start = insn;
        if( !cs2_ends_block(insn) )
            continue;
        if( cfg->block_count == capacity )
        {
            capacity *= 2;
            cfg->blocks = (struct RSCache_CS2_Block*)realloc(
                cfg->blocks, (size_t)capacity * sizeof(*cfg->blocks));
            assert(cfg->blocks);
            memset(cfg->blocks + cfg->block_count, 0,
                   (size_t)(capacity - cfg->block_count) * sizeof(*cfg->blocks));
        }
        cfg->blocks[cfg->block_count].index = cfg->block_count;
        cfg->blocks[cfg->block_count].first = start;
        cfg->blocks[cfg->block_count].last = insn;
        cfg->block_count++;
        start = NULL;
    }
    if( start )
    {
        if( cfg->block_count == capacity )
        {
            capacity *= 2;
            cfg->blocks = (struct RSCache_CS2_Block*)realloc(
                cfg->blocks, (size_t)capacity * sizeof(*cfg->blocks));
            assert(cfg->blocks);
            memset(cfg->blocks + cfg->block_count, 0,
                   (size_t)(capacity - cfg->block_count) * sizeof(*cfg->blocks));
        }
        cfg->blocks[cfg->block_count].index = cfg->block_count;
        cfg->blocks[cfg->block_count].first = start;
        cfg->blocks[cfg->block_count].last = function->instructions.last;
        cfg->block_count++;
    }

    /* Edges. A label's block index is found by matching the label instruction
     * against block starts, which is what making labels leaders bought. */
    for( int i = 0; i < cfg->block_count; i++ )
    {
        struct RSCache_CS2_Insn* last = cfg->blocks[i].last;
        if( !last )
            continue;
        bool falls_through = last->kind != RSCACHE_CS2_INSN_GOTO &&
                             last->kind != RSCACHE_CS2_INSN_RETURN;
        struct RSCache_CS2_Insn* jumps[2] = { NULL, NULL };
        if( last->kind == RSCACHE_CS2_INSN_BRANCH )
            jumps[0] = last->pass;
        else if( last->kind == RSCACHE_CS2_INSN_GOTO )
            jumps[0] = last->label;
        for( int j = 0; j < 2; j++ )
        {
            if( !jumps[j] )
                continue;
            for( int k = 0; k < cfg->block_count; k++ )
            {
                if( cfg->blocks[k].first != jumps[j] )
                    continue;
                cs2_push_edge(&cfg->blocks[i].successors, &cfg->blocks[i].successor_count, k);
                cs2_push_edge(&cfg->blocks[k].predecessors,
                              &cfg->blocks[k].predecessor_count, i);
                break;
            }
        }
        if( last->kind == RSCACHE_CS2_INSN_SWITCH )
        {
            for( int c = 0; c < last->case_count; c++ )
            {
                for( int k = 0; k < cfg->block_count; k++ )
                {
                    if( cfg->blocks[k].first != last->case_labels[c] )
                        continue;
                    cs2_push_edge(&cfg->blocks[i].successors,
                                  &cfg->blocks[i].successor_count, k);
                    cs2_push_edge(&cfg->blocks[k].predecessors,
                                  &cfg->blocks[k].predecessor_count, i);
                    break;
                }
            }
        }
        if( falls_through && i + 1 < cfg->block_count )
        {
            cs2_push_edge(&cfg->blocks[i].successors, &cfg->blocks[i].successor_count, i + 1);
            cs2_push_edge(&cfg->blocks[i + 1].predecessors,
                          &cfg->blocks[i + 1].predecessor_count, i);
        }
    }

    /* Reachability from the entry block, by worklist. */
    if( cfg->block_count > 0 )
    {
        int* stack = (int*)malloc((size_t)cfg->block_count * sizeof(int));
        assert(stack);
        int top = 0;
        stack[top++] = 0;
        cfg->blocks[0].reachable = true;
        while( top > 0 )
        {
            int at = stack[--top];
            for( int i = 0; i < cfg->blocks[at].successor_count; i++ )
            {
                int next = cfg->blocks[at].successors[i];
                if( cfg->blocks[next].reachable )
                    continue;
                cfg->blocks[next].reachable = true;
                stack[top++] = next;
            }
        }
        free(stack);
    }

    /*
     * Back edges, by the cheap test: an edge to a *lower-numbered* block.
     *
     * Blocks are numbered in chain order and the chain is in the order the
     * bytecode ran, so a jump backwards is a loop. That is not the textbook
     * definition — which needs dominance — and it can name an edge that is not
     * a natural loop's latch. The only consumer is the unroller, which then
     * checks the loop's shape in full before touching it, so a false positive
     * costs a rejected candidate rather than a wrong transformation.
     */
    for( int i = 0; i < cfg->block_count; i++ )
    {
        for( int j = 0; j < cfg->blocks[i].successor_count; j++ )
        {
            int target = cfg->blocks[i].successors[j];
            if( target > i )
                continue;
            cfg->loop_headers =
                (int*)realloc(cfg->loop_headers, (size_t)(cfg->loop_count + 1) * sizeof(int));
            assert(cfg->loop_headers);
            cfg->loop_latches =
                (int*)realloc(cfg->loop_latches, (size_t)(cfg->loop_count + 1) * sizeof(int));
            assert(cfg->loop_latches);
            cfg->loop_headers[cfg->loop_count] = target;
            cfg->loop_latches[cfg->loop_count] = i;
            cfg->loop_count++;
        }
    }

    /* Per-block use/def, then liveness to a fixpoint. */
    size_t vector = (size_t)cfg->block_count * (size_t)cfg->bitset_bytes;
    if( vector == 0 )
        vector = (size_t)cfg->bitset_bytes;
    cfg->use = (unsigned char*)calloc(vector, 1);
    cfg->def = (unsigned char*)calloc(vector, 1);
    cfg->live_in = (unsigned char*)calloc(vector, 1);
    cfg->live_out = (unsigned char*)calloc(vector, 1);
    assert(cfg->use && cfg->def && cfg->live_in && cfg->live_out);

    for( int i = 0; i < cfg->block_count; i++ )
    {
        unsigned char* use = cfg->use + (size_t)i * cfg->bitset_bytes;
        unsigned char* def = cfg->def + (size_t)i * cfg->bitset_bytes;
        struct cs2_liveness_visit visit = { cfg, use, def };
        for( struct RSCache_CS2_Insn* insn = cfg->blocks[i].first;; insn = insn->next )
        {
            /* Reads first: the right-hand side is evaluated before the store,
             * so `$x = calc($x + 1)` both uses and defines the slot. */
            cs2_walk_expr_variables(insn->expression, cs2_use_visit, &visit);
            if( insn->kind == RSCACHE_CS2_INSN_ASSIGNMENT )
            {
                struct RSCache_CS2_Expr* single = NULL;
                struct RSCache_CS2_Expr** targets = NULL;
                int count = 0;
                RSCache_CS2_ExprAsList(insn->definitions, &targets, &count, &single);
                for( int t = 0; t < count; t++ )
                {
                    if( !targets[t] || targets[t]->kind != RSCACHE_CS2_EXPR_ACCESS )
                        continue;
                    int slot = RSCache_CS2_CfgSlotOf(cfg, targets[t]->variable);
                    if( slot >= 0 )
                        cs2_bit_set(def, slot);
                }
            }
            if( insn == cfg->blocks[i].last )
                break;
        }
    }

    bool changed = true;
    while( changed )
    {
        changed = false;
        for( int i = cfg->block_count - 1; i >= 0; i-- )
        {
            unsigned char* out = cfg->live_out + (size_t)i * cfg->bitset_bytes;
            for( int s = 0; s < cfg->blocks[i].successor_count; s++ )
            {
                const unsigned char* in =
                    cfg->live_in +
                    (size_t)cfg->blocks[i].successors[s] * cfg->bitset_bytes;
                if( cs2_bit_or(out, in, cfg->bitset_bytes) )
                    changed = true;
            }
            /* in = use | (out & ~def) */
            unsigned char* in = cfg->live_in + (size_t)i * cfg->bitset_bytes;
            const unsigned char* use = cfg->use + (size_t)i * cfg->bitset_bytes;
            const unsigned char* def = cfg->def + (size_t)i * cfg->bitset_bytes;
            for( int b = 0; b < cfg->bitset_bytes; b++ )
            {
                unsigned char want = (unsigned char)(use[b] | (out[b] & (unsigned char)~def[b]));
                unsigned char merged = (unsigned char)(in[b] | want);
                if( merged == in[b] )
                    continue;
                in[b] = merged;
                changed = true;
            }
        }
    }
}

void
RSCache_CS2_CfgFree(struct RSCache_CS2_Cfg* cfg)
{
    if( !cfg )
        return;
    for( int i = 0; i < cfg->block_count; i++ )
    {
        free(cfg->blocks[i].successors);
        free(cfg->blocks[i].predecessors);
    }
    free(cfg->blocks);
    free(cfg->use);
    free(cfg->def);
    free(cfg->live_in);
    free(cfg->live_out);
    free(cfg->loop_headers);
    free(cfg->loop_latches);
    memset(cfg, 0, sizeof(*cfg));
}

bool
RSCache_CS2_CfgLiveAtEntry(const struct RSCache_CS2_Cfg* cfg, int slot)
{
    assert(cfg);
    if( cfg->block_count <= 0 || slot < 0 || slot >= cfg->slots.total )
        return true; /* Unknown means "assume it is read". */
    return cs2_bit_get(cfg->live_in, slot);
}

bool
RSCache_CS2_CfgLiveOut(const struct RSCache_CS2_Cfg* cfg, int block, int slot)
{
    assert(cfg);
    if( block < 0 || block >= cfg->block_count || slot < 0 || slot >= cfg->slots.total )
        return true; /* Unknown means "assume needed". */
    return cs2_bit_get(cfg->live_out + (size_t)block * cfg->bitset_bytes, slot);
}

struct cs2_reads_slot
{
    const struct RSCache_CS2_Cfg* cfg;
    int slot;
    bool found;
};

static void
cs2_reads_visit(void* user, struct RSCache_CS2_Variable* variable)
{
    struct cs2_reads_slot* probe = (struct cs2_reads_slot*)user;
    if( RSCache_CS2_CfgSlotOf(probe->cfg, variable) == probe->slot )
        probe->found = true;
}

bool
RSCache_CS2_CfgLiveAfter(
    const struct RSCache_CS2_Cfg* cfg,
    struct RSCache_CS2_Insn* insn,
    int slot)
{
    assert(cfg);
    assert(insn);
    if( slot < 0 )
        return true;
    int block = RSCache_CS2_CfgBlockOf(cfg, insn);
    if( block < 0 )
        return true;
    for( struct RSCache_CS2_Insn* at = insn->next; at; at = at->next )
    {
        struct cs2_reads_slot probe = { cfg, slot, false };
        cs2_walk_expr_variables(at->expression, cs2_reads_visit, &probe);
        if( probe.found )
            return true;
        /* A later store to the same slot, with nothing reading it in between,
         * makes this one dead regardless of what happens after. */
        if( at->kind == RSCACHE_CS2_INSN_ASSIGNMENT )
        {
            struct RSCache_CS2_Expr* single = NULL;
            struct RSCache_CS2_Expr** targets = NULL;
            int count = 0;
            RSCache_CS2_ExprAsList(at->definitions, &targets, &count, &single);
            bool overwritten = false;
            for( int t = 0; t < count; t++ )
            {
                if( !targets[t] || targets[t]->kind != RSCACHE_CS2_EXPR_ACCESS )
                    continue;
                if( RSCache_CS2_CfgSlotOf(cfg, targets[t]->variable) == slot )
                    overwritten = true;
            }
            if( overwritten )
                return false;
        }
        if( at == cfg->blocks[block].last )
            break;
    }
    return RSCache_CS2_CfgLiveOut(cfg, block, slot);
}
