#ifndef SRC_UI_DEV_OVERLAY_H
#define SRC_UI_DEV_OVERLAY_H

/*
 * The developer overlay: the in-canvas chrome instance, the frame-time panel
 * drawn in it, the ring of frame durations that panel averages, and the two
 * All Settings pickers that are panels in the same instance.
 *
 * The chrome instance belongs to this record. The loc editor's panel and the
 * map editor's panels (editor/editor_shell.h) are built in it too, but the
 * editor shell holds only the widget ids it was handed back: the App unit
 * passes `&overlay->ui` to every editor call that adds, reads or shows a
 * panel, and the plugin window's instance is copied from it once at init.
 *
 * It lives on the heap, in the UI group, and exists for as long as the App
 * does: DevOverlay_Create hands back a zeroed record with an empty frame-time
 * ring and no chrome initialised, and DevOverlay_Free releases it.
 *
 * This decides what the frame-time row reads for a mean: milliseconds to two
 * decimals, or "--" when the mean is 0.
 *
 * Nothing here initialises, builds or draws the chrome, reads input or a
 * clock, or prints. The caller does the chrome work through the fields below,
 * adds each frame's duration to the ring, decides when the panel is toggled,
 * and sets the row's text.
 */

#include "perf/frame_time_ring.h"
#include "ui/settings_pickers.h"
#include "ui/uitree_debug_overlay.h"

#include <stddef.h>
#include <stdint.h>

struct DevOverlay
{
    /**
     * The developer chrome instance. One minimenu-styled panel carries the
     * frame time, averaged over the last FRAME_TIME_RING_SAMPLES frames. It is
     * hidden until the toggle key, and a hidden panel builds no primitives at
     * all, which is why it can stay declared in a manifest permanently. The
     * loc editor, the map editor and the settings pickers are panels here too.
     */
    struct ToriRSChrome ui;
    /** The frame-time panel and its one row in `ui`, 0 until the App's
     *  overlay init built them. */
    int panel;
    int frame_row;
    /** Whether the frame-time panel is shown. */
    int visible;
    /** The last few frame durations, and their mean. It keeps filling while
     *  the panel is hidden, so the average is already settled the frame it is
     *  shown. See perf/frame_time_ring.h. */
    struct FrameTimeRing frame_times;
    /**
     * The two All Settings pickers: the colour swatch editor and the number
     * entry, as panels in `ui`. The cache has no picker to open -- its op
     * script for a swatch or a field plays a click and returns -- so these ARE
     * the rows' apply, and the value each commits goes straight into the row's
     * varp. See ui/settings_pickers.h.
     */
    struct UISettingsPickers settings_pickers;
};

/** A zeroed overlay with an empty frame-time ring, allocated in the UI group.
 *  Never NULL. */
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
