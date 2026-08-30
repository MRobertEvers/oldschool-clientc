/* NOT TORIDRAW_RASTER_BATCH_H: that guard belongs to toridraw_raster_batch.h,
 * which this file includes. Sharing it would suppress that header and leave
 * TORIDRAW_RASTER_BATCH undefined on every lane. */
#ifndef TORIDRAW_RASTER_BATCH_DECL_H
#define TORIDRAW_RASTER_BATCH_DECL_H

/*
 * The batched walk's NAME, available before its body.
 *
 * The body (raster.batch.u.c) can only be included once the per-face machinery
 * it falls back to exists, which is most of the way down toridraw_raster.u.c --
 * but the prebaked kernels are assembled before that and have to name it. So
 * the declaration is here and the definition is there, the ordinary split.
 *
 * Off a lane with presorted-run assembly the name resolves to the stock walk.
 * That is what lets every kernel write
 *
 *     .draw_model = toridraw_raster_walk_batched,
 *
 * unconditionally, and it is also correct rather than merely convenient: the
 * whole-model identity check compares draw_model against
 * ToriDraw_RasterWalkPerFace, so a lane with no assembly reports that the
 * branching kernel has no traversal of its own -- and the face sort is told
 * not to fill a stash nothing would read.
 */

#include "toridraw_raster_batch.h"

struct ToriDraw_Scene;
struct ToriDrawModelRasterContext;

#ifdef TORIDRAW_RASTER_BATCH

static void
toridraw_raster_walk_batched(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx);

#else

#define toridraw_raster_walk_batched ToriDraw_RasterWalkPerFace

#endif /* TORIDRAW_RASTER_BATCH */

#endif /* TORIDRAW_RASTER_BATCH_DECL_H */
