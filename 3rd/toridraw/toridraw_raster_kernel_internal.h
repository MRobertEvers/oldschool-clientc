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
    assert(kernel);
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
