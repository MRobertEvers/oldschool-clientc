#include "tori_rs_render.h"

#include "command_buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
LibToriRS_RenderCommandBufferAddCommand(
    struct ToriRSRenderCommandBuffer* buffer,
    struct ToriRSRenderCommand command)
{
    if( buffer->command_count >= buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands = realloc(
            buffer->commands, buffer->command_capacity * sizeof(struct ToriRSRenderCommand));
    }
    buffer->commands[buffer->command_count++] = command;
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