#ifndef TORIDRAW_RASTER_KERNEL_INTERNAL_H
#define TORIDRAW_RASTER_KERNEL_INTERNAL_H

#include "toridraw_raster_kernel.h"

#include <assert.h>

struct ToriDraw_ResolvedRasterSlotSD
{
    ToriDraw_RasterKernelSDFaceFn function;
    void* user_data;
};

struct ToriDraw_ResolvedRasterKernelSD
{
    struct ToriDraw_ResolvedRasterSlotSD slots[TORIDRAW_RASTER_FACE_SD_CLASS_COUNT];
};

struct ToriDraw_ResolvedRasterSlotHD
{
    ToriDraw_RasterKernelHDFaceFn function;
    void* user_data;
};

struct ToriDraw_ResolvedRasterKernelHD
{
    struct ToriDraw_ResolvedRasterSlotHD slots[TORIDRAW_RASTER_FACE_HD_CLASS_COUNT];
};

bool
ToriDraw_RasterKernelSDResolve(
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDraw_ResolvedRasterKernelSD* out);

bool
ToriDraw_RasterKernelHDResolve(
    const struct ToriDraw_RasterKernelHD* kernel,
    struct ToriDraw_ResolvedRasterKernelHD* out);

/* The sole prepared-face dispatch point in each typed raster pipeline. */
static inline void
ToriDraw_RasterKernelSDDispatch(
    const struct ToriDraw_ResolvedRasterKernelSD* kernel,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    const struct ToriDraw_ResolvedRasterSlotSD* slot;

    assert(kernel);
    assert(target);
    assert(face);
    assert(face->face_class >= 0 &&
           face->face_class < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT);

    slot = &kernel->slots[face->face_class];
    assert(slot->function);
    slot->function(slot->user_data, target, face);
}

static inline void
ToriDraw_RasterKernelHDDispatch(
    const struct ToriDraw_ResolvedRasterKernelHD* kernel,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceHD* face)
{
    const struct ToriDraw_ResolvedRasterSlotHD* slot;

    assert(kernel);
    assert(target);
    assert(face);
    assert(face->face_class >= 0 &&
           face->face_class < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT);

    slot = &kernel->slots[face->face_class];
    assert(slot->function);
    slot->function(slot->user_data, target, face);
}

#endif
