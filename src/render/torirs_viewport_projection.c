#include "render/torirs_viewport_projection.h"

#include <assert.h>

#include "impl/projection/projection.scalar_reference.h"

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
    struct ToriRS_ViewportClamp const* clamp)
{
    struct ToriRS_ViewportProjection result = { TORIRS_VIEWPORT_PROJECTION_KEEP, 0, 0, 0, 0, 0 };
    int scale;

    assert(clamp);

    /* 1. An explicit field of view changes the projection MODE, so it is
     *    answered before anything about scale. */
    if( fov_override > 0 )
    {
        result.action = TORIRS_VIEWPORT_PROJECTION_FOV;
        result.fov_rpi2048 = fov_override;
        return result;
    }

    /* 2. Switched off: the camera keeps whatever it already had. */
    if( wedge_mode == TORIRS_ENV_SCALE_OFF )
        return result;

    /* 3. A revision with no viewport-derived zoom pins the constant. An
     *    explicitly forced scale still wins, which is why this tests AUTO
     *    rather than "not forced". */
    if( wedge_mode == TORIRS_ENV_SCALE_AUTO && !viewport_zoom_enabled )
    {
        result.action = TORIRS_VIEWPORT_PROJECTION_SCALE;
        result.scale = TORIDRAW_PROJECTION_SCALE_DEFAULT;
        return result;
    }

    /* 4. From here the viewport is needed, and before the first emit walk
     *    there is not one. */
    if( !viewport_valid || viewport_width < 1 || viewport_height < 1 )
        return result;

    if( wedge_mode > 0 )
    {
        scale = wedge_mode;
    }
    else
    {
        struct ToriRS_ViewportLetterbox box;

        if( near_zoom < 1 )
            near_zoom = TORIRS_VIEWPORT_ZOOM_DEFAULT;
        if( far_zoom < 1 )
            far_zoom = TORIRS_VIEWPORT_ZOOM_DEFAULT;
        ToriRS_ViewportLetterboxCompute(
            0, 0, viewport_width, viewport_height, near_zoom, far_zoom, clamp, &box);
        scale = box.scale;
        result.zoom = (int)box.zoom;
        result.near_zoom = near_zoom;
        result.far_zoom = far_zoom;
    }
    if( scale < 1 )
        scale = 1;

    result.action = TORIRS_VIEWPORT_PROJECTION_SCALE;
    result.scale = scale;
    return result;
}
