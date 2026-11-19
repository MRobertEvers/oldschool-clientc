#include "ui/chrome_compositor.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

struct ChromeCompositor*
ChromeCompositor_Create(struct ToriPlatform_Renderer_Soft3D* soft)
{
    struct ChromeCompositor* comp;

    assert(soft);
    /* UI: the merged list and the surface keys; ChromeCompositor_Free
     * releases it. */
    comp = ToriMem_Calloc(ToriMem_Ui(), 1, sizeof(*comp));
    comp->soft = soft;
    return comp;
}

void
ChromeCompositor_Free(struct ChromeCompositor* comp)
{
    if( !comp )
        return;
    ToriMem_Free(ToriMem_Ui(), comp->surface_prims);
    comp->surface_prims = NULL;
    ToriMem_Free(ToriMem_Ui(), comp->surface_pixels);
    comp->surface_pixels = NULL;
    comp->surface_pixel_capacity = 0;
    ToriMem_Free(ToriMem_Ui(), comp);
}

struct ToriPlatform_Renderer_Soft3D*
ChromeCompositor_Soft(struct ChromeCompositor const* comp)
{
    assert(comp);
    return comp->soft;
}

bool
ChromeCompositor_MergedFor(
    struct ChromeCompositor const* comp,
    int dbg_serial,
    int win_serial,
    uint32_t panel_revision)
{
    assert(comp);
    /* Exact, not a heuristic: the serials move on every rebuild, including one
     * that changed a string without changing the prim count. */
    return comp->merged_dbg == dbg_serial && comp->merged_win == win_serial &&
           comp->merged_panel == panel_revision;
}

int
ChromeCompositor_MergeLists(
    struct ChromeCompositor* comp,
    struct ToriRSChromePrim const* dbg,
    int dbg_count,
    struct ToriRSChromePrim const* win,
    int win_count)
{
    int n;

    assert(comp);
    n = dbg_count < CHROME_COMPOSITOR_PRIMS_MAX ? dbg_count : CHROME_COMPOSITOR_PRIMS_MAX;
    memcpy(comp->merged, dbg, (size_t)n * sizeof(*dbg));
    if( n < CHROME_COMPOSITOR_PRIMS_MAX )
    {
        int const take = win_count < CHROME_COMPOSITOR_PRIMS_MAX - n
                             ? win_count
                             : CHROME_COMPOSITOR_PRIMS_MAX - n;
        memcpy(&comp->merged[n], win, (size_t)take * sizeof(*win));
        n += take;
    }
    return n;
}

struct ToriRSChromePrim*
ChromeCompositor_MergedSlot(
    struct ChromeCompositor* comp,
    int index)
{
    assert(comp);
    assert(index >= 0);
    assert(index < CHROME_COMPOSITOR_PRIMS_MAX);
    return &comp->merged[index];
}

void
ChromeCompositor_MergeCommit(
    struct ChromeCompositor* comp,
    int count,
    int dbg_serial,
    int win_serial,
    uint32_t panel_revision)
{
    assert(comp);
    assert(count >= 0);
    assert(count <= CHROME_COMPOSITOR_PRIMS_MAX);
    comp->merged_dbg = dbg_serial;
    comp->merged_win = win_serial;
    comp->merged_panel = panel_revision;
    comp->merged_count = count;
}

struct ToriRSChromePrim const*
ChromeCompositor_Merged(
    struct ChromeCompositor const* comp,
    int* out_count)
{
    assert(comp);
    assert(out_count);
    *out_count = comp->merged_count;
    return comp->merged;
}

bool
ChromeCompositor_SurfaceFor(
    struct ChromeCompositor const* comp,
    int win_serial,
    uint32_t panel_revision,
    int width,
    int height)
{
    assert(comp);
    return comp->surface_win == win_serial && comp->surface_panel == panel_revision &&
           comp->surface_w == width && comp->surface_h == height;
}

int
ChromeCompositor_SurfaceLists(
    struct ChromeCompositor* comp,
    struct ToriRSChromePrim const* win,
    int win_count)
{
    int n;

    assert(comp);
    if( !comp->surface_prims )
    {
        /* UI: the page's surface list; ChromeCompositor_Free frees it. */
        comp->surface_prims = ToriMem_Calloc(
            ToriMem_Ui(), CHROME_COMPOSITOR_SURFACE_PRIMS_MAX, sizeof(*comp->surface_prims));
    }
    n = win_count < TORIRS_CHROME_MAX_PRIMS ? win_count : TORIRS_CHROME_MAX_PRIMS;
    if( n > 0 )
        memcpy(comp->surface_prims, win, (size_t)n * sizeof(*win));
    return n;
}

struct ToriRSChromePrim*
ChromeCompositor_SurfaceSlot(
    struct ChromeCompositor* comp,
    int index)
{
    assert(comp);
    assert(comp->surface_prims);
    assert(index >= 0);
    assert(index < CHROME_COMPOSITOR_SURFACE_PRIMS_MAX);
    return &comp->surface_prims[index];
}

struct ToriRSChromePrim const*
ChromeCompositor_SurfacePrims(struct ChromeCompositor const* comp)
{
    assert(comp);
    return comp->surface_prims;
}

uint32_t*
ChromeCompositor_SurfacePixels(
    struct ChromeCompositor* comp,
    int width,
    int height)
{
    size_t pixels;

    assert(comp);
    pixels = (size_t)width * (size_t)height;
    if( pixels > comp->surface_pixel_capacity )
    {
        /* UI: the page's surface picture; ChromeCompositor_Free frees it. */
        uint32_t* grown = ToriMem_Realloc(
            ToriMem_Ui(), comp->surface_pixels, pixels * sizeof(*comp->surface_pixels));
        assert(grown);
        comp->surface_pixels = grown;
        comp->surface_pixel_capacity = pixels;
    }
    return comp->surface_pixels;
}

void
ChromeCompositor_SurfaceCommit(
    struct ChromeCompositor* comp,
    int win_serial,
    uint32_t panel_revision,
    int width,
    int height)
{
    assert(comp);
    comp->surface_win = win_serial;
    comp->surface_panel = panel_revision;
    comp->surface_w = width;
    comp->surface_h = height;
}

void
ChromeCompositor_SurfaceForgetSize(struct ChromeCompositor* comp)
{
    assert(comp);
    comp->surface_w = 0;
    comp->surface_h = 0;
}
