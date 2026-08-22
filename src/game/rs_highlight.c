#include "game/rs_highlight.h"

#include "cs2vm2/cs2_opcode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * The opcode table.
 *
 * The family is eight kinds of five opcodes, laid out in a strict block of
 * five per kind from 7000, plus one more block at 7040 that the opcode header
 * has not named. Written out rather than computed from `(opcode - 7000) / 5`,
 * because that arithmetic would silently absorb a renumbering: a kind inserted
 * in the middle would shift every later block by one and the formula would go
 * on returning a plausible answer for the wrong kind.
 *
 * 7040..7044 -- "_7040 .. _7044", a SETUP/ON/OFF/GET/CLEAR block keyed by a
 * STRING like the player family -- is deliberately absent. Its subject is a
 * name this client never sees: CS2VM2_Op_Highlight pops and discards the
 * string stack, so an entry here could only ever record members keyed on
 * nothing. Falling through as "not ours" is the honest result, and the caller
 * logs it.
 */
struct RS_HighlightOp
{
    int opcode;
    enum RS_HighlightKind kind;
    /** 0 setup, 1 on, 2 off, 3 get, 4 clear. */
    int action;
    /** Ints the form takes, as the opcode header states them. */
    int arg_count;
    /** Where the group sits in `args`, in push order. */
    int group_slot;
    /** Where the subject key sits, or -1 for a form that names none. */
    int key_slot;
    /** Where the coord sits, or -1. */
    int coord_slot;
    /** Where the per-subject flags sit, or -1. */
    int flag_slot;
};

#define OP_SETUP 0
#define OP_ON 1
#define OP_OFF 2
#define OP_GET 3
#define OP_CLEAR 4

/* clang-format off */
static struct RS_HighlightOp const HIGHLIGHT_OPS[] = {
    /* opcode                             kind                    action     n  grp key crd flg */
    /* SETUP is (group, colour, style, opacity, flags) for every kind. */
    { CS2_OP_HIGHLIGHT_NPC_SETUP,       RS_HIGHLIGHT_NPC,       OP_SETUP,  5,  0, -1, -1, -1 },
    /* NPC: (npc_uid, coord, group). */
    { CS2_OP_HIGHLIGHT_NPC_ON,          RS_HIGHLIGHT_NPC,       OP_ON,     3,  2,  0,  1, -1 },
    { CS2_OP_HIGHLIGHT_NPC_OFF,         RS_HIGHLIGHT_NPC,       OP_OFF,    3,  2,  0,  1, -1 },
    { CS2_OP_HIGHLIGHT_NPC_GET,         RS_HIGHLIGHT_NPC,       OP_GET,    3,  2,  0,  1, -1 },
    { CS2_OP_HIGHLIGHT_NPC_CLEAR,       RS_HIGHLIGHT_NPC,       OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_NPCTYPE_SETUP,   RS_HIGHLIGHT_NPCTYPE,   OP_SETUP,  5,  0, -1, -1, -1 },
    /* The *TYPE forms name a type and nothing else: (type, group). */
    { CS2_OP_HIGHLIGHT_NPCTYPE_ON,      RS_HIGHLIGHT_NPCTYPE,   OP_ON,     2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_OFF,     RS_HIGHLIGHT_NPCTYPE,   OP_OFF,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_GET,     RS_HIGHLIGHT_NPCTYPE,   OP_GET,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_CLEAR,   RS_HIGHLIGHT_NPCTYPE,   OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_LOC_SETUP,       RS_HIGHLIGHT_LOC,       OP_SETUP,  5,  0, -1, -1, -1 },
    /* LOC and OBJ carry a per-subject flag word: (type, coord, group, flags). */
    { CS2_OP_HIGHLIGHT_LOC_ON,          RS_HIGHLIGHT_LOC,       OP_ON,     4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_LOC_OFF,         RS_HIGHLIGHT_LOC,       OP_OFF,    4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_LOC_GET,         RS_HIGHLIGHT_LOC,       OP_GET,    4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_LOC_CLEAR,       RS_HIGHLIGHT_LOC,       OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_LOCTYPE_SETUP,   RS_HIGHLIGHT_LOCTYPE,   OP_SETUP,  5,  0, -1, -1, -1 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_ON,      RS_HIGHLIGHT_LOCTYPE,   OP_ON,     2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_OFF,     RS_HIGHLIGHT_LOCTYPE,   OP_OFF,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_GET,     RS_HIGHLIGHT_LOCTYPE,   OP_GET,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_CLEAR,   RS_HIGHLIGHT_LOCTYPE,   OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_OBJ_SETUP,       RS_HIGHLIGHT_OBJ,       OP_SETUP,  5,  0, -1, -1, -1 },
    { CS2_OP_HIGHLIGHT_OBJ_ON,          RS_HIGHLIGHT_OBJ,       OP_ON,     4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_OBJ_OFF,         RS_HIGHLIGHT_OBJ,       OP_OFF,    4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_OBJ_GET,         RS_HIGHLIGHT_OBJ,       OP_GET,    4,  2,  0,  1,  3 },
    { CS2_OP_HIGHLIGHT_OBJ_CLEAR,       RS_HIGHLIGHT_OBJ,       OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_OBJTYPE_SETUP,   RS_HIGHLIGHT_OBJTYPE,   OP_SETUP,  5,  0, -1, -1, -1 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_ON,      RS_HIGHLIGHT_OBJTYPE,   OP_ON,     2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_OFF,     RS_HIGHLIGHT_OBJTYPE,   OP_OFF,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_GET,     RS_HIGHLIGHT_OBJTYPE,   OP_GET,    2,  1,  0, -1, -1 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_CLEAR,   RS_HIGHLIGHT_OBJTYPE,   OP_CLEAR,  1,  0, -1, -1, -1 },

