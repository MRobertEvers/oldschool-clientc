#ifndef TORIDRAW_RASTER_KERNEL_INTERNAL_H
#define TORIDRAW_RASTER_KERNEL_INTERNAL_H

#include "toridraw_raster_kernel.h"

#include <assert.h>

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
