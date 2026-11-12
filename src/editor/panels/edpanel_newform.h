#ifndef SRC_EDITOR_PANELS_EDPANEL_NEWFORM_H
#define SRC_EDITOR_PANELS_EDPANEL_NEWFORM_H

/**
 * The New square dialog's form, and the one question it answers: is this a
 * square the server will create, and if so, what DOC_CREATE says it
 * (docs/MAPED_DESIGN.html #squares).
 *
 *   where it lives  the coordinates and the storage: the content tree's
 *                   maps/ (what the cache pack reads), an existing lane
 *                   (ported/<lane>/maps/), or a lane the dialog creates by
 *                   naming it. A square whose coordinates are taken in that
 *                   storage is refused, naming the owner.
 *   what it starts  empty, a copy of the picker's selection, or a flat fill
 *                   at an authored height and underlay.
 *
 * Copy to... and Save as new are the same form with the template fixed to
 * the copy: Copy to asks coordinates and storage, Save as new the storage
 * alone (design: "the storage question alone").
 *
 * New with a place ("Place at m72_56", "Block 2x2") is the same question
 * asked of every cell of a block at once (Editor_NewSquareBlockCheck): the
 * squares are made where they are named, in maps/, with no scratch square
 * and no Square map in between. Find free (Editor_NewSquareFindFree) names a
 * place nothing stands on.
 *
 * App-free and chrome-free: the modal in edpanel_newsquare.c reads its widgets
 * into this form; tools-panels' test validates forms against a real atlas.
 */

#include "editor/editor_host.h"
#include "edpanel_picker.h"
#include "edpanels.h"

#define EDITOR_LANE_NAME_MAX 48
#define EDITOR_NEWFORM_MESSAGE_MAX 160
#define EDITOR_NEWFORM_PLACE_MAX 24
/** A block made in one Create: at most this many squares a side, so it is
 *  one scene (EDITOR_DOC_WORLD_SIDE_MAX) and one working set
 *  (EDITOR_UI_OPEN_SET_MAX). */
#define EDITOR_NEWFORM_BLOCK_SIDE_MAX 3
#define EDITOR_NEWFORM_BLOCK_MAX (EDITOR_NEWFORM_BLOCK_SIDE_MAX * EDITOR_NEWFORM_BLOCK_SIDE_MAX)
/**
 * Where Find free looks: the columns between the content tree's last (x 65)
 * and the scratch slots' first (x 90), in the rows beside the mainland's
 * middle -- cells the Square map shows without a scroll, and squares a
 * player can stand on (x 100 up is the server's instance pool).
 */
#define EDITOR_NEWFORM_FREE_X0 66
#define EDITOR_NEWFORM_FREE_X1 89
#define EDITOR_NEWFORM_FREE_Z0 48
#define EDITOR_NEWFORM_FREE_Z1 63

struct Editor_NewSquareForm
{
    enum Editor_NewSquareMode mode;
    int map_x;
    int map_z;
    /** Index into the picker's storages, or -1: create the lane `new_lane`. */
    int storage;
    char new_lane[EDITOR_LANE_NAME_MAX];
    enum Editor_DocTemplate template_kind;
    /** The square a COPY copies ("m50_50", or with its storage). */
    char source_key[EDITOR_DOC_KEY_MAX];
    /** FLAT: the `h` raw byte (1..255) and the underlay id (0..255). */
    int fill_height;
    int fill_underlay;
    /** New only: where the square (the block's south-west square) is made,
     *  as typed ("m72_56", "72 56"). "" is the scratch flow: a scratch square
     *  now, its place asked at Save. */
    char place[EDITOR_NEWFORM_PLACE_MAX];
    /** New with a place: the block, squares east (w) and north (h) of it,
     *  1..EDITOR_NEWFORM_BLOCK_SIDE_MAX each. 0 reads as 1. */
    int block_w;
    int block_h;
};

struct Editor_NewSquareCheck
{
    int ok;
    /** Why not, or what will happen ("creates m50_50 in maps/"). */
    char message[EDITOR_NEWFORM_MESSAGE_MAX];
    /** The storage the request names ("" = maps/). */
    char storage[EDITOR_DOC_STORAGE_MAX];
    /** The lane is new: nothing of it exists until the first save. */
    int creates_lane;
};

/** A lane directory name the content tree accepts: [a-z0-9_-], 1..47. */
int
Editor_NewSquareLaneValid(const char* lane);

/**
 * Check `form` against the atlas and build the request. `request` may be NULL
 * (a live check while the user types). Returns check->ok.
 */
int
Editor_NewSquareValidate(
    const struct Editor_Picker* picker,
    const struct Editor_NewSquareForm* form,
    struct Editor_NewSquareCheck* check,
    struct EditorHost_DocCreate* request);

/* ---- New at a place: one square or a block ---------------------------------- */

/** A place as typed: "m72_56", "72 56" or "72,56" (blanks around it
 *  ignored). 1 and the square, 0 when it is none of them. Pure. */
int
Editor_NewSquarePlaceParse(
    const char* text,
    int* out_map_x,
    int* out_map_z);

struct Editor_NewSquareBlock
{
    int ok;
    /** Why not, naming the first cell that cannot be made ("m73_57 already
     *  exists (maps/, unmapped)"), or what Create will do ("creates m72_56
     *  .. m73_57 (4) in maps/ and opens them"). Short: the dialog's line
     *  is one row of a 340 px modal. */
    char message[EDITOR_NEWFORM_MESSAGE_MAX];
    /** The south-west square, and the block from it. */
    int map_x;
    int map_z;
    int w;
    int h;
    /** Its squares, south row first, west to east: (x, z) pairs. */
    int count;
    int cells[EDITOR_NEWFORM_BLOCK_MAX][2];
};

/**
 * Check New's place and block (`form->place`, `block_w`, `block_h`) against
 * the atlas: the place parses, the block is on the map, and every cell is
 * one Editor_NewSquareValidate accepts in maps/ with the form's template --
 * none taken there, none a scratch slot. All of it or none: a block with one
 * bad cell is refused whole, naming that cell, before anything is created.
 * Returns out->ok.
 */
int
Editor_NewSquareBlockCheck(
    const struct Editor_Picker* picker,
    const struct Editor_NewSquareForm* form,
    struct Editor_NewSquareBlock* out);

/**
 * The first place a `w` x `h` block is free in EDITOR_NEWFORM_FREE_*: no
 * cell of it holds a square in any storage or is a scratch slot. A block
 * with nothing beside it either (no square in the ring around it) is
 * preferred, so new land does not touch the mainland's edge by accident;
 * rows south first, then west to east. 1 and its south-west square, or 0
 * when the range has no room. Pure.
 */
int
Editor_NewSquareFindFree(
    const struct Editor_Picker* picker,
    int w,
    int h,
    int* out_map_x,
    int* out_map_z);

#endif
