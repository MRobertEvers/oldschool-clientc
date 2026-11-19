#include "ui/canvas_scale.h"

#include <assert.h>

void
UICanvasScale_SetWindow(
    struct UICanvasScale* scale,
    int window_w,
    int window_h)
{
    assert(scale);
    assert(window_w > 0);
    assert(window_h > 0);
    scale->window_w = window_w;
    scale->window_h = window_h;
}

bool
UICanvasScale_HasWindow(struct UICanvasScale const* scale)
{
    assert(scale);
    return scale->window_w > 0 && scale->window_h > 0;
}

int
UICanvasScale_DensityPercent(struct UICanvasScale const* scale)
{
    assert(scale);
    return scale->density_percent > 0 ? scale->density_percent : 100;
}

bool
UICanvasScale_SetDensity(
    struct UICanvasScale* scale,
    int density_percent)
{
    assert(scale);
    assert(density_percent > 0);
    if( scale->density_percent == density_percent )
        return false;
    scale->density_percent = density_percent;
    return true;
}

enum ClientScaleHighDpi
UICanvasScale_HighDpiAuto(struct UICanvasScale const* scale)
{
    assert(scale);
    return scale->high_dpi_auto;
}

bool
UICanvasScale_SetHighDpiAuto(
    struct UICanvasScale* scale,
    enum ClientScaleHighDpi mode)
{
    assert(scale);
    assert(mode >= 0);
    assert(mode < CLIENT_SCALE_HIGH_DPI_COUNT);
    if( scale->high_dpi_auto == mode )
        return false;
    scale->high_dpi_auto = mode;
    return true;
}

void
UICanvasScale_SetPresent(
    struct UICanvasScale* scale,
    struct ClientScalePresent const* present)
{
    assert(scale);
    assert(present);
    scale->present = *present;
    scale->present_known = true;
}

void
UICanvasScale_SetFixedLayout(
    struct UICanvasScale* scale,
    int frame_w,
    int frame_h,
    int percent,
    int chosen)
{
    assert(scale);
    scale->layout.w = frame_w;
    scale->layout.h = frame_h;
    scale->layout.percent = percent;
    scale->layout.shown_percent = scale->layout.percent;
    scale->layout.rounded_by_integer = scale->layout.percent != chosen;
    scale->layout.raised_by_limit = 0;
    scale->layout.lowered_to_fit = 0;
}

void
UICanvasScale_TakeShownPercent(struct UICanvasScale* scale)
{
    assert(scale);
    scale->shown_percent = scale->layout.shown_percent;
}

bool
UICanvasScale_NativeWindowModeSynced(struct UICanvasScale const* scale)
{
    assert(scale);
    return scale->native_window_mode_synced;
}

void
UICanvasScale_MarkNativeWindowModeSynced(struct UICanvasScale* scale)
{
    assert(scale);
    scale->native_window_mode_synced = true;
}
