#include "toridraw_raster_kernel_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool
toridraw_raster_kernel_sd_chain_is_valid(const struct ToriDraw_RasterKernelSD* kernel)
{
    const struct ToriDraw_RasterKernelSD* slow;
    const struct ToriDraw_RasterKernelSD* fast;
    const struct ToriDraw_RasterKernelSD* node;

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

    for( node = kernel; node; node = node->fallback )
    {
        if( !node->vtable )
            return false;
    }
    return true;
}

static bool
toridraw_raster_kernel_hd_chain_is_valid(const struct ToriDraw_RasterKernelHD* kernel)
{
    const struct ToriDraw_RasterKernelHD* slow;
    const struct ToriDraw_RasterKernelHD* fast;
    const struct ToriDraw_RasterKernelHD* node;

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

    for( node = kernel; node; node = node->fallback )
    {
        if( !node->vtable )
            return false;
    }
    return true;
}

static ToriDraw_RasterKernelSDFaceFn
toridraw_raster_kernel_sd_vtable_slot(
    const struct ToriDraw_RasterKernelSDVTable* vtable,
    int slot)
{
    switch( slot )
    {
    case TORIDRAW_RASTER_FACE_SD_GOURAUD:
        return vtable->draw_gouraud;
    case TORIDRAW_RASTER_FACE_SD_FLAT:
        return vtable->draw_flat;
    case TORIDRAW_RASTER_FACE_SD_TEXTURED:
        return vtable->draw_textured;
    case TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT:
        return vtable->draw_textured_flat;
    default:
        assert(false && "invalid SD raster face class");
        return NULL;
    }
}

static ToriDraw_RasterKernelHDFaceFn
toridraw_raster_kernel_hd_vtable_slot(
    const struct ToriDraw_RasterKernelHDVTable* vtable,
    int slot)
{
    switch( slot )
    {
    case TORIDRAW_RASTER_FACE_HD_GOURAUD:
        return vtable->draw_gouraud;
    case TORIDRAW_RASTER_FACE_HD_FLAT:
        return vtable->draw_flat;
    case TORIDRAW_RASTER_FACE_HD_PLANE:
        return vtable->draw_plane;
    case TORIDRAW_RASTER_FACE_HD_CYLINDER:
        return vtable->draw_cylinder;
    case TORIDRAW_RASTER_FACE_HD_CUBE:
        return vtable->draw_cube;
    case TORIDRAW_RASTER_FACE_HD_SPHERE:
        return vtable->draw_sphere;
    default:
        assert(false && "invalid HD raster face class");
        return NULL;
    }
}

bool
ToriDraw_RasterKernelSDResolve(
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDraw_ResolvedRasterKernelSD* out)
{
    const struct ToriDraw_RasterKernelSD* node;

    if( !out )
        return false;
    memset(out, 0, sizeof(*out));

    if( !toridraw_raster_kernel_sd_chain_is_valid(kernel) )
    {
        fprintf(stderr, "toridraw_raster_kernel: invalid SD kernel chain\n");
        return false;
    }

    for( node = kernel; node; node = node->fallback )
    {
        for( int slot = 0; slot < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; slot++ )
        {
            ToriDraw_RasterKernelSDFaceFn function;

            if( out->slots[slot].function )
                continue;
            function = toridraw_raster_kernel_sd_vtable_slot(node->vtable, slot);
            if( function )
            {
                out->slots[slot].function = function;
                out->slots[slot].user_data = node->user_data;
            }
        }
    }

    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; slot++ )
    {
        if( !out->slots[slot].function )
        {
            fprintf(stderr, "toridraw_raster_kernel: unresolved SD face slot %d\n", slot);
            memset(out, 0, sizeof(*out));
            return false;
        }
    }
    return true;
}

bool
ToriDraw_RasterKernelHDResolve(
    const struct ToriDraw_RasterKernelHD* kernel,
    struct ToriDraw_ResolvedRasterKernelHD* out)
{
    const struct ToriDraw_RasterKernelHD* node;

    if( !out )
        return false;
    memset(out, 0, sizeof(*out));

    if( !toridraw_raster_kernel_hd_chain_is_valid(kernel) )
    {
        fprintf(stderr, "toridraw_raster_kernel: invalid HD kernel chain\n");
        return false;
    }

    for( node = kernel; node; node = node->fallback )
    {
        for( int slot = 0; slot < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; slot++ )
        {
            ToriDraw_RasterKernelHDFaceFn function;

            if( out->slots[slot].function )
                continue;
            function = toridraw_raster_kernel_hd_vtable_slot(node->vtable, slot);
            if( function )
            {
                out->slots[slot].function = function;
                out->slots[slot].user_data = node->user_data;
            }
        }
    }

    for( int slot = 0; slot < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; slot++ )
    {
        if( !out->slots[slot].function )
        {
            fprintf(stderr, "toridraw_raster_kernel: unresolved HD face slot %d\n", slot);
            memset(out, 0, sizeof(*out));
            return false;
        }
    }
    return true;
}
