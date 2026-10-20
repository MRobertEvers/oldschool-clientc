#ifndef SRC_TORIRS_VIEWPORT_LETTERBOX_H
#define SRC_TORIRS_VIEWPORT_LETTERBOX_H

/*
 * The world viewport the reference client actually draws: rev-239
 * class159.method5357, whole.
 *
 * Given the viewport widget's box (the clientCode-1337 layer) it interpolates
 * the zoom between the two VIEWPORT_SETFOV endpoints over the box's height
 * (near below 334, far from 434, linear between), forms the aspect term
 * `height * zoom * 512 / (width * 334)`, and pulls that back inside the
 * VIEWPORT_CLAMPFOV bounds:
 *
 *   - below `aspect_min` the zoom is raised until the term reaches it; if that
 *     zoom passes `fov_max` it stops there and the box loses equal bands off
 *     its left and right (a window too wide for the clamp);
 *   - above `aspect_max` the zoom is lowered; if that passes `fov_min` it stops
 *     there and the box loses equal bands off its top and bottom.
 *
 * What is left is the rectangle the scene is drawn into (client.field811/897/
 * 813/837), the projection scale is `height * zoom / 334` over it
 * (client.field817), and the cut bands are painted black (class158.method5313
 * when the widget draws; class71.method2484). Clientscript
 * `viewport_geteffectivesize` (6203) answers the same rectangle's size.
 *
 * Header-only and dependency-free on purpose: the clientscript host, the UI
 * emit walk and the world projection each need this one rule, and they live in
 * layers that do not include one another.
 *
 * The integer interpolation is the reference's (the endpoints are shorts and the
 * expression truncates before it widens), as is every cast below.
 */

/* VIEWPORT_CLAMPFOV's four bounds (Statics.method6341 case 6202), with the
 * reference's unset values: the client starts with them and
 * `viewport_clampfov(0, 0, 0, 0)` restores them. */
#define TORIRS_VIEWPORT_CLAMP_FOV_MIN_UNSET 1
#define TORIRS_VIEWPORT_CLAMP_FOV_MAX_UNSET 32767
#define TORIRS_VIEWPORT_CLAMP_ASPECT_MIN_UNSET 1
#define TORIRS_VIEWPORT_CLAMP_ASPECT_MAX_UNSET 32767

struct ToriRS_ViewportClamp
{
    int fov_min;    /* field804 */
    int fov_max;    /* field805 */
    int aspect_min; /* field1040 */
    int aspect_max; /* field810 */
};

struct ToriRS_ViewportLetterbox
{
    /* The drawn rectangle, in the box's own coordinates. */
    int x;
    int y;
    int w;
    int h;
    /* The zoom method5357 settled on, and the projection scale from it. */
    double zoom;
    int scale;
};

static inline void
ToriRS_ViewportLetterboxCompute(
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    int near_zoom,
    int far_zoom,
    struct ToriRS_ViewportClamp const* clamp,
    struct ToriRS_ViewportLetterbox* out)
{
    int x = box_x;
    int y = box_y;
    int w = box_w < 1 ? 1 : box_w;
    int h = box_h < 1 ? 1 : box_h;
    int const band = h - 334;
    double zoom;
    double aspect;

    if( band < 0 )
        zoom = near_zoom;
    else if( band >= 100 )
        zoom = far_zoom;
    else
        zoom = (far_zoom - near_zoom) * band / 100 + near_zoom;

    aspect = h * zoom * 512.0 / (w * 334);
    if( aspect < clamp->aspect_min )
    {
        double const floor_aspect = clamp->aspect_min;
        zoom = w * floor_aspect * 334.0 / (h * 512);
        if( zoom > clamp->fov_max )
        {
            double visible;
            int cut;

            zoom = clamp->fov_max;
            visible = h * zoom * 512.0 / (floor_aspect * 334.0);
            cut = (int)((w - visible) / 2.0);
            x += cut;
            w -= cut * 2;
        }
    }
    else if( aspect > clamp->aspect_max )
    {
        double const ceil_aspect = clamp->aspect_max;
        zoom = w * ceil_aspect * 334.0 / (h * 512);
        if( zoom < clamp->fov_min )
        {
            double visible;
            int cut;

            zoom = clamp->fov_min;
            visible = w * ceil_aspect * 334.0 / (zoom * 512.0);
            cut = (int)((h - visible) / 2.0);
            y += cut;
            h -= cut * 2;
        }
    }

    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
    out->zoom = zoom;
    out->scale = (int)(h * zoom / 334.0);
}

#endif /* SRC_TORIRS_VIEWPORT_LETTERBOX_H */
