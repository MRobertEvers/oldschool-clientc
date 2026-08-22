#include "game/rs_clientop.h"

#include "cs2vm2/cs2_opcode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char const* const KIND_NAME[RS_CLIENTOP_KIND_COUNT] = {
    "npc", "loc", "obj", "player", "tile"
};

char const*
RS_ClientOpKindName(enum RS_ClientOpKind kind)
{
    if( kind < 0 || kind >= RS_CLIENTOP_KIND_COUNT )
        return "?";
    return KIND_NAME[kind];
}

/*
 * SET and DEL alternate from 6700 in kind order, which is exactly the layout
 * the opcode numbering states -- but it is written out rather than computed,
 * for the reason the highlight table is: a kind inserted in the middle would
 * shift every later pair and the arithmetic would go on returning a plausible
 * answer for the wrong one.
 */
struct RS_ClientOpOp
{
    int opcode;
    enum RS_ClientOpKind kind;
    bool is_set;
};

static struct RS_ClientOpOp const CLIENTOP_OPS[] = {
    { CS2_OP_CLIENTOP_NPC_SET, RS_CLIENTOP_NPC, true },
    { CS2_OP_CLIENTOP_NPC_DEL, RS_CLIENTOP_NPC, false },
    { CS2_OP_CLIENTOP_LOC_SET, RS_CLIENTOP_LOC, true },
    { CS2_OP_CLIENTOP_LOC_DEL, RS_CLIENTOP_LOC, false },
    { CS2_OP_CLIENTOP_OBJ_SET, RS_CLIENTOP_OBJ, true },
    { CS2_OP_CLIENTOP_OBJ_DEL, RS_CLIENTOP_OBJ, false },
    { CS2_OP_CLIENTOP_PLAYER_SET, RS_CLIENTOP_PLAYER, true },
    { CS2_OP_CLIENTOP_PLAYER_DEL, RS_CLIENTOP_PLAYER, false },
    { CS2_OP_CLIENTOP_TILE_SET, RS_CLIENTOP_TILE, true },
    { CS2_OP_CLIENTOP_TILE_DEL, RS_CLIENTOP_TILE, false },
};

/*
 * The context getters.
 *
 * `field` says which part of the subject the opcode asks for. The npc block is
 * the only complete one -- name, uid, coord and type -- and the others are
 * subsets, which is why this is a table of opcodes rather than four parallel
 * blocks of arithmetic.
 */
enum
{
    CTX_NAME = 0,
    CTX_UID,
    CTX_COORD,
    CTX_TYPE
};

struct RS_ClientOpCtxOp
{
    int opcode;
    enum RS_ClientOpKind kind;
    int field;
};

static struct RS_ClientOpCtxOp const CONTEXT_OPS[] = {
    { CS2_OP__6750, RS_CLIENTOP_NPC, CTX_NAME },
    { CS2_OP__6751, RS_CLIENTOP_NPC, CTX_UID },
    { CS2_OP__6752, RS_CLIENTOP_NPC, CTX_COORD },
    { CS2_OP__6753, RS_CLIENTOP_NPC, CTX_TYPE },
    { CS2_OP__6800, RS_CLIENTOP_LOC, CTX_NAME },
    { CS2_OP__6801, RS_CLIENTOP_LOC, CTX_COORD },
    { CS2_OP__6802, RS_CLIENTOP_LOC, CTX_TYPE },
    { CS2_OP__6850, RS_CLIENTOP_OBJ, CTX_NAME },
    { CS2_OP__6851, RS_CLIENTOP_OBJ, CTX_COORD },
    { CS2_OP__6852, RS_CLIENTOP_OBJ, CTX_TYPE },
    { CS2_OP__6900, RS_CLIENTOP_PLAYER, CTX_NAME },
    { CS2_OP__6902, RS_CLIENTOP_PLAYER, CTX_COORD },
    { CS2_OP__6950, RS_CLIENTOP_TILE, CTX_COORD },
};

