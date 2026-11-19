#ifndef SRC_UI_CANVAS_SCALE_H
#define SRC_UI_CANVAS_SCALE_H

/*
 * What the client knows about how its canvas is scaled, between the shell's
 * reports and the settings that divide them: the last unscaled window size,
 * the layout the last resize or setting change chose, what the shell last
 * presented with, the display density and what HighDPI "automatic" means on
 * this lane, and whether the lane's own window mode has been restated yet.
 *
 * The window size is kept because the canvas is a lossy function of it: at
 * 200% a 1600x900 window and a 1601x901 one both become 800x450, so the
 * canvas cannot be scaled back up when a setting changes. Until the shell has
 * reported one there is nothing to divide, and a setting change has to wait.
 *
 * A fixed window never runs the resizable layout, so its layout is stated
 * here from the pinned frame and the percent the window was sized by. The
 * readout's percent is always the layout's, in the unit the player chose it in.
 *
 * Nothing here reads a setting, a window or a platform. How interface scaling,
 * stretch mode, HighDPI and the pixel limit combine is platform/client_scale.h,
 * which the caller runs; the caller also reports the density and presents,
 * decides when a change has to be relaid out, and logs.
 */

#include "platform/client_scale.h"

#include <stdbool.h>

struct UICanvasScale
{
    /** The last unscaled game-area size the shell reported
     *  (TORIRS_CMD_WINDOW_RESIZE). 0 until the first resize. */
    int window_w;
    int window_h;
    /** The layout the last resize or setting change asked for (before the
     *  canvas floor), and the percent it really used. In fixed mode the
     *  percent is the one the window was sized by. */
    struct ClientScaleLayout layout;
    /** What the shell presented the last frame with. Reported, not decided:
     *  the shell calls ClientScale_Present itself. */
    struct ClientScalePresent present;
    bool present_known;
    /** The scale the frame is shown at, in the unit it was chosen in. */
    int shown_percent;
    /** Drawable pixels per window point as a percent, as the platform last
     *  detected it; 0 until it reports one. */
    int density_percent;
    /** What HighDPI "automatic" means on this lane. */
    enum ClientScaleHighDpi high_dpi_auto;
    /** The lane's window mode has been restated once, at plugin host start. */
    bool native_window_mode_synced;
};

/** Record the window size a resize reported. Both above 0. */
void
UICanvasScale_SetWindow(
    struct UICanvasScale* scale,
    int window_w,
    int window_h);

/** True once a resize has reported a window to divide. */
bool
UICanvasScale_HasWindow(struct UICanvasScale const* scale);

/** The density, or 100 before a platform has reported one: a headless run has
 *  no display, and 1x is what its canvas has always been. */
int
UICanvasScale_DensityPercent(struct UICanvasScale const* scale);

/** Store a detected density, above 0. True when it differs from the one held. */
bool
UICanvasScale_SetDensity(
    struct UICanvasScale* scale,
    int density_percent);

enum ClientScaleHighDpi
UICanvasScale_HighDpiAuto(struct UICanvasScale const* scale);

/** Store what HighDPI "automatic" means. True when it changed. */
bool
UICanvasScale_SetHighDpiAuto(
    struct UICanvasScale* scale,
    enum ClientScaleHighDpi mode);

/** Record what the shell presented with. */
void
UICanvasScale_SetPresent(
    struct UICanvasScale* scale,
    struct ClientScalePresent const* present);

/**
 * State a fixed window's layout: the pinned `frame_w` x `frame_h` at `percent`,
 * which the settings rounded from the `chosen` one. Neither the pixel limit nor
 * the frame floor touches a fixed window.
 */
void
UICanvasScale_SetFixedLayout(
    struct UICanvasScale* scale,
    int frame_w,
    int frame_h,
    int percent,
    int chosen);

/** Take the shown percent from the layout, after a present. */
void
UICanvasScale_TakeShownPercent(struct UICanvasScale* scale);

bool
UICanvasScale_NativeWindowModeSynced(struct UICanvasScale const* scale);

/** Record that the lane's window mode has been restated. */
void
UICanvasScale_MarkNativeWindowModeSynced(struct UICanvasScale* scale);

#endif
