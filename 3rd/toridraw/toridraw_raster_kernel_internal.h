#ifndef TORIDRAW_RASTER_KERNEL_INTERNAL_H
#define TORIDRAW_RASTER_KERNEL_INTERNAL_H

#include "toridraw_raster_kernel.h"

#include <assert.h>
#include <stdlib.h>

#define TORIDRAW_RASTER_KERNEL_FLAG_MASK                                            \
    (TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |                              \
     TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER)

#ifndef NDEBUG
static inline void
ToriDraw_RasterKernelSDAssertValid(const struct ToriDraw_RasterKernelSD* kernel)
{
    /* A raster kernel names a raster: the whole-model walk and the four face
     * callbacks it dispatches through. Stages 1 and 2 are the TABLE's, and a
     * table's slots are asserted where a table is used. */
    assert(kernel);
    assert(kernel->draw_model);
    assert(kernel->vtable);
    assert((kernel->flags & ~TORIDRAW_RASTER_KERNEL_FLAG_MASK) == 0);
    for( int face_class = 0; face_class < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; face_class++ )
        assert(kernel->vtable->draw[face_class]);
}

static inline void
ToriDraw_RasterKernelHDAssertValid(const struct ToriDraw_RasterKernelHD* kernel)
{
    assert(kernel);
    assert(kernel->vtable);
    assert((kernel->flags & ~TORIDRAW_RASTER_KERNEL_FLAG_MASK) == 0);
    for( int face_class = 0; face_class < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; face_class++ )
        assert(kernel->vtable->draw[face_class]);
}
#else
#define ToriDraw_RasterKernelSDAssertValid(kernel) ((void)(kernel))
#define ToriDraw_RasterKernelHDAssertValid(kernel) ((void)(kernel))
#endif

/*
 * ABLATION SUPPORT (measurement only) -- see the TORIRS_ABL_NOKERNEL arm in
 * ToriDraw_RasterKernelSDDispatch. Read once; off is one predicted branch.
 *
 * Deliberately a RUNTIME knob rather than the compile-time census machinery:
 * TORIDRAW_PROBE_CFLAGS withdraws the asm kernels, and this arm has to keep
 * them, or it measures a different program.
 */
static inline int
toridraw_raster_abl_nokernel(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOKERNEL") ? 1 : 0;
    return armed;
}

/*
 * Hand out a prebaked TABLE with its two front stages filled.
 *
 * A table names all three stages, and every stage entry ASSERTS its slot
 * rather than resolving a NULL to the stock one. A slot that quietly means
 * "the default" is how a table whose sort was dropped keeps drawing the right
 * pixels down the wrong path, visible only in a profile -- so the two stages
 * the library chooses for a prebaked table are chosen HERE, at the getter,
 * which is the moment a renderer takes its kernel.
 *
 * That is also why the prebaked tables are mutable statics: the face sort is
 * still a runtime choice (TORIDRAW_FACE_SORT /
 * ToriDraw_FaceSortSetBitonicRadix), it is just made once, at a point the
 * caller can see, instead of on every model.
 *
 * There is no kernel twin of this any more. A RASTER kernel names a raster;
 * the stage-1 and stage-2 pointers that used to hang off it were the thing
 * the table replaced, and publishing into them was what kept them alive.
 */
static inline void
toridraw_kernel_table_publish(struct ToriDraw_Kernel* table)
{
    assert(table);
    table->projection = ToriDraw_ProjectionKernelGetDefault();
    table->face_sort = ToriDraw_FaceCullSortKernelGetDefault();
}

/* The sole prepared-face dispatch point in each typed raster pipeline. */
static inline void
ToriDraw_RasterKernelSDDispatch(
    const struct ToriDraw_RasterKernelSD* kernel,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    ToriDraw_RasterKernelSDFaceFn function;

    assert(kernel);
    assert(target);
    assert(face);
    assert(face->face_class >= 0 &&
           face->face_class < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT);

    function = kernel->vtable->draw[face->face_class];
    assert(function);
    /* ABLATION (TORIRS_ABL_NOKERNEL=1, measurement only): everything that
     * decides WHAT to draw has run and the prepared face is built; only the
     * fill is withheld. Against the TORIRS_ABL_NOFACES arm this brackets what
     * a staged/batched pipeline could recover. */
    if( toridraw_raster_abl_nokernel() )
        return;
    function(kernel->user_data, target, face);
}

static inline void
ToriDraw_RasterKernelHDDispatch(
    const struct ToriDraw_RasterKernelHD* kernel,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    ToriDraw_RasterKernelHDFaceFn function;

    assert(kernel);
    assert(target);
    assert(face);
    assert(face->face_class >= 0 &&
           face->face_class < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT);

    function = kernel->vtable->draw[face->face_class];
    assert(function);
    function(kernel->user_data, target, face);
}

#endif