    /* PLAYER's subject is a NAME on the string stack, which the VM discards --
     * so SETUP and CLEAR are recorded and ON/OFF/GET are not in this table.
     * They fall through as "not ours" and the caller logs them, which is what
     * a player highlight nobody can key is. */
    { CS2_OP_HIGHLIGHT_PLAYER_SETUP,    RS_HIGHLIGHT_PLAYER,    OP_SETUP,  5,  0, -1, -1, -1 },
    { CS2_OP_HIGHLIGHT_PLAYER_CLEAR,    RS_HIGHLIGHT_PLAYER,    OP_CLEAR,  1,  0, -1, -1, -1 },

    { CS2_OP_HIGHLIGHT_TILE_SETUP,      RS_HIGHLIGHT_TILE,      OP_SETUP,  5,  0, -1, -1, -1 },
    /* TILE names a place and no key: (coord, group, flags). */
    { CS2_OP_HIGHLIGHT_TILE_ON,         RS_HIGHLIGHT_TILE,      OP_ON,     3,  1, -1,  0,  2 },
    { CS2_OP_HIGHLIGHT_TILE_OFF,        RS_HIGHLIGHT_TILE,      OP_OFF,    3,  1, -1,  0,  2 },
    { CS2_OP_HIGHLIGHT_TILE_GET,        RS_HIGHLIGHT_TILE,      OP_GET,    3,  1, -1,  0,  2 },
    { CS2_OP_HIGHLIGHT_TILE_CLEAR,      RS_HIGHLIGHT_TILE,      OP_CLEAR,  1,  0, -1, -1, -1 },
};
/* clang-format on */

static char const* const KIND_NAME[RS_HIGHLIGHT_KIND_COUNT] = {
    "npc", "npctype", "loc", "loctype", "obj", "objtype", "player", "tile"
};

char const*
RS_HighlightKindName(enum RS_HighlightKind kind)
{
    if( kind < 0 || kind >= RS_HIGHLIGHT_KIND_COUNT )
        return "?";
    return KIND_NAME[kind];
}

static struct RS_HighlightOp const*
highlight_op_find(int opcode)
{
    for( size_t i = 0; i < sizeof(HIGHLIGHT_OPS) / sizeof(HIGHLIGHT_OPS[0]); i++ )
        if( HIGHLIGHT_OPS[i].opcode == opcode )
            return &HIGHLIGHT_OPS[i];
    return NULL;
}

bool
RS_HighlightOpcodeKind(int opcode, enum RS_HighlightKind* out_kind)
{
    struct RS_HighlightOp const* op = highlight_op_find(opcode);

    assert(out_kind);
    if( !op )
        return false;
    *out_kind = op->kind;
    return true;
}

void
RS_HighlightReset(struct RS_HighlightState* state)
{
    assert(state);

    memset(state, 0, sizeof(*state));
    /* -1, not 0: see the header. A zeroed style is black-at-nothing, which is
     * off by accident; -1 is the value every disabling call in the cache
     * actually passes. */
    for( int kind = 0; kind < RS_HIGHLIGHT_KIND_COUNT; kind++ )
        for( int group = 0; group < RS_HIGHLIGHT_GROUP_MAX; group++ )
            state->style[kind][group].colour = -1;
}

/* A group id the cache asked for but this build has no slot for. Not an
 * assert: the number comes from a script, and a cache naming group 40 is a
 * cache this client should refuse the group of, not die on. */
static bool
highlight_group_ok(struct RS_HighlightState* state, int group, char const* what)
{
    if( group >= 0 && group < RS_HIGHLIGHT_GROUP_MAX )
        return true;
    (void)state;
    fprintf(
        stderr,
        "highlight: %s named group %d, which is outside 0..%d -- ignored\n",
        what,
        group,
        RS_HIGHLIGHT_GROUP_MAX - 1);
    return false;
}

void
RS_HighlightSetup(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int colour,
    int style,
    int opacity,
    int flags)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( !highlight_group_ok(state, group, "setup") )
        return;

    state->style[kind][group].colour = colour;
    state->style[kind][group].style = style;
    state->style[kind][group].opacity = opacity;
    state->style[kind][group].flags = flags;
    state->revision++;
}

