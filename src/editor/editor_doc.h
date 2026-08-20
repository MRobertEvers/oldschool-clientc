#ifndef SRC_EDITOR_EDITOR_DOC_H
#define SRC_EDITOR_EDITOR_DOC_H

/**
 * The editor's document: the set of map squares currently open, in authored
 * form, plus the derivation that turns them into what the renderer draws.
 *
 * The document is the source of truth while the editor runs. It is loaded from
 * the `.jm2`/`.jl2` text in OSRS-Content rather than from the baked cache, and
 * it is what a save writes back — so the bake is not in the editing loop and
 * cannot go stale underneath the editor.
 *
 * Going the other way — authored tiles out to the terrain the world builder
 * meshes — is editor_derive.h. It is separate because it is the only part of
 * the editor that needs the cache codec, and keeping it out of here leaves the
 * document layer as plain data that tests can exercise without a cache.
 */

#include "editor_types.h"

#include <stddef.h>

struct RSCache;

/** Squares open at once. The world load path tops out at 64 (an 8x8 region),
 *  and there is no reason to hold document state for squares it cannot show. */
#define EDITOR_DOC_MAX_SQUARES 64

/**
 * NEVER a stack local: 64 squares of 16,384 tiles is about 8 MB, which
 * overflows a default stack before the first field is written. The session owns
 * one on the heap (struct Editor), and tests must allocate theirs the same way.
 */
struct Editor_Doc
{
    struct Editor_Square squares[EDITOR_DOC_MAX_SQUARES];
    int square_count;

    /** Cache identity, for the tile-width flags the terrain codec needs. The
     *  widths are an era difference (u8 before OldSchool 209, u16 after), so
     *  deriving with the wrong one shifts every tile. Borrowed, not owned. */
    const struct RSCache* profile;
};

/* ---- square ------------------------------------------------------------- */

void
Editor_SquareInit(
    struct Editor_Square* square,
    int map_x,
    int map_z);

/** Frees the square's owned buffers and zeroes it. NULL-tolerant. */
void
Editor_SquareFree(struct Editor_Square* square);

/** Append a placement. Returns its index; grows the list as needed. */
int
Editor_SquareLocAdd(
    struct Editor_Square* square,
    const struct Editor_Loc* loc);

void
Editor_SquareLocRemoveAt(
    struct Editor_Square* square,
    int index);

/**
 * Index of the placement at `(level, x, z)` with this shape, or -1.
 *
 * Shape is part of the key because a tile legitimately holds several locs at
 * once — a wall, its decoration, and a ground decor occupy the same tile in
 * different layers, and the cache addresses them by shape. Matching on
 * position alone would make an edit to one silently hit another.
 */
int
Editor_SquareLocFind(
    const struct Editor_Square* square,
    int level,
    int x,
    int z,
    int shape);

/* ---- document ----------------------------------------------------------- */

void
Editor_DocInit(
    struct Editor_Doc* doc,
    const struct RSCache* profile);

void
Editor_DocFree(struct Editor_Doc* doc);

/** The open square at these coords, or NULL. */
struct Editor_Square*
Editor_DocFindSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z);

/**
 * The open square at these coords, opening an empty one if there is room.
 * Returns NULL only when the document is full.
 */
struct Editor_Square*
Editor_DocOpenSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z);

/** 1 when any open square has unsaved edits. */
int
Editor_DocHasUnsaved(const struct Editor_Doc* doc);

#endif
