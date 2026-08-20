#include "editor_doc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ---- square ------------------------------------------------------------- */

void
Editor_SquareInit(
    struct Editor_Square* square,
    int map_x,
    int map_z)
{
    assert(square);

    memset(square, 0, sizeof(*square));
    square->map_x = map_x;
    square->map_z = map_z;
}

void
Editor_SquareFree(struct Editor_Square* square)
{
    if( !square )
        return;

    free(square->locs);
    free(square->trailing);
    free(square->foreign);
    memset(square, 0, sizeof(*square));
}

int
Editor_SquareLocAdd(
    struct Editor_Square* square,
    const struct Editor_Loc* loc)
{
    assert(square);
    assert(loc);

    if( square->loc_count == square->loc_capacity )
    {
        int next = square->loc_capacity ? square->loc_capacity * 2 : 256;
        square->locs = realloc(square->locs, (size_t)next * sizeof(*square->locs));
        assert(square->locs);
        square->loc_capacity = next;
    }

    square->locs[square->loc_count] = *loc;
    return square->loc_count++;
}

void
Editor_SquareLocRemoveAt(
    struct Editor_Square* square,
    int index)
{
    assert(square);
    assert(index >= 0);
    assert(index < square->loc_count);

    /* Order matters: the cache lists a square's locs sorted, and the client
     * walks them in file order. Compacting rather than swapping the tail in
     * keeps a save from reshuffling placements the user did not touch. */
    memmove(
        &square->locs[index],
        &square->locs[index + 1],
        (size_t)(square->loc_count - index - 1) * sizeof(*square->locs));
    square->loc_count--;
}

int
Editor_SquareLocFind(
    const struct Editor_Square* square,
    int level,
    int x,
    int z,
    int shape)
{
    assert(square);

    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level == level && loc->x == x && loc->z == z && loc->shape == shape )
            return i;
    }
    return -1;
}

/* ---- document ----------------------------------------------------------- */

void
Editor_DocInit(
    struct Editor_Doc* doc,
    const struct RSCache* profile)
{
    assert(doc);
    assert(profile);

    memset(doc, 0, sizeof(*doc));
    doc->profile = profile;
}

void
Editor_DocFree(struct Editor_Doc* doc)
{
    if( !doc )
        return;

    for( int i = 0; i < doc->square_count; i++ )
        Editor_SquareFree(&doc->squares[i]);
    doc->square_count = 0;
}

struct Editor_Square*
Editor_DocFindSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    assert(doc);

    for( int i = 0; i < doc->square_count; i++ )
    {
        if( doc->squares[i].map_x == map_x && doc->squares[i].map_z == map_z )
            return &doc->squares[i];
    }
    return NULL;
}

struct Editor_Square*
Editor_DocOpenSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    struct Editor_Square* square;

    assert(doc);

    square = Editor_DocFindSquare(doc, map_x, map_z);
    if( square )
        return square;

    if( doc->square_count == EDITOR_DOC_MAX_SQUARES )
        return NULL;

    square = &doc->squares[doc->square_count++];
    Editor_SquareInit(square, map_x, map_z);
    return square;
}

int
Editor_DocHasUnsaved(const struct Editor_Doc* doc)
{
    assert(doc);

    for( int i = 0; i < doc->square_count; i++ )
    {
        if( doc->squares[i].dirty_map || doc->squares[i].dirty_loc )
            return 1;
    }
    return 0;
}
