#ifndef SRC_PLUGIN_TORIRS_PLUGIN_MESH_H
#define SRC_PLUGIN_TORIRS_PLUGIN_MESH_H

/**
 * One mesh a plugin authored, triangle by triangle.
 *
 * Held as the plugin stated it -- vertices, faces, packed HSL, transparency --
 * and not as a ToriDraw_Model, because the two have different lifetimes: the
 * mesh is the plugin's SHAPE and outlives any number of objects standing on
 * it, each of which builds its own model (its own lighting, its own recolours,
 * its own bounds) from these arrays.
 *
 * `revision` moves on every edit. It is what an object's built_mesh_revision
 * is compared against, so re-authoring a mesh rebuilds the objects made from
 * it and re-stating one unchanged rebuilds nothing.
 *
 * The table this lives in, and the handle validation that indexes it, belong
 * to whoever owns the table. What is here is the mesh itself: grow it, free
 * it, and turn one into something drawable.
 */

#include "toridraw_model.h"

#include <stdint.h>

struct ToriRS_PluginMesh
{
    int in_use;
    int revision;
    /* Grown on append rather than sized to TORIRS_PLUGIN_MESH_*_MAX up front:
     * the ceilings are what a plugin may not exceed, not what one costs, and
     * a table of 128 meshes at the maximum would be megabytes that nothing has
     * authored. */
    int vertex_count;
    int vertex_cap;
    int face_count;
    int face_cap;
    int16_t* vertices_x;
    int16_t* vertices_y;
    int16_t* vertices_z;
    int16_t* face_a;
    int16_t* face_b;
    int16_t* face_c;
    uint16_t* face_color;
    uint8_t* face_alpha;
};

/**
 * Grow one half of a mesh to hold at least `want` entries; `faces` selects
 * which half.
 *
 * Every array of that half grows together. One doubling for the whole half
 * rather than a realloc per array per append: they are the same list seen
 * eight ways and always carry the same count.
 */
void
ToriRS_PluginMeshGrow(
    struct ToriRS_PluginMesh* mesh,
    int faces,
    int want);

/** Free every array and zero the mesh, so the slot reads as free. */
void
ToriRS_PluginMeshRelease(struct ToriRS_PluginMesh* mesh);

/**
 * A drawable model over one mesh, unlit and in its bind pose.
 *
 * Everything a cache model gets from its decoder is stated by the plugin --
 * vertices, triangles, a flat colour and a transparency per face -- and the
 * per-vertex a/b/c the rasteriser actually reads are left zeroed for the
 * lighting pass that follows, exactly as ToriDraw_ModelFromToriRS leaves them
 * for a model whose cache copy carried none.
 *
 * The mesh must have geometry; an empty one is a caller that did not check.
 */
struct ToriDraw_Model*
ToriRS_PluginMeshBuildModel(struct ToriRS_PluginMesh const* mesh);

#endif /* SRC_PLUGIN_TORIRS_PLUGIN_MESH_H */
