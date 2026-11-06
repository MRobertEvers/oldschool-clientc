#ifndef SRC_EDITOR_EDITOR_INSTANCE_H
#define SRC_EDITOR_EDITOR_INSTANCE_H

/**
 * The instance helper's planner (workbench 6.5, design section 13): an
 * instance is a set of squares at coordinates no world-map area claims (the
 * instance space, TORIRSMAPED_INSTANCE_X0..X1 x Z0..Z1 = 0..19 x 100..255)
 * plus whatever script brings a player there. The dialog
 * (panels/edpanel_instance.c) gathers what the user chose; this file answers
 * the questions that have one answer and no document behind them:
 *
 *   where       the source's bounding box, and the first free rectangle of
 *               its size in instance space: no square in ANY storage, scanned
 *               from the lowest free coordinates -- z from Z0 upward, x from
 *               X0 upward within a row, the first fit wins (so two squares
 *               stacked north-south land at the bottom-left of the space);
 *   rebasing    a source square's key, an absolute tile, a spawn row, a coord
 *               to the destination: every source square moves by the same
 *               whole-square offset, so a tile keeps its local (x, z) and its
 *               level, and a level outside the copied range is dropped;
 *   the copy    the commands that turn a DOC_CREATE'd square into what the
 *               "Copy" row asked for (terrain off: the locs added onto an
 *               empty square; locs off: the copy's locs removed; a level
 *               range: the other levels' tiles and locs cleared);
 *   the entry   the trigger script's text: an [oploc1,<loc>] that
 *               p_telejump's to the destination coord, or one that builds a
 *               runtime map instance of the destination first.
 *
 * Pure: no document, no host, no chrome. NULL arguments assert.
 */

#include "editor_cmd.h"
#include "editor_host.h"
#include "editor_spawn.h"
#include "editor_types.h"
#include "editor_wma.h"

#include <stddef.h>

/** Squares one instance copies (a 4 x 4 block, or 16 scattered). */
#define EDITOR_INSTANCE_SQUARES_MAX 16
/** An instance's name: the .wma name, the trigger's directory, the
 *  collection; [a-z0-9_], what all three take. */
#define EDITOR_INSTANCE_NAME_MAX EDITOR_WMA_NAME_MAX
/** A loc name the trigger's subject takes: [a-z0-9_]. */
#define EDITOR_INSTANCE_LOC_MAX 64
/** "3_255_255_63_63" and the terminator. */
#define EDITOR_INSTANCE_COORD_MAX 24

/** The instance space, in map squares (design section 13). Equal to the
 *  catalog's TORIRSMAPED_INSTANCE_* (torirs_maped_catalog.h). */
#define EDITOR_INSTANCE_X0 0
#define EDITOR_INSTANCE_X1 19
#define EDITOR_INSTANCE_Z0 100
#define EDITOR_INSTANCE_Z1 255

/** What the "Copy" row copies. */
enum
{
    EDITOR_INSTANCE_COPY_TERRAIN = 1,
    EDITOR_INSTANCE_COPY_LOCS = 2,
    EDITOR_INSTANCE_COPY_SPAWNS = 4,
    /** The AREAS document's named areas inside the source, rebased. */
    EDITOR_INSTANCE_COPY_AREAS = 8,
};

/** How a player gets in. */
enum Editor_InstanceEntry
{
    /** [oploc1,<loc>] p_telejump(<coord>). */
    EDITOR_INSTANCE_ENTRY_TELEPORT = 0,
    /** The same trigger building a runtime copy of the destination with the
     *  map_instance_* commands first (docs/map_instances.md). */
    EDITOR_INSTANCE_ENTRY_RUNTIME,
    /** No script. */
    EDITOR_INSTANCE_ENTRY_NONE,
};

struct Editor_InstanceSquare
{
    int map_x;
    int map_z;
    /** The source's full key ("m50_50", "ported/<lane>/maps/m50_50"). */
    char key[EDITOR_DOC_KEY_MAX];
};

struct Editor_InstancePlan
{
    int count;
    struct Editor_InstanceSquare sources[EDITOR_INSTANCE_SQUARES_MAX];
    /** The sources' bounding box, squares: [src_x0, src_x0 + w) x
     *  [src_z0, src_z0 + h). */
    int src_x0;
    int src_z0;
    int w;
    int h;
    /** Where the box's south-west square lands. */
    int dst_x0;
    int dst_z0;
    /** The levels copied, inclusive (0..3 for all). */
    int level_lo;
    int level_hi;
};

/** [a-z0-9_], 1..47 (Editor_WmaNameValid): an instance name. */
int
Editor_InstanceNameValid(const char* name);

/** [a-z0-9_], 1..63: a loc's config name, the trigger's subject. */
int
Editor_InstanceLocNameValid(const char* name);

