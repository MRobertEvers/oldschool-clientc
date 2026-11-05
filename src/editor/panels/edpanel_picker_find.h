#ifndef SRC_EDITOR_PANELS_EDPANEL_PICKER_FIND_H
#define SRC_EDITOR_PANELS_EDPANEL_PICKER_FIND_H

/**
 * The square picker's Find (workbench design section 5, "Navigation"; build
 * step 6.4): what the box under the picker's title understands, parsed and
 * resolved without touching anything.
 *
 *   m50_50            a square
 *   50 50, 50,50      a square (two numbers under 256)
 *   3222 3218 [1]     an absolute tile, optionally its level: the square that
 *                     holds it, and the tile is PINNED
 *   Lumbridge         an area or region name (a page of the Area dropdown),
 *                     else a collection name (the AREAS document); exact
 *                     names first, then the first that contains the text
 *   lane:mylane       a lane's squares (storage ported/mylane/maps)
 *   instance:         the instance space (EDITOR_PICKER_INSTANCE_*): the
 *                     unmapped high-coordinate squares instances are built in
 *   instance:<name>   an area or collection of that name
 *
 * Two halves, both pure: Editor_PickerFindParse reads the text alone (no
 * atlas), Editor_PickerFindResolve answers it against an atlas and the
 * collection names. Neither changes the picker; the Squares panel applies the
 * match (edpanel_squares.c).
 */

#include "edpanel_picker.h"

/** The instance space (design section 13: "unmapped space 0..19 x
 *  100..255"): the squares no world-map area claims, where the instance
 *  helper finds room. Inclusive. */
#define EDITOR_PICKER_INSTANCE_X0 0
#define EDITOR_PICKER_INSTANCE_X1 19
#define EDITOR_PICKER_INSTANCE_Z0 100
#define EDITOR_PICKER_INSTANCE_Z1 255

enum Editor_PickerFindKind
{
    EDITOR_PICKER_FIND_NONE = 0,
    /** "m50_50", "50 50", "50,50". */
    EDITOR_PICKER_FIND_SQUARE,
    /** "3222 3218", "3222 3218 1". */
    EDITOR_PICKER_FIND_TILE,
    /** Anything else: an area, region or collection name. */
    EDITOR_PICKER_FIND_NAME,
    /** "lane:<name>". */
    EDITOR_PICKER_FIND_LANE,
    /** "instance:" (name empty) or "instance:<name>". */
    EDITOR_PICKER_FIND_INSTANCE,
};

struct Editor_PickerFindQuery
{
    enum Editor_PickerFindKind kind;
    /** SQUARE and TILE: the square. */
    int map_x;
    int map_z;
    /** TILE: the absolute tile and its level (0 when unsaid). */
    int tile_x;
    int tile_z;
    int level;
    /** NAME, LANE, INSTANCE: the name, trimmed (may be "" for INSTANCE). */
    char name[EDITOR_PICKER_NAME_MAX];
};

/**
 * Parse `text`. Returns 0 when it says nothing Find can use: empty, "m50"
 * without its z, a square coordinate past 255, a level past 3, "lane:" with
 * no name, more than three numbers. Pure.
 */
int
Editor_PickerFindParse(
    const char* text,
    struct Editor_PickerFindQuery* out);

enum Editor_PickerFindMatchKind
{
    EDITOR_PICKER_MATCH_NONE = 0,
    /** A cell: `square` (or -1, an empty cell New can make) on `tab`; a
     *  tile query also sets `pin_valid`. */
    EDITOR_PICKER_MATCH_CELL,
    /** An Area page: `tab`. */
    EDITOR_PICKER_MATCH_AREA,
    /** A collection: `collection` (index into the names given). */
    EDITOR_PICKER_MATCH_COLLECTION,
    /** A lane's squares: `storage`. */
    EDITOR_PICKER_MATCH_LANE,
    /** The instance space. */
    EDITOR_PICKER_MATCH_INSTANCE,
};

struct Editor_PickerFindMatch
{
    enum Editor_PickerFindMatchKind kind;
    int tab;
    int square;
    int map_x;
    int map_z;
    int pin_valid;
    int pin_x;
    int pin_z;
    int pin_level;
    int collection;
    int storage;
};

/**
 * Answer `query` against `picker`'s atlas and the `collection_count`
 * collection names (`collections` may be NULL when the count is 0). Returns
 * 0 when nothing matches: a name no area, region or collection has, a lane
 * the atlas holds no storage for. A cell always matches (an empty one is
 * where New would create). A square on the current page stays on it. Pure.
 */
int
Editor_PickerFindResolve(
    const struct Editor_Picker* picker,
    const struct Editor_PickerFindQuery* query,
    const char* const* collections,
    int collection_count,
    struct Editor_PickerFindMatch* out);

/** The lane name of storage `storage` ("ported/<lane>/maps" -> "<lane>"),
 *  or of a lane square's full key ("ported/<lane>/maps/m16_101"); 0 (and
 *  "") when it is not a lane's (maps/). */
int
Editor_PickerLaneName(
    const char* storage,
    char out[EDITOR_PICKER_NAME_MAX]);

#endif
