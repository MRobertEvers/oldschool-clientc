#ifndef SRC_UI_DEV_OVERLAY_H
#define SRC_UI_DEV_OVERLAY_H

/*
 * The developer overlay: the in-canvas chrome instance every developer panel
 * is built in, and the two All Settings pickers that are panels in it.
 *
 * The chrome instance belongs to this record. The frame-time panel (a
 * side-app, app/app_tool_frame_time.c), the loc editor's panel (a side-app,
 * app/app_tool_loc_editor.c) and the map editor's panels
 * (editor/map_editor_app.h) are built in it too, but each holds
 * only the widget ids it was handed back: the App unit passes `&overlay->ui`
 * to every call that adds, reads or shows a panel, and the plugin window's
 * instance is copied from it once at init.
 *
 * It lives on the heap, in the UI group, and exists for as long as the App
 * does: DevOverlay_Create hands back a zeroed record with no chrome
 * initialised and the model-view camera reset, and DevOverlay_Free releases
 * it.
 *
 * This decides what the frame-time row reads for a mean: milliseconds to two
 * decimals, or "--" when the mean is 0.
 *
 * Nothing here initialises, builds or draws the chrome, reads input or a
 * clock, or prints. The caller does the chrome work through the fields below.
 */

#include "editor/editor_preview_camera.h"
#include "ui/settings_pickers.h"
#include "ui/uitree_debug_overlay.h"

#include <stddef.h>
#include <stdint.h>

struct DevOverlay
{
    /**
     * The developer chrome instance. The frame-time panel, the loc editor,
     * the map editor and the settings pickers are panels here; a hidden panel
     * builds no primitives at all, which is why one can stay declared in a
     * manifest permanently.
     */
    struct ToriRSChrome ui;
    /**
     * The two All Settings pickers: the colour swatch editor and the number
     * entry, as panels in `ui`. The cache has no picker to open -- its op
     * script for a swatch or a field plays a click and returns -- so these ARE
     * the rows' apply, and the value each commits goes straight into the row's
     * varp. See ui/settings_pickers.h.
     */
    struct UISettingsPickers settings_pickers;
    /**
     * The camera on the model-view well, ONE instance shared by the panels
     * of this chrome that show one: the map editor Catalog's preview and the
     * loc editor's. It lives here, with the chrome both are built in, because
     * it exists in every boot the loc editor can open in, editor or not. See
     * editor/editor_preview_camera.h.
     */
    struct EditorPreviewCamera preview_camera;
};

/** A zeroed overlay with the model-view camera reset, allocated in the UI
 *  group. Never NULL. */
struct DevOverlay*
DevOverlay_Create(void);

/** Release an overlay from DevOverlay_Create. NULL is accepted. */
void
DevOverlay_Free(struct DevOverlay* overlay);

/**
 * The frame-time row's text for a mean in microseconds: "%u.%02u ms", or "--"
 * when `mean_us` is 0, which is both "no samples yet" and a run of sub-10us
 * frames. `out` is always terminated.
 */
void
DevOverlay_FrameTimeText(
    uint32_t mean_us,
    char* out,
    size_t out_size);

#endif