static int
highlight_member_find(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord)
{
    for( int i = 0; i < state->member_count[kind]; i++ )
    {
        struct RS_HighlightMember const* m = &state->member[kind][i];
        if( m->group == group && m->key == key && m->coord == coord )
            return i;
    }
    return -1;
}

bool
RS_HighlightOn(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord,
    int flags)
{
    int at;

    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( !highlight_group_ok(state, group, "on") )
        return false;

    at = highlight_member_find(state, kind, group, key, coord);
    if( at >= 0 )
    {
        /* Already in: the flags are the only thing a repeat can change, and a
         * script that re-adds a subject every tick (the clue helper does)
         * must not grow the list once per tick. */
        state->member[kind][at].flags = flags;
        return true;
    }

    if( state->member_count[kind] >= RS_HIGHLIGHT_MEMBERS_MAX )
    {
        if( !state->overflowed )
        {
            state->overflowed = true;
            fprintf(
                stderr,
                "highlight: the %s list is full at %d subjects; further ones are "
                "refused (they would otherwise be silently unmarked)\n",
                RS_HighlightKindName(kind),
                RS_HIGHLIGHT_MEMBERS_MAX);
        }
        return false;
    }

    at = state->member_count[kind]++;
    state->member[kind][at].group = group;
    state->member[kind][at].key = key;
    state->member[kind][at].coord = coord;
    state->member[kind][at].flags = flags;
    state->revision++;
    return true;
}

void
RS_HighlightOff(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord)
{
    int at;

    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( !highlight_group_ok(state, group, "off") )
        return;

    at = highlight_member_find(state, kind, group, key, coord);
    if( at < 0 )
        return;
    /* Order carries no meaning -- every member is drawn -- so the hole is
     * filled from the end rather than by shifting the tail. */
    state->member[kind][at] = state->member[kind][--state->member_count[kind]];
    state->revision++;
}

bool
RS_HighlightGet(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( group < 0 || group >= RS_HIGHLIGHT_GROUP_MAX )
        return false;
    return highlight_member_find(state, kind, group, key, coord) >= 0;
}

void
RS_HighlightClear(struct RS_HighlightState* state, enum RS_HighlightKind kind, int group)
{
    int kept = 0;

    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( !highlight_group_ok(state, group, "clear") )
        return;

    for( int i = 0; i < state->member_count[kind]; i++ )
        if( state->member[kind][i].group != group )
            state->member[kind][kept++] = state->member[kind][i];
    if( kept != state->member_count[kind] )
        state->revision++;
    state->member_count[kind] = kept;
}

bool
RS_HighlightGroupLive(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group)
{
    assert(state);
    assert(kind >= 0);
    assert(kind < RS_HIGHLIGHT_KIND_COUNT);

    if( group < 0 || group >= RS_HIGHLIGHT_GROUP_MAX )
        return false;
    /* Both are how the cache says "off", and it says it both ways at once
     * everywhere except one call site -- clientscript 1854's teardown passes
     * the colour it was using and opacity 0. So neither test alone is enough. */
    return state->style[kind][group].colour >= 0 && state->style[kind][group].opacity > 0;
}

bool
RS_HighlightApply(
    struct RS_HighlightState* state,
    int opcode,
    int const* args,
    int arg_count,
    int* out_query)
{
    struct RS_HighlightOp const* op = highlight_op_find(opcode);
    int group;
    int key;
    int coord;
    int flags;

    assert(state);
    assert(args || arg_count == 0);

    if( !op )
        return false;
    if( arg_count != op->arg_count )
    {
        /* The opcode table in cs2vm2 and this one disagree about the form.
         * Acting on it would read a group out of whichever slot happened to
         * line up, so it is refused and said out loud. */
        fprintf(
            stderr,
            "highlight: opcode %d took %d ints, expected %d -- ignored\n",
            opcode,
            arg_count,
            op->arg_count);
        return false;
    }

    group = args[op->group_slot];
    key = op->key_slot >= 0 ? args[op->key_slot] : -1;
    coord = op->coord_slot >= 0 ? args[op->coord_slot] : -1;
    flags = op->flag_slot >= 0 ? args[op->flag_slot] : 0;

    switch( op->action )
    {
    case OP_SETUP:
        RS_HighlightSetup(state, op->kind, group, args[1], args[2], args[3], args[4]);
        return true;
    case OP_ON:
        RS_HighlightOn(state, op->kind, group, key, coord, flags);
        return true;
    case OP_OFF:
        RS_HighlightOff(state, op->kind, group, key, coord);
        return true;
    case OP_GET:
        assert(out_query);
        *out_query = RS_HighlightGet(state, op->kind, group, key, coord) ? 1 : 0;
        return true;
    case OP_CLEAR:
        RS_HighlightClear(state, op->kind, group);
        return true;
    default:
        return false;
    }
}
