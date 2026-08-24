#include "toridraw_raster_kernel_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TORIDRAW_RASTER_KERNEL_ALL_DOMAINS                                                         \
    ((unsigned int)TORIDRAW_RASTER_KERNEL_STOCK | (unsigned int)TORIDRAW_RASTER_KERNEL_HD)

static bool
toridraw_raster_kernel_node_is_valid(const struct ToriDraw_RasterKernel* kernel)
{
    return kernel && kernel->vtable && kernel->domains != 0 &&
           (kernel->domains & ~TORIDRAW_RASTER_KERNEL_ALL_DOMAINS) == 0;
}

bool
ToriDraw_RasterKernelChainIsValid(const struct ToriDraw_RasterKernel* kernel)
{
    const struct ToriDraw_RasterKernel* slow;
    const struct ToriDraw_RasterKernel* fast;
    const struct ToriDraw_RasterKernel* node;

    if( !kernel )
        return false;

    slow = kernel;
    fast = kernel;
    while( fast && fast->fallback )
    {
        slow = slow->fallback;
        fast = fast->fallback->fallback;
        if( slow == fast )
            return false;
    }

    /* Check the complete chain, including domain-incompatible nodes: skipping
     * one for this pass must never hide a malformed borrowed object.  This walk
     * is safe to terminate now that the cycle check has succeeded. */
    for( node = kernel; node; node = node->fallback )
    {
        if( !toridraw_raster_kernel_node_is_valid(node) )
            return false;
    }

    return true;
}

static ToriDraw_RasterKernelFaceFn
toridraw_raster_kernel_vtable_slot(
    const struct ToriDraw_RasterKernelVTable* vtable,
    int slot)
{
    switch( slot )
    {
    case TORIDRAW_RASTER_FACE_GOURAUD:
        return vtable->draw_gouraud;
    case TORIDRAW_RASTER_FACE_FLAT:
        return vtable->draw_flat;
    case TORIDRAW_RASTER_FACE_TEXTURED:
        return vtable->draw_textured;
    case TORIDRAW_RASTER_FACE_TEXTURED_FLAT:
        return vtable->draw_textured_flat;
    default:
        assert(false && "invalid raster face class");
        return NULL;
    }
}

static void
toridraw_raster_kernel_resolve_chain(
    const struct ToriDraw_RasterKernel* kernel,
    enum ToriDraw_RasterKernelDomain domain,
    struct ToriDraw_ResolvedRasterKernel* out)
{
    for( ; kernel; kernel = kernel->fallback )
    {
        if( (kernel->domains & (unsigned int)domain) == 0 )
            continue;

        for( int slot = 0; slot < TORIDRAW_RASTER_FACE_CLASS_COUNT; slot++ )
        {
            ToriDraw_RasterKernelFaceFn function;

            if( out->slots[slot].function )
                continue;
            function = toridraw_raster_kernel_vtable_slot(kernel->vtable, slot);
            if( function )
            {
                out->slots[slot].function = function;
                out->slots[slot].user_data = kernel->user_data;
            }
        }
    }
}

static bool
toridraw_raster_kernel_domain_is_single(enum ToriDraw_RasterKernelDomain domain)
{
    return domain == TORIDRAW_RASTER_KERNEL_STOCK || domain == TORIDRAW_RASTER_KERNEL_HD;
}

bool
ToriDraw_RasterKernelResolve(
    const struct ToriDraw_RasterKernel* kernel,
    const struct ToriDraw_RasterKernel* terminal,
    enum ToriDraw_RasterKernelDomain domain,
    struct ToriDraw_ResolvedRasterKernel* out)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));

    if( !toridraw_raster_kernel_domain_is_single(domain) )
    {
        fprintf(stderr, "toridraw_raster_kernel: invalid pass domain 0x%x\n", (unsigned)domain);
        return false;
    }
    if( !ToriDraw_RasterKernelChainIsValid(terminal) )
    {
        fprintf(stderr, "toridraw_raster_kernel: invalid terminal chain\n");
        return false;
    }

    if( kernel && !ToriDraw_RasterKernelChainIsValid(kernel) )
    {
        /* A borrowed chain may have been mutated after Set.  Never combine a
         * partly resolved bad chain with the terminal: discard it wholesale. */
        fprintf(stderr, "toridraw_raster_kernel: invalid live scene chain; using terminal\n");
        kernel = NULL;
    }

    if( kernel )
        toridraw_raster_kernel_resolve_chain(kernel, domain, out);
    toridraw_raster_kernel_resolve_chain(terminal, domain, out);

    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_CLASS_COUNT; slot++ )
    {
        if( !out->slots[slot].function )
        {
            fprintf(
                stderr,
                "toridraw_raster_kernel: terminal did not resolve face slot %d "
                "for domain 0x%x\n",
                slot,
                (unsigned)domain);
            memset(out, 0, sizeof(*out));
            return false;
        }
    }

    return true;
}

bool
ToriDraw_SceneSetRasterKernel(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernel* kernel)
{
    if( !scene || !kernel || ToriDraw_RenderContextIsActive(scene) ||
        !ToriDraw_RasterKernelChainIsValid(kernel) )
        return false;

    scene->raster_kernel = kernel;
    return true;
}

bool
ToriDraw_SceneResetRasterKernel(struct ToriDraw_Scene* scene)
{
    if( !scene || ToriDraw_RenderContextIsActive(scene) )
        return false;

    scene->raster_kernel = NULL;
    return true;
}

const struct ToriDraw_RasterKernel*
ToriDraw_SceneGetRasterKernel(const struct ToriDraw_Scene* scene)
{
    return scene ? scene->raster_kernel : NULL;
}

#undef TORIDRAW_RASTER_KERNEL_ALL_DOMAINS
