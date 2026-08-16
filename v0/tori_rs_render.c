#include "tori_rs_render.h"

#include "command_buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
render_command_buffer_grow_if_full(struct ToriRSRenderCommandBuffer* buffer)
{
    if( buffer->command_count < buffer->command_capacity )
        return;
    buffer->command_capacity *= 2;
    buffer->commands = realloc(
        buffer->commands, buffer->command_capacity * sizeof(struct ToriRSRenderCommand));
}

struct ToriRSRenderCommandBuffer*
LibToriRS_RenderCommandBufferNew(int capacity)
{
    struct ToriRSRenderCommandBuffer* buffer = malloc(sizeof(struct ToriRSRenderCommandBuffer));
    buffer->commands = malloc(capacity * sizeof(struct ToriRSRenderCommand));
    buffer->command_count = 0;
    buffer->command_capacity = capacity;
    return buffer;
}

void
LibToriRS_RenderCommandBufferFree(struct ToriRSRenderCommandBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->commands);
    free(buffer);
}

void
LibToriRS_RenderCommandBufferAddCommandByCopy(
    struct ToriRSRenderCommandBuffer* buffer,
    const struct ToriRSRenderCommand* command)
{
    render_command_buffer_grow_if_full(buffer);
    buffer->commands[buffer->command_count++] = *command;
}

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferEmplaceCommand(struct ToriRSRenderCommandBuffer* buffer)
{
    render_command_buffer_grow_if_full(buffer);
    struct ToriRSRenderCommand* slot = &buffer->commands[buffer->command_count];
    memset(slot, 0, sizeof(struct ToriRSRenderCommand));
    buffer->command_count++;
    return slot;
}

void
LibToriRS_RenderCommandBufferReset(struct ToriRSRenderCommandBuffer* buffer)
{
    buffer->command_count = 0;
}

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferAt(
    struct ToriRSRenderCommandBuffer* buffer,
    int index)
{
    assert(index >= 0 && index < buffer->command_count);
    return &buffer->commands[index];
}

int
LibToriRS_RenderCommandBufferCount(struct ToriRSRenderCommandBuffer* buffer)
{
    return buffer->command_count;
}