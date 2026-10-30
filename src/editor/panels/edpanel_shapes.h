#ifndef SRC_EDITOR_PANELS_EDPANEL_SHAPES_H
#define SRC_EDITOR_PANELS_EDPANEL_SHAPES_H

/**
 * Overlay shape thumbnails, sampled from the mesh builder itself
 * (docs/MAPED_DESIGN.html #terrain: "thumbnails from the mesh builder's own
 * table so the picture cannot disagree with the result").
 *
 * Not a copy of world_decode_tile's vertex and face tables: each thumbnail
 * asks world_decode_tile for the tile -- the same call the world builder
 * makes, shape + 1 for an overlaid tile, flat corners -- with the underlay and
 * the overlay in two hues, and samples which hue covers each cell of a
 * grid. Whatever the builder draws is what the palette shows.
 */

/** The `.jm2` overlay shapes, 0..11. */
#define EDITOR_SHAPE_COUNT 12
/** Thumbnail resolution: cells per side. */
#define EDITOR_SHAPE_GRID 8

enum Editor_ShapeCellKind
{
    EDITOR_SHAPE_CELL_NONE = 0,
    EDITOR_SHAPE_CELL_UNDERLAY,
    EDITOR_SHAPE_CELL_OVERLAY,
};

struct Editor_ShapeThumb
{
    /** Row-major, row 0 the NORTH edge, enum Editor_ShapeCellKind. */
    unsigned char cells[EDITOR_SHAPE_GRID * EDITOR_SHAPE_GRID];
    int overlay_cells;
};

/** Sample `shape` (0..11) at `rotation` (0..3). */
void
Editor_ShapeThumbBuild(
    int shape,
    int rotation,
    struct Editor_ShapeThumb* out);

#endif
