#include "editor/editor_preview_camera.h"

#include <assert.h>
#include <string.h>

void
EditorPreviewCamera_Reset(struct EditorPreviewCamera* camera)
{
    assert(camera);
    memset(camera, 0, sizeof(*camera));
    camera->pitch = EDITOR_PREVIEW_DEFAULT_PITCH;
    camera->yaw = EDITOR_PREVIEW_DEFAULT_YAW;
    camera->zoom = EDITOR_PREVIEW_FIT_ZOOM_MIN;
    camera->fit_pending = true;
}

void
EditorPreviewCamera_Invalidate(
    struct EditorPreviewCamera* camera,
    bool same_model)
{
    assert(camera);
    camera->dirty = true;
    if( same_model )
        return;
    camera->pitch = EDITOR_PREVIEW_DEFAULT_PITCH;
    camera->yaw = EDITOR_PREVIEW_DEFAULT_YAW;
    camera->fit_pending = true;
}

void
EditorPreviewCamera_Orbit(
    struct EditorPreviewCamera* camera,
    int pitch_step,
    int yaw_step)
{
    assert(camera);
    /* Wrapping, not clamped: the well is a turntable and a model seen from
     * behind is a legitimate thing to want. */
    camera->pitch = (camera->pitch + pitch_step) & 2047;
    camera->yaw = (camera->yaw + yaw_step) & 2047;
    EditorPreviewCamera_Invalidate(camera, true);
}

void
EditorPreviewCamera_Zoom(
    struct EditorPreviewCamera* camera,
    int direction)
{
    assert(camera);
    if( direction == 0 )
        return;
    /* By fiftieths of where it is, so a step feels the same at either end --
     * a fixed step is imperceptible zoomed out and a lurch zoomed in. */
    if( direction < 0 )
        camera->zoom = camera->zoom * 49 / 50;
    else
        camera->zoom = camera->zoom * 51 / 50;
    if( camera->zoom < EDITOR_PREVIEW_ZOOM_MIN )
        camera->zoom = EDITOR_PREVIEW_ZOOM_MIN;
    if( camera->zoom > EDITOR_PREVIEW_ZOOM_MAX )
        camera->zoom = EDITOR_PREVIEW_ZOOM_MAX;
    EditorPreviewCamera_Invalidate(camera, true);
}

bool
EditorPreviewCamera_TakeRender(struct EditorPreviewCamera* camera)
{
    assert(camera);

    if( !camera->dirty )
        return false;
    camera->dirty = false;
    return true;
}

bool
EditorPreviewCamera_TakeFit(
    struct EditorPreviewCamera* camera,
    int radius,
    int min_y,
    int max_y)
{
    int height;
    int size;

    assert(camera);

    if( !camera->fit_pending )
        return false;
    camera->fit_pending = false;

    /* Whichever way the model is long. A gate is wide and short, a lamp post
     * is narrow and tall, and fitting on one dimension loses the other. */
    height = max_y - min_y;
    size = 2 * radius > height ? 2 * radius : height;
    camera->zoom = (size * 9) / 2;
    /* Clamped because a degenerate bounds -- a model with no faces, or one
     * that has not finished loading -- would otherwise zoom to nothing or to
     * infinity, and both render an empty well that looks like a missing
     * model rather than a bad number. */
    if( camera->zoom < EDITOR_PREVIEW_FIT_ZOOM_MIN )
        camera->zoom = EDITOR_PREVIEW_FIT_ZOOM_MIN;
    if( camera->zoom > EDITOR_PREVIEW_FIT_ZOOM_MAX )
        camera->zoom = EDITOR_PREVIEW_FIT_ZOOM_MAX;
    return true;
}
