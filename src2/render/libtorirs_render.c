#include "libtorirs_render.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LibToriRS_RenderQueue*
LibToriRS_RenderQueue_New(void)
{
    struct LibToriRS_RenderQueue* render_queue = malloc(sizeof(struct LibToriRS_RenderQueue));
    memset(render_queue, 0, sizeof(struct LibToriRS_RenderQueue));
    return render_queue;
}

void
LibToriRS_RenderQueue_Free(struct LibToriRS_RenderQueue* render_queue)
{
    assert(render_queue);
    free(render_queue);
}

void
LibToriRS_RenderQueue_Clear(struct LibToriRS_RenderQueue* render_queue)
{
    assert(render_queue);
    render_queue->count = 0;
}

bool
LibToriRS_RenderQueue_IsEmpty(struct LibToriRS_RenderQueue* render_queue)
{
    assert(render_queue);
    return render_queue->count == 0;
}

void
LibToriRS_RenderQueue_PushCommandBegin3D(
    struct LibToriRS_RenderQueue* render_queue,
    const struct ToriDraw_ViewPort* view_port,
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_Position* camera_position)
{
    assert(render_queue);
    if( render_queue->count >= LIBTORIRS_RENDER_QUEUE_MAX_SIZE )
        return;

    struct LibToriRS_RenderCommand* command = &render_queue->commands[render_queue->count];
    command->kind = TORIRSRC_BEGIN_3D;
    if( view_port )
        command->u.begin_3d.view_port = *view_port;
    if( camera )
        command->u.begin_3d.camera = *camera;
    if( camera_position )
        command->u.begin_3d.camera_position = *camera_position;
    render_queue->count++;
}

void
LibToriRS_RenderQueue_PushCommandModelDraw(
    struct LibToriRS_RenderQueue* render_queue,
    struct ToriDraw_ModelHandle model,
    const struct ToriDraw_Position* position)
{
    assert(render_queue);
    if( render_queue->count >= LIBTORIRS_RENDER_QUEUE_MAX_SIZE )
        return;

    struct LibToriRS_RenderCommand* command = &render_queue->commands[render_queue->count];
    command->kind = TORIRSRC_MODEL;
    command->u.model.model = model;
    if( position )
        memcpy(&command->u.model.position, position, sizeof(struct ToriDraw_Position));
    render_queue->count++;
}

void
LibToriRS_RenderQueue_PushCommandEnd3D(struct LibToriRS_RenderQueue* render_queue)
{
    assert(render_queue);
    if( render_queue->count >= LIBTORIRS_RENDER_QUEUE_MAX_SIZE )
        return;

    struct LibToriRS_RenderCommand* command = &render_queue->commands[render_queue->count];
    command->kind = TORIRSRC_END_3D;
    render_queue->count++;
}
