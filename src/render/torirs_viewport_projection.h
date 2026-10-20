#ifndef SRC_RENDER_TORIRS_VIEWPORT_PROJECTION_H
#define SRC_RENDER_TORIRS_VIEWPORT_PROJECTION_H

/**
 * The world camera's projection, chosen from the world viewport's height.
 *
 * The later client derives its projection scale from the viewport rather than
 * using a constant, and interpolates it over a band around the reference
 * height of 334. The 2004 client does not: its projection is the bare `<< 9`
 * in Model.project, scale 512, whatever the viewport measures.
 *
 * Getting the era wrong is not subtle once seen and invisible until then. A
 * 2004-era frame recomputed from a 335-high viewport with the SETFOV endpoints
 * that era never writes lands on `335 * 256 / 334` = 256 -- half the reference
 * scale, which reads as an eye at twice the distance. That is the "camera is
 * too far out" report against rev 289, whose `[camera]` section is the shared
 * one.
 *
 * Four inputs decide it, in this order of authority:
 *
 *   1. an explicit field-of-view override, which switches projection modes
 *      entirely;
 *   2. `wedge_mode` OFF, which declines to touch the camera at all;
 *   3. `wedge_mode` AUTO on a revision whose `[camera] viewport_zoom` is off,
 *      which pins the constant;
 *   4. otherwise the viewport-derived scale, or a forced one.
 *
 * A forced scale still wins over (3), because it exists to bisect exactly
 * that case.
 */

#include "torirs_env_values.h"
#include "torirs_viewport_letterbox.h"

/** The reference viewport height the zoom band is centred on. */
#define TORIRS_VIEWPORT_REFERENCE_HEIGHT 334
/** Pixels above the reference over which near zoom eases into far zoom. */
#define TORIRS_VIEWPORT_ZOOM_BAND 100
/** What `viewport_zoom_near`/`_far` mean when the cache states neither. */
#define TORIRS_VIEWPORT_ZOOM_DEFAULT 256

enum ToriRS_ViewportProjectionAction
{
    /** Leave the camera exactly as it is. */
    TORIRS_VIEWPORT_PROJECTION_KEEP = 0,
    /** Use `scale` with the scale projection mode. */
    TORIRS_VIEWPORT_PROJECTION_SCALE,
    /** Use `fov_rpi2048` with the field-of-view projection mode. */
    TORIRS_VIEWPORT_PROJECTION_FOV
};

struct ToriRS_ViewportProjection
{
    enum ToriRS_ViewportProjectionAction action;
    int scale;
    int fov_rpi2048;
    /** The zoom the band produced, for the trace. 0 when none was computed. */
    int zoom;
    int near_zoom;
    int far_zoom;
};

/**
 * Decide the projection.
 *
 * `wedge_mode` is a ToriRS_EnvScaleMode value: TORIRS_ENV_SCALE_OFF to decline,
 * TORIRS_ENV_SCALE_AUTO to derive, or an explicit scale of 8 or more.
 * `fov_override` is -1 when unset. `viewport_zoom_enabled` is the revision's
 * `[camera] viewport_zoom`. `viewport_valid` is false before the emit walk has
 * found a world rectangle, which is every frame before the first one.
 *
 * `viewport_width`/`viewport_height` are the viewport WIDGET's box, before any
 * letterbox, and `clamp` its VIEWPORT_CLAMPFOV bounds: the derived scale is
 * class159.method5357's field817 (torirs_viewport_letterbox.h), which lifts the
 * zoom when a clamp asks for it -- a cutscene's `viewport_clampfov(512, 512,
 * 512, 512)` roughly doubles it on a wide window.
 */
struct ToriRS_ViewportProjection
ToriRS_ProjectionForViewport(
    int wedge_mode,
    int fov_override,
    int viewport_zoom_enabled,
    int viewport_valid,
    int viewport_width,
    int viewport_height,
    int near_zoom,
    int far_zoom,
    struct ToriRS_ViewportClamp const* clamp);

#endif /* SRC_RENDER_TORIRS_VIEWPORT_PROJECTION_H */
