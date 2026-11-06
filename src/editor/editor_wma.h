#ifndef SRC_EDITOR_EDITOR_WMA_H
#define SRC_EDITOR_EDITOR_WMA_H

/**
 * A world-map AREA file (workbench 6.5, design section 13 "Instance helper":
 * "Map area: add worldmap/areas/<name>.wma"), the TEXT document "wma:<name>"
 * at `<content_dir>/worldmap/areas/<name>.wma`.
 *
 * THE SHAPE is the one the server catalog's atlas reader takes
 * (torirsmaped/torirs_maped_catalog.c `cat_parse_worldmap`, which reads every
 * `.wma` under worldmap/areas), and the one cachepack writes for the details
 * archive (3rd/rscache/tools/cachepack/cp_decode.c `worldmap_emit_area`):
 *
 *     // worldmap/areas/tutorial_cave.wma: written by the instance helper ...
 *     [tutorial_cave]
 *     internal=tutorial_cave
 *     external=Tutorial cave
 *     origin=17308032
 *     background=0xFF000000
 *     main=no
 *     zoom=100
 *     unknown_int=-1
 *     unknown_byte=0
 *     section1=0,0,4,16,101,16,102,0,0,0,0,16,101,16,102,0,0,0
 *     section1_dst_chunk_y_high=0
 *
 * A `[name]` header opens an AREA (the reader keys the area by it); `external`
 * is the name the map shows; `main=yes` marks the surface map; a `section<N>`
 * of 18 integers claims squares by its SOURCE side, (type, min_plane, planes,
 * src rx, rz, rx_end, rz_end, ...): type 0 the rectangle rx..rx_end x
 * rz..rz_end, any other type the one square (rx, rz). The destination half is
 * where the world map draws it. Every other `key=value` (origin, background,
 * zoom, the two unknowns, section<N>_dst_chunk_y_high) is carried and
 * re-emitted verbatim; the reader ignores them.
 *
 * LINE-PRESERVING, like editor_stamps.c and editor_areas.c: every line keeps
 * its own bytes, so parse -> emit is byte-identical (comments, blank lines,
 * CRLF, a missing final newline), and a document written through the
 * authority is a TEXT_SPLICE plan from the old text to the new
 * (builder_text.h Builder_TextSync), one undo step.
 *
 * WHAT THE CACHE SEES: nothing, yet. cachepack packs the details archive from
 * `details.wma` (one block per map, each with a file id in details.compack)
 * and the composite map from `compositemap.wmc`; a separate `<name>.wma`
 * reaches the catalog's atlas (the squares become a named area, no longer
 * "unmapped") and the picker's Area pages, not the game's world map. Merging
 * a block into details.wma with a fresh id is the packer's half, open.
 *
 * Pure: no document, no host. Malformed text is data (the parser reports the
 * line); NULL arguments assert.
 */

#include <stddef.h>
#include <stdint.h>

/** TEXT document key prefix: "wma:<name>". */
#define EDITOR_WMA_KEY_PREFIX "wma:"
#define EDITOR_WMA_DIR "worldmap/areas"
/** Bytes of an area / file name, the terminator included: [a-z0-9_]. */
#define EDITOR_WMA_NAME_MAX 48
/** Bytes of the display (`external`) name. */
#define EDITOR_WMA_EXTERNAL_MAX 64
/** Sections one area holds. */
#define EDITOR_WMA_SECTIONS_MAX 32
/** Integers in a section line. */
#define EDITOR_WMA_SECTION_INTS 18
/** Areas one file holds. */
#define EDITOR_WMA_AREAS_MAX 16

enum Editor_WmaLineKind
{
    /** Blank, a comment, or a line before the first header: carried. */
    EDITOR_WMA_LINE_TEXT = 0,
    /** `[name]` */
    EDITOR_WMA_LINE_HEADER,
    /** `key=value` inside an area. */
    EDITOR_WMA_LINE_FIELD,
};

struct Editor_WmaSection
{
    /** type, min_plane, planes, src rx, rz, rx_end, rz_end, src chunk x/z
     *  low/high, dst rx, rz, rx_end, rz_end, dst chunk x/z low, x high. */
    int v[EDITOR_WMA_SECTION_INTS];
};

struct Editor_WmaArea
{
    char name[EDITOR_WMA_NAME_MAX];
    /** `external=`, "" when the area states none (the reader then shows the
     *  header name). */
    char external[EDITOR_WMA_EXTERNAL_MAX];
    int is_main;
    int section_count;
    struct Editor_WmaSection sections[EDITOR_WMA_SECTIONS_MAX];
    /** Line index of the header. */
    int line;
};

struct Editor_WmaLine
{
    enum Editor_WmaLineKind kind;
    /** HEADER / FIELD: the area's index; -1 for TEXT. */
    int area;
    /** Owned, NUL-terminated: the line's bytes without its newline. */
    char* text;
};

struct Editor_WmaFile
{
    struct Editor_WmaLine* lines;
    int line_count;
    int line_capacity;
    int final_newline;
    int area_count;
    struct Editor_WmaArea areas[EDITOR_WMA_AREAS_MAX];
};

struct Editor_WmaParseResult
{
    int ok;
    /** 1-based line of the first failure, 0 when ok. */
    int line;
};

/** [a-z0-9_], 1..47 bytes: what a file name, a TEXT key and the reader's
 *  `[name]` all take. */
int
Editor_WmaNameValid(const char* name);

/** "wma:<name>". */
void
Editor_WmaDocKey(
    char* out,
    size_t out_size,
    const char* name);

void
Editor_WmaInit(struct Editor_WmaFile* file);

/** NULL-tolerant. */
void
Editor_WmaFree(struct Editor_WmaFile* file);

/**
 * Parse, replacing what `file` held. Refused (ok 0, the line named): a
 * header with no name or a name taken in the file, more than
 * EDITOR_WMA_AREAS_MAX areas or EDITOR_WMA_SECTIONS_MAX sections, a
 * `section<N>=` that is not 18 integers, a `main=` that is neither yes nor
 * no. The lines are kept either way.
 */
struct Editor_WmaParseResult
Editor_WmaParse(
    struct Editor_WmaFile* file,
    const char* text,
    size_t length);

/** snprintf's sizing contract. */
size_t
Editor_WmaEmit(
    const struct Editor_WmaFile* file,
    char* out,
    size_t out_capacity);

/** Index of area `name`, or -1. */
int
Editor_WmaFindArea(
    const struct Editor_WmaFile* file,
    const char* name);

/** Whether `area` claims square (map_x, map_z) as the reader does: a type 0
 *  section's source rectangle, any other type's source square. */
int
Editor_WmaAreaClaims(
    const struct Editor_WmaArea* area,
    int map_x,
    int map_z);

/** The packed coord `origin=` holds: level << 28 | x << 14 | z (absolute
 *  tiles), as the cache's coord grid does. */
int32_t
Editor_WmaPackCoord(
    int level,
    int x,
    int z);

/**
 * The text of a NEW area file: a header comment naming the writer, then one
 * area `name` shown as `external`, one type-0 section claiming (and drawn at)
 * squares x0..x1 x z0..z1 on `planes` planes from `min_plane`, its origin at
 * the rectangle's middle tile. snprintf's contract; returns the length.
 */
size_t
Editor_WmaNewAreaText(
    const char* name,
    const char* external,
    int x0,
    int z0,
    int x1,
    int z1,
    int min_plane,
    int planes,
    char* out,
    size_t out_capacity);

#endif
