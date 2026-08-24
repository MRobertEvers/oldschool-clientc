#ifndef TORIDRAW_RASTER_KERNEL_INTERNAL_H
#define TORIDRAW_RASTER_KERNEL_INTERNAL_H

#include "toridraw_raster_kernel.h"
#include "toridraw_render_context_internal.h"

struct ToriDraw_ResolvedRasterSlot
{
    ToriDraw_RasterKernelFaceFn function;
    void* user_data;
};

struct ToriDraw_ResolvedRasterKernel
{
    struct ToriDraw_ResolvedRasterSlot slots[TORIDRAW_RASTER_FACE_CLASS_COUNT];
};

/* Structural validation used by both the public setter and pass resolution. */
bool
ToriDraw_RasterKernelChainIsValid(const struct ToriDraw_RasterKernel* kernel);

/*
 * Resolve once at model-pass entry.  Domain-incompatible nodes are ignored and
 * each slot retains the user_data of the node that supplied its callback.
 *
 * `terminal` is injected after the explicit borrowed chain.  If that live
 * chain has become invalid since Set, it is discarded wholesale and a
 * diagnostic is emitted before resolving the terminal.  The function returns
 * false only when the terminal itself is invalid or cannot supply all four
 * slots; `out` is then cleared.  It performs no allocation.
 */
bool
ToriDraw_RasterKernelResolve(
    const struct ToriDraw_RasterKernel* kernel,
    const struct ToriDraw_RasterKernel* terminal,
    enum ToriDraw_RasterKernelDomain domain,
    struct ToriDraw_ResolvedRasterKernel* out);

#endif