/**
 * Start a plan over `count` source squares (`keys`, full keys; at least one,
 * at most EDITOR_INSTANCE_SQUARES_MAX): their coordinates, ordered south-west
 * first (z, then x), their bounding box, all levels, the destination at the
 * box itself (no move). Returns 0
 * when a key is not a square key or one place is named twice.
 */
int
Editor_InstancePlanInit(
    struct Editor_InstancePlan* plan,
    const char* const* keys,
    int count);

/** Whether square (map_x, map_z) is in the instance space. */
int
Editor_InstanceInSpace(
    int map_x,
    int map_z);

/**
 * The first free w x h rectangle of squares in instance space, scanned from
 * the lowest coordinates (z rows from Z0 upward, x from X0 upward in a row):
 * no square of `taken` (count (map_x, map_z) pairs, any storage) inside it.
 * Returns 1 and fills (*out_x, *out_z), its south-west square; 0 when none
 * fits.
 */
int
Editor_InstanceFindFree(
    const int* taken,
    int count,
    int w,
    int h,
    int* out_x,
    int* out_z);

/** Squares of instance space no pair of `taken` names. */
int
Editor_InstanceFreeCount(
    const int* taken,
    int count);

/** Source square `index`'s destination square. */
void
Editor_InstanceDest(
    const struct Editor_InstancePlan* plan,
    int index,
    int* out_map_x,
    int* out_map_z);

/** Index of the source square holding absolute tile (x, z), or -1. */
int
Editor_InstanceSourceOfTile(
    const struct Editor_InstancePlan* plan,
    int abs_x,
    int abs_z);

/** An absolute tile of a source square, moved to the destination. 0 when the
 *  tile is in no source square. */
int
Editor_InstanceRebaseTile(
    const struct Editor_InstancePlan* plan,
    int abs_x,
    int abs_z,
    int* out_x,
    int* out_z);

/** A spawn row of a source square, moved: 0 (nothing written) when it stands
 *  outside every source square, on a level the plan does not copy, or is a
 *  suppression row (a suppression names a generated row, which the copy does
 *  not have). */
int
Editor_InstanceRebaseSpawn(
    const struct Editor_InstancePlan* plan,
    const struct Editor_SpawnRow* row,
    struct Editor_SpawnRow* out);

/** "<level>_<mx>_<mz>_<lx>_<lz>", the script's coord literal. */
void
Editor_InstanceCoordText(
    int level,
    int abs_x,
    int abs_z,
    char out[EDITOR_INSTANCE_COORD_MAX]);

/** Parse a coord literal (or "x z [level]" absolute tiles). 1 on success. */
int
Editor_InstanceCoordParse(
    const char* text,
    int* out_level,
    int* out_x,
    int* out_z);

/** The default entry tile: the middle of the first source square's
 *  destination, on the lowest copied level. */
void
Editor_InstanceEntryTile(
    const struct Editor_InstancePlan* plan,
    int* out_level,
    int* out_x,
    int* out_z);

/**
 * The commands that make `created` -- a square DOC_CREATE just made at
 * `created->map_x, map_z` with the template Editor_InstanceTemplate chose
 * -- what `copy` and the plan's levels ask for. `source` is the square it
 * was copied from (read only when the locs are added onto an empty square;
 * may be NULL otherwise). Writes at most `max` (out NULL to count) and
 * returns how many are needed.
 */
int
Editor_InstanceCopyCommands(
    const struct Editor_InstancePlan* plan,
    unsigned copy,
    const struct Editor_Square* created,
    const struct Editor_Square* source,
    struct Editor_Cmd* out,
    int max);

/** The DOC_CREATE template `copy` needs: COPY when the terrain is copied
 *  (the locs, if not wanted, are removed after), else EMPTY. */
enum Editor_DocTemplate
Editor_InstanceTemplate(unsigned copy);

/** "rs2:edited/<name>/scripts/<name>.rs2": the trigger's TEXT key (the file
 *  server/scripts/edited/<name>/scripts/<name>.rs2, a directory the helper
 *  owns). */
void
Editor_InstanceTriggerKey(
    const char* name,
    char out[EDITOR_DOC_KEY_MAX]);

/**
 * The trigger script: a header naming the writer, then
 * `[oploc1,<loc>]` and either `p_telejump(<coord>);` (TELEPORT) or the
 * map_instance_alloc / setchunk / build / coord sequence over the plan's
 * destination squares, then the telejump into it (RUNTIME; the destination
 * box must be at most 2 x 2 squares, the commands' 16 x 16 zones).
 * (level, x, z) is the destination coord. snprintf's contract.
 */
size_t
Editor_InstanceTriggerText(
    const struct Editor_InstancePlan* plan,
    const char* name,
    const char* loc,
    enum Editor_InstanceEntry entry,
    int level,
    int x,
    int z,
    char* out,
    size_t out_capacity);

#endif
