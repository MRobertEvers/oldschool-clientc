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
    /* Every stage this kernel names, and it names all of them: no slot below
     * is a NULL that some entry point quietly resolves to the stock one. */
    assert(kernel);
    assert(kernel->draw_model);
    assert(kernel->vtable);
    assert(kernel->projection);
    assert(kernel->face_sort);
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
 * Hand out a prebaked kernel or table with every stage slot filled.
 *
 * A kernel names all three stages, and every stage entry ASSERTS its slot
 * rather than resolving a NULL to the stock one. A slot that quietly means
 * "the default" is how a table whose sort was dropped keeps drawing the right
 * pixels down the wrong path, visible only in a profile -- so the two stages
 * the library chooses for a prebaked object are chosen HERE, at the getter,
 * which is the moment a renderer takes its kernel.
 *
 * That is also why the prebaked objects are mutable statics: the face sort is
 * still a runtime choice (TORIDRAW_FACE_SORT / ToriDraw_FaceSortSetFlat), it
 * is just made once, at a point the caller can see, instead of on every model.
 */
static inline void
toridraw_sd_kernel_publish_one(struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    kernel->projection = ToriDraw_ProjectionKernelGetDefault();
    kernel->face_sort = ToriDraw_FaceCullSortKernelGetDefault();
}

static inline void
toridraw_sd_kernel_publish(struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    toridraw_sd_kernel_publish_one(kernel);
    /*
     * A kernel's depth-tested twin goes out with it.
     *
     * Reading zbuffered_variant IS taking that kernel -- the stage-3 entries
     * swap to it without passing a getter -- so it has to leave here with the
     * same two slots filled, or the swap hands the raster a kernel with a hole
     * in stage 1 and stage 2 that only the depth-flagged models reach.
     *
     * One level, not a walk: a twin sets NEEDS_ZBUFFER and names no twin of
     * its own, which is what the stage-3 resolver asserts.
     */
    if( kernel->zbuffered_variant )
        toridraw_sd_kernel_publish_one(kernel->zbuffered_variant);
}

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