void
RS_ClientOpReset(struct RS_ClientOpState* state)
{
    assert(state);
    memset(state, 0, sizeof(*state));
    RS_ClientOpContextEnd(state);
    state->mouseover.kind = -1;
    state->mouseover.uid = -1;
    state->mouseover.type = -1;
    state->mouseover.coord = -1;
    state->mouseover.layer = -1;
    state->mouseover_type = RS_MINIMENU_TYPE_NONE;
    for( int i = 0; i < RS_CLIENTOP_KIND_COUNT; i++ )
        RS_ClientOpActiveSet(state, (enum RS_ClientOpKind)i, NULL);
}

void
RS_ClientOpActiveSet(
    struct RS_ClientOpState* state,
    enum RS_ClientOpKind kind,
    struct RS_ClientOpContext const* ctx)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_CLIENTOP_KIND_COUNT);

    struct RS_ClientOpContext* reg = &state->active[kind];

    if( !ctx )
    {
        memset(reg, 0, sizeof(*reg));
        reg->kind = -1;
        reg->uid = -1;
        reg->type = -1;
        reg->coord = -1;
        reg->layer = -1;
        return;
    }
    assert(ctx->kind == (int)kind);
    *reg = *ctx;
}

struct RS_ClientOpContext const*
RS_ClientOpSubject(
    struct RS_ClientOpState const* state,
    enum RS_ClientOpKind kind,
    int running_script_id)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_CLIENTOP_KIND_COUNT);

    /*
     * Three sources, narrowest first.
     *
     * A dispatch is about the thing that was clicked and is gated on the script
     * that was named for it. The active register is about the thing an opcode
     * just went and found, and is gated on nothing -- LOC_FIND's whole purpose
     * is to answer the ops that follow it. The mouseover is the standing
     * answer for a script that asked without either.
     */
    if( state->ctx.kind == (int)kind && state->ctx.script_id > 0 &&
        state->ctx.script_id == running_script_id )
        return &state->ctx;
    if( state->active[kind].kind == (int)kind )
        return &state->active[kind];
    if( state->mouseover.kind == (int)kind )
        return &state->mouseover;
    return NULL;
}

static bool
clientop_slot_ok(int slot, char const* what)
{
    if( slot >= 0 && slot < RS_CLIENTOP_SLOT_MAX )
        return true;
    /* Not an assert: the number comes from a cache script, and a cache asking
     * for slot 12 is one whose slot this client should refuse rather than die
     * on. */
    fprintf(
        stderr,
        "clientop: %s named slot %d, which is outside 0..%d -- ignored\n",
        what,
        slot,
        RS_CLIENTOP_SLOT_MAX - 1);
    return false;
}

void
RS_ClientOpSet(
    struct RS_ClientOpState* state,
    enum RS_ClientOpKind kind,
    int slot,
    char const* label,
    int script_id)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_CLIENTOP_KIND_COUNT);

    if( !clientop_slot_ok(slot, "set") )
        return;

    state->slot[kind][slot].set = true;
    state->slot[kind][slot].script_id = script_id;
    /* A NULL label is not a contract violation here: it arrives from the VM's
     * string pool, where an empty push is a legal (if useless) thing for a
     * script to do, and a row with no text is better than an abort. */
    snprintf(
        state->slot[kind][slot].label,
        sizeof(state->slot[kind][slot].label),
        "%s",
        label ? label : "");
}

void
RS_ClientOpDel(struct RS_ClientOpState* state, enum RS_ClientOpKind kind, int slot)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_CLIENTOP_KIND_COUNT);

    if( !clientop_slot_ok(slot, "del") )
        return;
    memset(&state->slot[kind][slot], 0, sizeof(state->slot[kind][slot]));
}

struct RS_ClientOpSlot const*
RS_ClientOpGet(struct RS_ClientOpState const* state, enum RS_ClientOpKind kind, int slot)
{
    assert(state);
    if( kind < 0 || kind >= RS_CLIENTOP_KIND_COUNT )
        return NULL;
    if( slot < 0 || slot >= RS_CLIENTOP_SLOT_MAX )
        return NULL;
    if( !state->slot[kind][slot].set )
        return NULL;
    return &state->slot[kind][slot];
}

