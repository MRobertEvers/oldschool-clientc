#include "plugin/plugins/nxt_cox_layout.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * Every CoX room, by where it is authored.
 *
 * `zcell` is the template tile z / 32; `plane` the template level, or -1 for a
 * room the table knows only by row. The rows and planes are the server's
 * (cox_layout.rs2: ^cox_tpl_* and ~cox_room_src_plane / ~cox_room_src_cz),
 * which agree with Zenyte's RaidRoom and RuneLite's InstanceTemplates.
 *
 * The TEXT is the C++ client's, as far as it has been published. Its one
 * picture (the wiki's Chambers_of_Xeric_helper.png) shows: Corridor --
 * Scavenger, Floor end; Combat -- Guardians, Muttadile, Tekton, Mystics;
 * Resource -- Bats, Fish; Puzzle -- Tightrope. The rest follow the same
 * pattern (singular or the encounter's short name, sentence case) and are
 * UNCONFIRMED: Shamans, Vasa, Vespula, Vanguards, Ice demon, Crabs, Thieving.
 *
 * Which resource room is which is read off the template itself: m51_85 plane
 * 0 carries `fishing_point_icon` and the water edges, plane 1
 * `hunting_shop_icon`.
 */
struct NxtCoxRoomRow
{
    int zcell;
    int plane;
    struct NxtCoxRoomKind kind;
};

static struct NxtCoxRoomRow const NXT_COX_ROOMS[] = {
    { 161, -1, { "Corridor", "Floor end", true } },
    /* The raid's own start and each floor's start: walked, never listed. */
    { 162, -1, { "Corridor", "Start", false } },
    { 178, -1, { "Corridor", "Floor start", false } },
    { 163, 0, { "Corridor", "Scavenger", true } },
    { 163, 1, { "Corridor", "Scavenger", true } },
    { 164, 0, { "Combat", "Shamans", true } },
    { 164, 1, { "Combat", "Mystics", true } },
    { 164, 2, { "Combat", "Guardians", true } },
    { 165, 0, { "Combat", "Vasa", true } },
    { 165, 1, { "Combat", "Tekton", true } },
    { 165, 2, { "Combat", "Vespula", true } },
    { 166, 0, { "Combat", "Vanguards", true } },
    { 166, 1, { "Combat", "Muttadile", true } },
    { 167, 0, { "Puzzle", "Ice demon", true } },
    { 167, 1, { "Puzzle", "Tightrope", true } },
    { 167, 2, { "Puzzle", "Crabs", true } },
    { 168, 0, { "Puzzle", "Thieving", true } },
    { 170, 0, { "Resource", "Fish", true } },
    { 170, 1, { "Resource", "Bats", true } },
};

#define NXT_COX_ROOM_COUNT ((int)(sizeof(NXT_COX_ROOMS) / sizeof(NXT_COX_ROOMS[0])))

/** Template x / 32 of the counter-clockwise variant; the other two follow. */
#define NXT_COX_VARIANT_XCELL0 102

int
NxtCox_RoomVariantOf(int template_x)
{
    int const v = (template_x >> 5) - NXT_COX_VARIANT_XCELL0;

    if( template_x < 0 || v < 0 || v > 2 )
        return -1;
    return v;
}

int
NxtCox_RoomKindOf(int template_x, int template_z, int template_plane)
{
    int const zcell = template_z >> 5;

    if( template_z < 0 || NxtCox_RoomVariantOf(template_x) < 0 )
        return -1;
    for( int i = 0; i < NXT_COX_ROOM_COUNT; i++ )
    {
        if( NXT_COX_ROOMS[i].zcell != zcell )
            continue;
        if( NXT_COX_ROOMS[i].plane < 0 || NXT_COX_ROOMS[i].plane == template_plane )
            return i;
    }
    return -1;
}

struct NxtCoxRoomKind const*
NxtCox_RoomKind(int kind)
{
    assert(kind >= 0);
    assert(kind < NXT_COX_ROOM_COUNT);
    return &NXT_COX_ROOMS[kind].kind;
}

/*
 * A, B, C in template order: x cell 102, 103, 104. The picture prints a
 * letter per room and the template x is the only per-room fact with three
 * values, so the mapping is that; which letter the C++ client gives which
 * variant is otherwise UNCONFIRMED.
 */
char
NxtCox_VariantLetter(int variant)
{
    assert(variant >= 0);
    assert(variant <= 2);
    return (char)('A' + variant);
}

/*
 * Authored for a path heading north: in through the south wall, out through
 * the west (counter-clockwise), north (through) or east (clockwise) one. A
 * rotation turns all of it clockwise.
 */
int
NxtCox_ExitDir(int variant, int rotation)
{
    static int const AUTHORED_EXIT[3] = { 3, 0, 1 };

    assert(variant >= 0);
    assert(variant <= 2);
    assert(rotation >= 0);
    assert(rotation <= 3);
    return (AUTHORED_EXIT[variant] + rotation) & 3;
}

static int const NXT_COX_DX[4] = { 0, 1, 0, -1 };
static int const NXT_COX_DZ[4] = { 1, 0, -1, 0 };

void
NxtCoxLayout_Reset(struct NxtCoxLayout* layout)
{
    assert(layout);
    memset(layout, 0, sizeof(*layout));
}

static int
nxt_cox_find(struct NxtCoxLayout const* layout, int level, int cell_x, int cell_z)
{
    for( int i = 0; i < layout->count; i++ )
    {
        struct NxtCoxCell const* c = &layout->cells[i];
        if( c->level == level && c->cell_x == cell_x && c->cell_z == cell_z )
            return i;
    }
    return -1;
}

bool
NxtCoxLayout_Note(
    struct NxtCoxLayout* layout,
    int level,
    int cell_x,
    int cell_z,
    int kind,
    int variant,
    int rotation)
{
    struct NxtCoxCell* c;
    int at;

    assert(layout);
    assert(level >= 0);
    assert(level < NXT_COX_MAX_LEVELS);
    assert(kind >= 0);
    assert(kind < NXT_COX_ROOM_COUNT);
    assert(variant >= 0);
    assert(variant <= 2);
    assert(rotation >= 0);
    assert(rotation <= 3);

    at = nxt_cox_find(layout, level, cell_x, cell_z);
    if( at < 0 )
    {
        /* A raid has sixteen rooms; a table this full is a caller that never
         * reset between raids. */
        assert(layout->count < NXT_COX_MAX_CELLS);
        at = layout->count++;
    }
    else
    {
        c = &layout->cells[at];
        if( c->kind == kind && c->variant == variant && c->rotation == rotation )
            return false;
    }
    c = &layout->cells[at];
    c->level = (int8_t)level;
    c->cell_x = cell_x;
    c->cell_z = cell_z;
    c->kind = (int8_t)kind;
    c->variant = (int8_t)variant;
    c->rotation = (int8_t)rotation;
    return true;
}

void
NxtCoxLayout_Visit(struct NxtCoxLayout* layout, int level)
{
    assert(layout);
    assert(level >= 0);
    assert(level < NXT_COX_MAX_LEVELS);
    for( int i = 0; i < layout->visited_count; i++ )
    {
        if( layout->visited[i] == level )
            return;
    }
    assert(layout->visited_count < NXT_COX_MAX_LEVELS);
    layout->visited[layout->visited_count++] = (int8_t)level;
}

/** The cell `i` is left into, or -1 when that is no known room. */
static int
nxt_cox_next(struct NxtCoxLayout const* layout, int i)
{
    struct NxtCoxCell const* c = &layout->cells[i];
    int const dir = NxtCox_ExitDir(c->variant, c->rotation);

    return nxt_cox_find(layout, c->level, c->cell_x + NXT_COX_DX[dir], c->cell_z + NXT_COX_DZ[dir]);
}

static int
nxt_cox_order_floor(
    struct NxtCoxLayout const* layout, int level, bool* taken, int* out, int at, int max)
{
    bool entered[NXT_COX_MAX_CELLS] = { false };

    for( int i = 0; i < layout->count; i++ )
    {
        if( layout->cells[i].level != level )
            continue;
        int const n = nxt_cox_next(layout, i);
        if( n >= 0 && n != i )
            entered[n] = true;
    }

    /* Heads first, in the order they were seen; then whatever a cycle left. */
    for( int pass = 0; pass < 2; pass++ )
    {
        for( int head = 0; head < layout->count; head++ )
        {
            if( layout->cells[head].level != level || taken[head] )
                continue;
            if( pass == 0 && entered[head] )
                continue;
            for( int i = head; i >= 0 && !taken[i]; i = nxt_cox_next(layout, i) )
            {
                taken[i] = true;
                if( NxtCox_RoomKind(layout->cells[i].kind)->listed && at < max )
                    out[at++] = i;
            }
        }
    }
    return at;
}

int
NxtCoxLayout_Order(struct NxtCoxLayout const* layout, int* out, int max)
{
    bool taken[NXT_COX_MAX_CELLS] = { false };
    bool floor_done[NXT_COX_MAX_LEVELS] = { false };
    int at = 0;

    assert(layout);
    assert(out);
    assert(max >= 0);

    for( int v = 0; v < layout->visited_count; v++ )
    {
        int const level = layout->visited[v];
        at = nxt_cox_order_floor(layout, level, taken, out, at, max);
        floor_done[level] = true;
    }
    for( int level = 0; level < NXT_COX_MAX_LEVELS; level++ )
    {
        if( !floor_done[level] )
            at = nxt_cox_order_floor(layout, level, taken, out, at, max);
    }
    return at;
}
