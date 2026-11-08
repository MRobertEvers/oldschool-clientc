#ifndef SRC_EDITOR_PANELS_EDPANEL_REGION_H
#define SRC_EDITOR_PANELS_EDPANEL_REGION_H

/**
 * Region painting as COMMANDS (editor/editor_region.h is the planner): the
 * Region brush, the Lasso and Tidy turn a coverage grid into TILE commands
 * on the `o` token (the overlay, shaped to the region's edge) or the `u`
 * token (the underlay, its rim dithered). Built against a document, never
 * applied here, as edpanel_ops.h builds the brushes' and the path's.
 *
 * Coordinates are ABSOLUTE world tiles. A tile whose square is not open is
 * left out and counted.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_region.h"

/** What one region edit did, for the status line and the preview. */
struct Editor_RegionStats
{
    /** Tiles the plan touched (an overlay plan: edge shapes among them). */
    int planned;
    int edges;
    /** Tiles whose best shape still misses their coverage by more than
     *  EDITOR_REGION_POOR_FIT samples. */
    int poor;
    /** Tiles the plan takes the overlay off. */
    int cleared;
    /** Tiles left out: their square is not open here. */
    int closed;
    /** Changes left out because the commands ran out of room. */
    int dropped;
};

/** Editor_RegionTileFn over the document: what world tile (x, z) on `level`
 *  holds against `overlay_id` (enum Editor_RegionHas). */
struct Editor_RegionDocTiles
{
    struct Editor_Doc* doc;
    int level;
    int overlay_id;
};

int
Editor_RegionDocTile(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation);

/**
 * The overlay plan of `grid` against the document: `overlay_id` on the
 * tiles the region covers, shaped (ADD), taken off them (SUBTRACT), or the
 * grid made the overlay over its span (REPLACE). Writes up to `max` tiles;
 * returns the count. `stats` may be NULL.
 */
int
Editor_RegionOverlayPlan(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_RegionParams* params,
    int overlay_id,
    const struct Editor_RegionGrid* grid,
    struct Editor_RegionTile* out,
    int max,
    struct Editor_RegionStats* stats);

/**
 * The TILE commands the overlay plan makes: one per tile it changes. Returns
 * the count, at most `max`. `stats` may be NULL.
 */
int
Editor_RegionOverlayCommands(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_RegionParams* params,
    int overlay_id,
    const struct Editor_RegionGrid* grid,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats);

/**
 * The TILE commands that paint `underlay_id` over the grid's region, the rim
 * dithered (Editor_RegionDither: `feather` tiles, `seed`). Returns the count,
 * at most `max`. `stats` may be NULL.
 */
int
Editor_RegionUnderlayCommands(
    struct Editor_Doc* doc,
    int level,
    int underlay_id,
    const struct Editor_RegionGrid* grid,
    int feather,
    uint32_t seed,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats);

/** Tidy: one run of an overlay, read back and smoothed. */
struct Editor_RegionTidy
{
    /** The smoothed coverage over the run's box (and a margin). */
    struct Editor_RegionGrid grid;
    /** Per grid tile: 1 when it is in the run. A tile of the same overlay
     *  outside the run is another run's, and the plan leaves it alone. */
    unsigned char* member;
    int overlay_id;
    int level;
    /** Tiles in the run. */
    int run;
};

/**
 * Tidy's grid: the run of tiles holding the overlay world tile (x, z) holds
 * on `level`, its coverage read back and smoothed by `smooth_halves` half
 * tiles at `hardness`. Returns 1 with `tidy` filled (free it with
 * Editor_RegionTidyFree), 0 when the tile holds no overlay the planner can
 * read or its square is not open, -1 when the run is wider than a plan
 * takes (tidy->run says how far the flood got).
 */
int
Editor_RegionTidyBegin(
    struct Editor_Doc* doc,
    int level,
    int world_x,
    int world_z,
    int smooth_halves,
    int hardness,
    struct Editor_RegionTidy* tidy);

void
Editor_RegionTidyFree(struct Editor_RegionTidy* tidy);

/** The run refitted (REPLACE over the run): up to `max` tiles. */
int
Editor_RegionTidyPlan(
    struct Editor_Doc* doc,
    const struct Editor_RegionTidy* tidy,
    enum Editor_RegionSet set,
    int hardness,
    struct Editor_RegionTile* out,
    int max,
    struct Editor_RegionStats* stats);

/** The TILE commands of the refit: one per tile it changes. */
int
Editor_RegionTidyCommands(
    struct Editor_Doc* doc,
    const struct Editor_RegionTidy* tidy,
    enum Editor_RegionSet set,
    int hardness,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats);

#endif
