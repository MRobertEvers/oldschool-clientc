#include "edpanel_shapes.h"

#include "engine/world_builder/world_decode_tile.h"
#include "toridraw_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <string.h>

/* Two hues the builder will not confuse: lightness is the only thing it
 * adjusts (terrain_adjust_lightness), so the hue bits say which colour a face
 * carries. hsl16 = hue << 10 | saturation << 7 | lightness. */
#define SHAPE_HUE_UNDERLAY 8
#define SHAPE_HUE_OVERLAY 40
#define SHAPE_HSL(hue) (((hue) << 10) | (5 << 7) | 64)

static long
shape_cross(
    long ax,
    long az,
    long bx,
    long bz,
    long px,
    long pz)
{
    return (bx - ax) * (pz - az) - (bz - az) * (px - ax);
}

static int
shape_in_triangle(
    long ax,
    long az,
    long bx,
    long bz,
    long cx,
    long cz,
    long px,
    long pz)
{
    long const d1 = shape_cross(ax, az, bx, bz, px, pz);
    long const d2 = shape_cross(bx, bz, cx, cz, px, pz);
    long const d3 = shape_cross(cx, cz, ax, az, px, pz);
    int const has_neg = d1 < 0 || d2 < 0 || d3 < 0;
    int const has_pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(has_neg && has_pos);
}

void
Editor_ShapeThumbBuild(
    int shape,
    int rotation,
    struct Editor_ShapeThumb* out)
{
    struct ToriDraw_Model* model;
    long min_x;
    long min_z;
    long max_x;
    long max_z;

    assert(out);
    assert(shape >= 0);
    assert(shape < EDITOR_SHAPE_COUNT);
    assert(rotation >= 0);
    assert(rotation < 4);

    memset(out, 0, sizeof(*out));

    /* shape + 1: the builder's index 0 is the plain, overlay-less tile, which
     * is how world_terrain's shapemap spells a tile with no overlay. */
    model = world_decode_tile(
        shape + 1, rotation, -1, 0, 0, 0, 0, 96, 96, 96, 96, SHAPE_HSL(SHAPE_HUE_UNDERLAY),
        SHAPE_HSL(SHAPE_HUE_OVERLAY));
    assert(model);
    assert(model->vertex_count > 0);

    min_x = max_x = model->vertices_x[0];
    min_z = max_z = model->vertices_z[0];
    for( int v = 1; v < model->vertex_count; v++ )
    {
        if( model->vertices_x[v] < min_x )
            min_x = model->vertices_x[v];
        if( model->vertices_x[v] > max_x )
            max_x = model->vertices_x[v];
        if( model->vertices_z[v] < min_z )
            min_z = model->vertices_z[v];
        if( model->vertices_z[v] > max_z )
            max_z = model->vertices_z[v];
    }

    for( int row = 0; row < EDITOR_SHAPE_GRID; row++ )
    {
        for( int col = 0; col < EDITOR_SHAPE_GRID; col++ )
        {
            /* Cell centres, in the model's units; row 0 is the north edge. */
            long const px = min_x + ((2L * col + 1) * (max_x - min_x)) / (2L * EDITOR_SHAPE_GRID);
            long const pz = max_z - ((2L * row + 1) * (max_z - min_z)) / (2L * EDITOR_SHAPE_GRID);
            unsigned char kind = EDITOR_SHAPE_CELL_NONE;

            for( int f = 0; f < model->face_count; f++ )
            {
                int const a = model->face_indices_a[f];
                int const b = model->face_indices_b[f];
                int const c = model->face_indices_c[f];
                int hue;

                if( model->face_colors_c[f] == TORIDRAWHSL16_HIDDEN )
                    continue;
                if( !shape_in_triangle(
                        model->vertices_x[a], model->vertices_z[a], model->vertices_x[b],
                        model->vertices_z[b], model->vertices_x[c], model->vertices_z[c], px, pz) )
                    continue;
                hue = ((int)model->face_colors_a[f] >> 10) & 0x3f;
                kind = hue == SHAPE_HUE_OVERLAY ? EDITOR_SHAPE_CELL_OVERLAY
                                                : EDITOR_SHAPE_CELL_UNDERLAY;
                /* Later faces are drawn over earlier ones. */
            }
            out->cells[row * EDITOR_SHAPE_GRID + col] = kind;
            if( kind == EDITOR_SHAPE_CELL_OVERLAY )
                out->overlay_cells++;
        }
    }
    ToriDraw_ModelFree(model);
}