bool
RS_ClientOpApply(
    struct RS_ClientOpState* state,
    int opcode,
    bool is_set,
    int slot,
    char const* label,
    int script_id)
{
    assert(state);

    for( size_t i = 0; i < sizeof(CLIENTOP_OPS) / sizeof(CLIENTOP_OPS[0]); i++ )
    {
        if( CLIENTOP_OPS[i].opcode != opcode )
            continue;
        /* The VM already knows which form it popped; this asserts the two
         * agree rather than trusting either alone, because a SET whose label
         * was never popped would leave the string stack one deep. */
        assert(CLIENTOP_OPS[i].is_set == is_set);
        if( is_set )
            RS_ClientOpSet(state, CLIENTOP_OPS[i].kind, slot, label, script_id);
        else
            RS_ClientOpDel(state, CLIENTOP_OPS[i].kind, slot);
        return true;
    }
    return false;
}

void
RS_ClientOpContextBegin(struct RS_ClientOpState* state, struct RS_ClientOpContext const* ctx)
{
    assert(state);
    assert(ctx);
    state->ctx = *ctx;
}

void
RS_ClientOpMouseoverSet(
    struct RS_ClientOpState* state,
    struct RS_ClientOpContext const* ctx,
    int minimenu_type)
{
    assert(state);
    assert(ctx);
    state->mouseover = *ctx;
    state->mouseover_type = minimenu_type;
}

void
RS_ClientOpContextEnd(struct RS_ClientOpState* state)
{
    assert(state);
    memset(&state->ctx, 0, sizeof(state->ctx));
    state->ctx.kind = -1;
    state->ctx.script_id = -1;
    state->ctx.uid = -1;
    state->ctx.type = -1;
    state->ctx.coord = -1;
    state->ctx.layer = -1;
    /* The mouseover is deliberately NOT cleared here: this ends one client op's
     * dispatch, and what the pointer is on has not changed because a script
     * finished. RS_ClientOpReset clears both. */
}

bool
RS_ClientOpContextRead(
    struct RS_ClientOpState const* state,
    int opcode,
    int running_script_id,
    int* out_int,
    char const** out_str)
{
    assert(state);

    assert(out_int);
    assert(out_str);

    for( size_t i = 0; i < sizeof(CONTEXT_OPS) / sizeof(CONTEXT_OPS[0]); i++ )
    {
        struct RS_ClientOpCtxOp const* op = &CONTEXT_OPS[i];
        /*
         * The KIND has to match, not just the opcode.
         *
         * A script that reads `_6802` (loc type) while a TILE op is being
         * dispatched is asking about a loc that is not there, and answering
         * with the tile's coord because both live in the same struct would be
         * a confident wrong answer. "Nothing" is the truth.
         */
        /*
         * A dispatch answers first, then the mouseover.
         *
         * The dispatch is gated on WHICH SCRIPT is asking (see
         * RS_ClientOpContext::script_id); the mouseover is not gated at all,
         * because it is not a secret -- it is what the pointer is on, and any
         * script may ask. Falling back rather than choosing between them is
         * what makes one set of getters serve both: during a client op the
         * subject IS the op's, and outside one it is the mouseover.
         */
        struct RS_ClientOpContext const* from =
            RS_ClientOpSubject(state, op->kind, running_script_id);

        bool const live = from != NULL;

        if( op->opcode != opcode )
            continue;

        /* NULL means "this one is int-valued": see the declaration for why the
         * caller must not work that out for itself. */
        *out_str = NULL;
        *out_int = -1;

        switch( op->field )
        {
        case CTX_NAME:
            *out_str = live ? from->name : "";
            return true;
        case CTX_UID:
            *out_int = live ? from->uid : -1;
            return true;
        case CTX_COORD:
            *out_int = live ? from->coord : -1;
            return true;
        case CTX_TYPE:
            *out_int = live ? from->type : -1;
            return true;
        default:
            return false;
        }
    }
    return false;
}
