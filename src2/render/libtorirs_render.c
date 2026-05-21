#include "libtorirs_render.h"

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
    if( !render_queue )
        return;
    free(render_queue);
}

void
LibToriRS_RenderQueue_Clear(struct LibToriRS_RenderQueue* render_queue)
{
    if( !render_queue )
        return;
    render_queue->count = 0;
}

bool
LibToriRS_RenderQueue_IsEmpty(struct LibToriRS_RenderQueue* render_queue)
{
    if( !render_queue )
        return true;
    return render_queue->count == 0;
}

void
LibToriRS_RenderQueue_PushCommandModelDraw(
    struct LibToriRS_RenderQueue* render_queue,
    struct ToriDraw_ModelHandle model,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    if( !render_queue )
        return;
    if( render_queue->count >= LIBTORIRS_RENDER_QUEUE_MAX_SIZE )
        return;

    struct LibToriRS_RenderCommand* command = &render_queue->commands[render_queue->count];
    command->kind = TORIRS_RENDER_COMMAND_MODEL;
    command->u.model.model = model;
    memcpy(&command->u.model.position, position, sizeof(struct ToriDraw_Position));
    command->u.model.view_port_ref = view_port;
    command->u.model.camera_ref = camera;
    render_queue->count++;
}