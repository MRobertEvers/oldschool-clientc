#ifndef SRC_UI_CHROME_COMPOSITOR_H
#define SRC_UI_CHROME_COMPOSITOR_H

/*
 * The chrome display lists a frame hands downstream, and the picture of the
 * plugin page when it sits on a surface of its own.
 *
 * The MERGED list is the developer chrome's primitives, then the plugin
 * window's, then the panel's custom-well primitives converted to chrome ones,
 * cut where CHROME_COMPOSITOR_PRIMS_MAX is reached. It is rebuilt only when
 * the developer chrome's build serial, the plugin window's build serial or the
 * custom wells' revision differs from what it was last built for, so a steady
 * frame hands out the same array and count without copying anything.
 *
 * The SURFACE is the plugin page drawn on a surface of its own (the strip
 * beside the rail, or a pop-out window): its own list -- the window's
 * primitives cut at TORIRS_CHROME_MAX_PRIMS, then the wells' -- and a pixel
 * buffer it is rasterised into. It is redrawn only when the window's build
 * serial, the wells' revision, or the surface's width or height moves.
 * Forgetting the size makes the next present redraw. The list is allocated at
 * its full size on first use; the pixel buffer grows to exactly width *
 * height when a surface needs more than it holds and never shrinks.
 *
 * It also holds the software renderer that rasterises plugin chrome, which it
 * borrows: the caller makes it before Create and frees it before Free.
 *
 * It lives on the heap, in the UI group, and exists for as long as the App
 * does: ChromeCompositor_Create hands back a zeroed record, and
 * ChromeCompositor_Free releases it with the two surface buffers it grew.
 *
 * Nothing here reads a chrome instance, the panel's wells or the plugin
 * window, draws, or prints. The caller asks for the serials and the revision
 * it compares, decides whether there is anything to merge or a surface to
 * present at all, converts each well primitive into the slot it is handed,
 * and does the rasterising and the presenting.
 */

#include "render/panel_overlay_stage.h"
#include "ui/uitree_debug_overlay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Room for both chrome instances plus converted panel-local primitives. */
#define CHROME_COMPOSITOR_PRIMS_MAX (2 * TORIRS_CHROME_MAX_PRIMS + PANEL_OVERLAY_STAGE_MAX)
/** Room for the plugin window's primitives plus its wells', on a surface. */
#define CHROME_COMPOSITOR_SURFACE_PRIMS_MAX (TORIRS_CHROME_MAX_PRIMS + PANEL_OVERLAY_STAGE_MAX)

struct ToriPlatform_Renderer_Soft3D;

struct ChromeCompositor
{
    /**
     * The two chrome instances' display lists, concatenated for the emit layer
     * -- which takes one pointer and one count for the whole overlay. Sized
     * for both instances at once because that is the worst case it has to
     * hold.
     */
    struct ToriRSChromePrim merged[CHROME_COMPOSITOR_PRIMS_MAX];
    int merged_count;
    /** The build serials and wells revision the merge was made from. */
    int merged_dbg;
    int merged_win;
    uint32_t merged_panel;

    /**
     * Rasterises retained plugin chrome -- CUSTOM well bitmaps and the
     * surface page -- without disturbing the game canvas's renderer.
     * Borrowed: the caller makes and frees it.
     */
    struct ToriPlatform_Renderer_Soft3D* soft;

    /** The surface page's display list, CHROME_COMPOSITOR_SURFACE_PRIMS_MAX
     *  long once allocated. */
    struct ToriRSChromePrim* surface_prims;
    /** The surface page's picture, `surface_pixel_capacity` pixels. */
    uint32_t* surface_pixels;
    size_t surface_pixel_capacity;
    /** What the surface picture was last drawn for. */
    int surface_win;
    uint32_t surface_panel;
    int surface_w;
    int surface_h;
};

/** A zeroed compositor from the UI group, borrowing `soft`. Never NULL. */
struct ChromeCompositor*
ChromeCompositor_Create(struct ToriPlatform_Renderer_Soft3D* soft);

/** Release the compositor and its surface buffers, not the renderer it
 *  borrows. NULL is accepted. */
void
ChromeCompositor_Free(struct ChromeCompositor* comp);

/** The plugin chrome's software renderer. */
struct ToriPlatform_Renderer_Soft3D*
ChromeCompositor_Soft(struct ChromeCompositor const* comp);

/* ---- the merged list ---------------------------------------------------- */

/** Whether the merged list was built from exactly these serials and this
 *  wells revision. */
bool
ChromeCompositor_MergedFor(
    struct ChromeCompositor const* comp,
    int dbg_serial,
    int win_serial,
    uint32_t panel_revision);

/**
 * Start a rebuild: copy the developer chrome's primitives, then as many of
 * the plugin window's as still fit.
 *
 * @return how many primitives the list now holds; the caller appends the
 * wells' from there with ChromeCompositor_MergedSlot.
 */
int
ChromeCompositor_MergeLists(
    struct ChromeCompositor* comp,
    struct ToriRSChromePrim const* dbg,
    int dbg_count,
    struct ToriRSChromePrim const* win,
    int win_count);

/**
 * Append `count` primitives to the merged list from index `at` (as many as
 * still fit). The plugin window's overlay prims go here, AFTER the wells, so
 * an open dropdown list is drawn over the well beside its button.
 *
 * @return how many the list now holds.
 */
int
ChromeCompositor_MergeAppend(
    struct ChromeCompositor* comp,
    int at,
    struct ToriRSChromePrim const* prims,
    int count);

/** Slot `index` of the merged list, `index` below CHROME_COMPOSITOR_PRIMS_MAX. */
struct ToriRSChromePrim*
ChromeCompositor_MergedSlot(
    struct ChromeCompositor* comp,
    int index);

/** End a rebuild: the list holds `count` primitives, built for these. */
void
ChromeCompositor_MergeCommit(
    struct ChromeCompositor* comp,
    int count,
    int dbg_serial,
    int win_serial,
    uint32_t panel_revision);

/** The merged list as last committed. */
struct ToriRSChromePrim const*
ChromeCompositor_Merged(
    struct ChromeCompositor const* comp,
    int* out_count);

/* ---- the surface page --------------------------------------------------- */

/** Whether the surface picture was drawn for exactly this window serial,
 *  wells revision and size. */
bool
ChromeCompositor_SurfaceFor(
    struct ChromeCompositor const* comp,
    int win_serial,
    uint32_t panel_revision,
    int width,
    int height);

/**
 * Start the surface page's list: allocate it on first use and copy the
 * plugin window's primitives, at most TORIRS_CHROME_MAX_PRIMS of them.
 *
 * @return how many it now holds; the caller appends the wells' with
 * ChromeCompositor_SurfaceSlot.
 */
int
ChromeCompositor_SurfaceLists(
    struct ChromeCompositor* comp,
    struct ToriRSChromePrim const* win,
    int win_count);

/** ChromeCompositor_MergeAppend for the surface list (which must be
 *  allocated): the window's overlay prims after the wells. */
int
ChromeCompositor_SurfaceAppend(
    struct ChromeCompositor* comp,
    int at,
    struct ToriRSChromePrim const* prims,
    int count);

/** Slot `index` of the surface list, `index` below
 *  CHROME_COMPOSITOR_SURFACE_PRIMS_MAX. The list must be allocated. */
struct ToriRSChromePrim*
ChromeCompositor_SurfaceSlot(
    struct ChromeCompositor* comp,
    int index);

/** The surface list; NULL before the first ChromeCompositor_SurfaceLists. */
struct ToriRSChromePrim const*
ChromeCompositor_SurfacePrims(struct ChromeCompositor const* comp);

/**
 * The surface picture, grown to exactly `width * height` pixels when it holds
 * fewer. Its contents are whatever the last present left.
 */
uint32_t*
ChromeCompositor_SurfacePixels(
    struct ChromeCompositor* comp,
    int width,
    int height);

/** The surface picture is now drawn for these. */
void
ChromeCompositor_SurfaceCommit(
    struct ChromeCompositor* comp,
    int win_serial,
    uint32_t panel_revision,
    int width,
    int height);

/** Forget the surface's size, so the next present draws whatever the keys. */
void
ChromeCompositor_SurfaceForgetSize(struct ChromeCompositor* comp);

#endif
